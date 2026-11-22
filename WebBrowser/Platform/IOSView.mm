#include "IOSView.hpp"
#include "../Core/Rules.hpp"
#include <QQuickWindow>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

static QPointer<Browser> browser;
static QPointer<Downloads> downloads;
static QHash<QString,IOSView*> views;
static WKWebsiteDataStore* privateStore;
static NSString* ns(QString s){return [NSString stringWithUTF8String:s.toUtf8().constData()];}
static QString qs(NSString* s){return s?QString::fromUtf8(s.UTF8String):QString();}
static UIViewController* topController(UIView* v){
    UIViewController* c=v.window.rootViewController;while(c.presentedViewController)c=c.presentedViewController;return c;
}

@interface BrowserDownload : NSObject<WKDownloadDelegate>
@property(nonatomic,strong) WKDownload* download;
@property(nonatomic,strong) WKWebView* view;
@property(nonatomic,strong) NSData* resumeData;
@property(nonatomic,strong) NSURL* destination;
@property(nonatomic,strong) NSTimer* timer;
@property(nonatomic) BOOL privateMode;
@property(nonatomic) BOOL pausing;
@property(nonatomic) BOOL resuming;
@property(nonatomic) BOOL cancelled;
@property(nonatomic) BOOL finished;
- (void)start:(WKDownload*)d view:(WKWebView*)v privateMode:(BOOL)p;
- (void)perform:(NSString*)a;
- (void)updateProgress;
- (void)cancelIfUnconfirmed;
@end
@implementation BrowserDownload {
    QString _id;
    qint64 _total;
}
- (void)start:(WKDownload*)d view:(WKWebView*)v privateMode:(BOOL)p {
    self.download=d;self.view=v;self.privateMode=p;d.delegate=self;
}
- (void)updateProgress {
    if(!downloads||_id.isEmpty())return;
    NSProgress* progress=nil;
    if([self.download respondsToSelector:@selector(progress)])progress=[(id<NSProgressReporting>)self.download progress];
    qint64 bytes=progress?progress.completedUnitCount:QFileInfo(qs(self.destination.path)).size();
    qint64 total=progress&&progress.totalUnitCount>0?progress.totalUnitCount:_total;
    downloads->update(_id,{{"received",qMax(qint64(0),bytes)},{"total",total}});
}
- (void)download:(WKDownload*)d decideDestinationUsingResponse:(NSURLResponse*)response suggestedFilename:(NSString*)filename completionHandler:(void (^)(NSURL*))completion {
    if(self.cancelled){completion(nil);return;}
    if(self.destination){completion(self.destination);return;}
    auto n=QString::fromStdString(web::safeFilename(qs(filename).toStdString()));
    auto dir=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/Downloads";QDir().mkpath(dir);
    auto path=QDir(dir).filePath(n);
    for(int i=1;QFileInfo::exists(path);++i)path=QDir(dir).filePath(QString::number(i)+"-"+n);
    _total=response.expectedContentLength;
    auto root=topController(self.view);
    if(!root||!downloads){completion(nil);return;}
    auto alert=[UIAlertController alertControllerWithTitle:@"Save download?" message:ns(n) preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*){completion(nil);self.finished=YES;}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction*){
        if(!downloads||self.cancelled){completion(nil);return;}
        self->_id=downloads->add(n,path,self.privateMode);
        if(self->_id.isEmpty()){completion(nil);return;}
        // The model owns this delegate for the lifetime of a resumable download.
        BrowserDownload* strong=self;
        downloads->handler(self->_id,[strong](QString a){[strong perform:ns(a)];});
        self.destination=[NSURL fileURLWithPath:ns(path)];
        downloads->update(self->_id,{{"state","Downloading"},{"total",self->_total},{"canPause",true}});
        completion(self.destination);
        __weak BrowserDownload* weak=self;
        self.timer=[NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer*){
            BrowserDownload* s=weak;if(!s||!downloads)return;
            [s updateProgress];
        }];
    }]];
    [root presentViewController:alert animated:YES completion:nil];
}
- (void)finishState:(QString)state resumable:(BOOL)resumable {
    [self.timer invalidate];self.timer=nil;
    if(downloads&&!_id.isEmpty()){
        QVariantMap fields{{"state",state},{"finished",!resumable},{"canCancel",bool(resumable)},{"canPause",false},{"canResume",bool(resumable)}};
        if(state=="Completed")fields["received"]=QFileInfo(qs(self.destination.path)).size();downloads->update(_id,fields);
    }
    self.finished=!resumable;
    if(self.finished){self.download=nil;self.view=nil;self.resumeData=nil;}
}
- (void)cancelIfUnconfirmed { if(_id.isEmpty()&&!self.finished)[self perform:@"cancel"]; }
- (void)downloadDidFinish:(WKDownload*)d { [self finishState:QStringLiteral("Completed") resumable:NO];self.download=nil;self.view=nil; }
- (void)download:(WKDownload*)d didFailWithError:(NSError*)error resumeData:(NSData*)resumeData {
    if(self.pausing||self.cancelled)return;
    self.resumeData=resumeData;[self finishState:qs(error.localizedDescription) resumable:resumeData!=nil];
}
- (void)perform:(NSString*)a {
    if(self.finished)return;
    if([a isEqualToString:@"pause"]){
        if(self.pausing||self.resuming||!self.download)return;self.pausing=YES;
        if(downloads)downloads->update(_id,{{"state","Pausing"},{"canPause",false}});
        [self.download cancel:^(NSData* data){self.pausing=NO;if(self.cancelled)return;self.resumeData=data;self.download=nil;[self finishState:data?QStringLiteral("Paused"):QStringLiteral("Server cannot resume this download") resumable:data!=nil];}];
    }else if([a isEqualToString:@"resume"]&&self.resumeData&&self.view&&!self.resuming){
        NSData* data=self.resumeData;self.resumeData=nil;self.finished=NO;self.resuming=YES;
        if(downloads)downloads->update(_id,{{"state","Resuming"},{"canResume",false}});
        [self.view resumeDownloadFromResumeData:data completionHandler:^(WKDownload* d){
            self.resuming=NO;if(self.cancelled){[d cancel:nil];return;}if(!d){[self finishState:QStringLiteral("Could not resume download") resumable:NO];return;}self.download=d;d.delegate=self;
            if(downloads)downloads->update(self->_id,{{"state","Downloading"},{"finished",false},{"canPause",true},{"canCancel",true},{"canResume",false}});
            __weak BrowserDownload* weak=self;self.timer=[NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer*){BrowserDownload* s=weak;if(s)[s updateProgress];}];
        }];
    }else if([a isEqualToString:@"cancel"]){
        self.cancelled=YES;[self.download cancel:nil];self.resumeData=nil;self.download=nil;
        [self finishState:QStringLiteral("Cancelled") resumable:NO];self.view=nil;
    }
}
@end

@interface BrowserDelegate : NSObject<WKNavigationDelegate,WKUIDelegate>
@property(nonatomic,assign) IOSView* owner;
@property(nonatomic,strong) NSMutableArray<BrowserDownload*>* pending;
@end
@implementation BrowserDelegate
- (void)webView:(WKWebView*)v didFinishNavigation:(WKNavigation*)n {if(self.owner)self.owner->report(true);}
- (void)webView:(WKWebView*)v didStartProvisionalNavigation:(WKNavigation*)n {if(self.owner)self.owner->report(false);}
- (void)webView:(WKWebView*)v didFailNavigation:(WKNavigation*)n withError:(NSError*)e {if(self.owner)self.owner->report(false,qs(e.localizedDescription));}
- (void)webView:(WKWebView*)v didFailProvisionalNavigation:(WKNavigation*)n withError:(NSError*)e {if(self.owner&&e.code!=NSURLErrorCancelled)self.owner->report(false,qs(e.localizedDescription));}
- (void)webView:(WKWebView*)v decidePolicyForNavigationAction:(WKNavigationAction*)a decisionHandler:(void (^)(WKNavigationActionPolicy))done{
    NSString* s=a.request.URL.scheme.lowercaseString;
    if(a.shouldPerformDownload){done(WKNavigationActionPolicyDownload);return;}
    if([@[@"http",@"https",@"about",@"blob",@"data"] containsObject:s?:@""]){done(WKNavigationActionPolicyAllow);return;}done(WKNavigationActionPolicyCancel);
}
- (void)webView:(WKWebView*)v decidePolicyForNavigationResponse:(WKNavigationResponse*)r decisionHandler:(void (^)(WKNavigationResponsePolicy))done{
    NSString* disposition=nil;
    if([r.response isKindOfClass:NSHTTPURLResponse.class])disposition=[(NSHTTPURLResponse*)r.response valueForHTTPHeaderField:@"Content-Disposition"];
    done(!r.canShowMIMEType||[disposition.lowercaseString hasPrefix:@"attachment"]?WKNavigationResponsePolicyDownload:WKNavigationResponsePolicyAllow);
}
- (void)attach:(WKDownload*)d view:(WKWebView*)v{
    if(!self.owner||!self.owner->tab())return;
    if(!self.pending)self.pending=[NSMutableArray array];
    // Retain pending confirmation delegates; completed delegates are pruned on next download.
    for(BrowserDownload* x in [self.pending copy])if(x.finished)[self.pending removeObject:x];
    if(self.pending.count>=200){[d cancel:nil];return;}
    auto x=[BrowserDownload new];[self.pending addObject:x];[x start:d view:v privateMode:self.owner->tab()->privateMode()];
}
- (void)webView:(WKWebView*)v navigationAction:(WKNavigationAction*)a didBecomeDownload:(WKDownload*)d{[self attach:d view:v];}
- (void)webView:(WKWebView*)v navigationResponse:(WKNavigationResponse*)r didBecomeDownload:(WKDownload*)d{[self attach:d view:v];}
- (WKWebView*)webView:(WKWebView*)v createWebViewWithConfiguration:(WKWebViewConfiguration*)c forNavigationAction:(WKNavigationAction*)a windowFeatures:(WKWindowFeatures*)f{
    if(!self.owner||!browser||!self.owner->tab()||!self.owner->shown())return nil;
    if(!Store::webUrl(QUrl(qs(a.request.URL.absoluteString)))&&qs(a.request.URL.absoluteString)!="about:blank")return nil;
    auto t=browser->newTab(QUrl("about:blank"),self.owner->tab()->privateMode());if(!t)return nil;
    auto target=IOSView::lookup(t->id());if(target){target->rebuild((__bridge void*)c);return (__bridge WKWebView*)target->native();}
    t->navigate(QUrl(qs(a.request.URL.absoluteString)));return nil;
}
- (void)webView:(WKWebView*)v requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin initiatedByFrame:(WKFrameInfo*)frame type:(WKMediaCaptureType)type decisionHandler:(void (^)(WKPermissionDecision))done{
    auto root=topController(v);if(!root||!self.owner||!self.owner->shown()){done(WKPermissionDecisionDeny);return;}
    NSString* what=type==WKMediaCaptureTypeCamera?@"camera":type==WKMediaCaptureTypeMicrophone?@"microphone":@"camera and microphone";
    UIAlertController* a=[UIAlertController alertControllerWithTitle:@"Website permission" message:[NSString stringWithFormat:@"%@://%@:%ld requests %@ access.",origin.protocol,origin.host,(long)origin.port,what] preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Deny" style:UIAlertActionStyleCancel handler:^(UIAlertAction*){done(WKPermissionDecisionDeny);}]];
    [a addAction:[UIAlertAction actionWithTitle:@"Allow" style:UIAlertActionStyleDefault handler:^(UIAlertAction*){done(WKPermissionDecisionGrant);}]];
    [root presentViewController:a animated:YES completion:nil];
}
- (void)webView:(WKWebView*)v runJavaScriptAlertPanelWithMessage:(NSString*)m initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(void))done{
    auto c=topController(v);if(!c||!self.owner||!self.owner->shown()){done();return;}
    auto a=[UIAlertController alertControllerWithTitle:f.securityOrigin.host message:m preferredStyle:UIAlertControllerStyleAlert];[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction*){done();}]];[c presentViewController:a animated:YES completion:nil];
}
- (void)webView:(WKWebView*)v runJavaScriptConfirmPanelWithMessage:(NSString*)m initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(BOOL))done{
    auto c=topController(v);if(!c||!self.owner||!self.owner->shown()){done(NO);return;}
    auto a=[UIAlertController alertControllerWithTitle:f.securityOrigin.host message:m preferredStyle:UIAlertControllerStyleAlert];[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*){done(NO);}]];[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction*){done(YES);}]];[c presentViewController:a animated:YES completion:nil];
}
- (void)webView:(WKWebView*)v runJavaScriptTextInputPanelWithPrompt:(NSString*)m defaultText:(NSString*)text initiatedByFrame:(WKFrameInfo*)f completionHandler:(void (^)(NSString*))done{
    auto c=topController(v);if(!c||!self.owner||!self.owner->shown()){done(nil);return;}
    auto a=[UIAlertController alertControllerWithTitle:f.securityOrigin.host message:m preferredStyle:UIAlertControllerStyleAlert];[a addTextFieldWithConfigurationHandler:^(UITextField* t){t.text=text;}];[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*){done(nil);}]];[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction*){done(a.textFields.firstObject.text);}]];[c presentViewController:a animated:YES completion:nil];
}
@end

struct IOSView::State { WKWebView* view=nil; BrowserDelegate* delegate=nil; int generation=0; QUrl committed; QVariantMap applied; int progress=-1; bool loading=false,back=false,forward=false; };
void IOSView::initialize(Browser* b,Downloads* d){browser=b;downloads=d;QObject::connect(b,&Browser::privateEnded,b,[d]{d->clearPrivate();privateStore=nil;});}
IOSView* IOSView::lookup(QString id){return views.value(id);}
IOSView::IOSView(QQuickItem* p):QQuickItem(p),state_(new State){
    timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,[this]{sync();report(false,error_);});
    connect(this,&QQuickItem::windowChanged,this,[this]{sync();});
    connect(this,&QQuickItem::visibleChanged,this,[this]{sync();});
    if(browser)connect(browser,&Browser::optionsChanged,this,&IOSView::configureRules);
}
IOSView::~IOSView(){if(tab_)views.remove(tab_->id());for(BrowserDownload* d in state_->delegate.pending)[d cancelIfUnconfirmed];state_->delegate.owner=nullptr;state_->view.navigationDelegate=nil;state_->view.UIDelegate=nil;[state_->view removeFromSuperview];delete state_;}
void IOSView::setTab(Tab* t){if(t==tab_)return;tab_=t;if(!t)return;views[t->id()]=this;connect(t,&Tab::loadRequested,this,&IOSView::load);rebuild();load(t->url());emit changed();}
void IOSView::rebuild(void* configuration){
    ++findGeneration_;findResult_.clear();
    state_->delegate.owner=nullptr;[state_->view removeFromSuperview];auto c=configuration?(__bridge WKWebViewConfiguration*)configuration:[WKWebViewConfiguration new];
    if(!configuration){if(tab_&&tab_->privateMode()){if(!privateStore)privateStore=[WKWebsiteDataStore nonPersistentDataStore];c.websiteDataStore=privateStore;}else c.websiteDataStore=[WKWebsiteDataStore defaultDataStore];}
    c.allowsInlineMediaPlayback=YES;c.mediaTypesRequiringUserActionForPlayback=WKAudiovisualMediaTypeNone;
    if(browser)c.preferences.javaScriptCanOpenWindowsAutomatically=browser->options()["allowSitePopups"].toBool();
    state_->applied.clear();state_->committed=QUrl{};
    state_->view=[[WKWebView alloc] initWithFrame:CGRectZero configuration:c];state_->delegate=[BrowserDelegate new];state_->delegate.owner=this;state_->view.navigationDelegate=state_->delegate;state_->view.UIDelegate=state_->delegate;
    configureRules();sync();
}
void IOSView::configureRules(){
    if(!state_->view||!browser)return;auto o=browser->options();
    state_->view.configuration.preferences.javaScriptCanOpenWindowsAutomatically=o["allowSitePopups"].toBool();
    if(!state_->applied.isEmpty()&&o["adBlock"]==state_->applied["adBlock"]&&o["adHosts"]==state_->applied["adHosts"]&&o["adExceptions"]==state_->applied["adExceptions"])return;
    state_->applied=o;auto r=web::Rules::make(o["adBlock"].toBool(),o["adHosts"].toString().toStdString(),o["adExceptions"].toString().toStdString());
    int generation=++state_->generation;auto controller=state_->view.configuration.userContentController;[controller removeAllContentRuleLists];if(!r.enabled)return;
    QJsonArray rules,unless;for(auto& d:r.exceptions)unless.append("*"+QString::fromStdString(d));
    auto append=[&](const std::string& d,const QString& suffix){
        auto domain=QString::fromStdString(d);domain.replace(".","\\.");
        QJsonObject trigger{{"url-filter","^https?://([^/:]*\\.)?"+domain+suffix},{"load-type",QJsonArray{"third-party"}}};if(!unless.isEmpty())trigger["unless-domain"]=unless;
        rules.append(QJsonObject{{"trigger",trigger},{"action",QJsonObject{{"type","block"}}}});
    };
    for(auto& d:r.blocked)append(d,"[/:]");
    for(const auto& [domain,prefixes]:r.paths)for(const auto& prefix:prefixes){
        QString escaped;for(const auto c:QString::fromStdString(prefix)){
            if(QStringLiteral(".+()[]{}^$|?*\\").contains(c))escaped+='\\';
            escaped+=c;
        }
        append(domain,"(:[0-9]+)?"+escaped);
    }
    auto json=QJsonDocument(rules).toJson(QJsonDocument::Compact);auto hash=QCryptographicHash::hash(json,QCryptographicHash::Sha256).toHex();
    NSString* identifier=ns("browser-"+QString::fromLatin1(hash));QPointer<IOSView> guard(this);
    [[WKContentRuleListStore defaultStore] compileContentRuleListForIdentifier:identifier encodedContentRuleList:ns(QString::fromUtf8(json)) completionHandler:^(WKContentRuleList* list,NSError* error){if(guard&&guard->state_->generation==generation&&list)[controller addContentRuleList:list];}];
}
void* IOSView::native()const{return (__bridge void*)state_->view;}
void IOSView::setShown(bool v){if(v==shown_)return;shown_=v;sync();if(v)timer_.start();else timer_.stop();emit changed();}
void IOSView::geometryChange(const QRectF& a,const QRectF& b){QQuickItem::geometryChange(a,b);sync();}
void IOSView::sync(){
    auto v=state_->view;if(!v)return;v.hidden=!shown_||!isVisible()||!window();
    if(!window())return;
    auto root=(__bridge UIView*)reinterpret_cast<void*>(window()->winId());if(!root)return;
    if(v.superview!=root)[root addSubview:v];
    auto p=mapToScene(QPointF(0,0));v.frame=CGRectMake(p.x(),p.y(),width(),height());
    if(!v.hidden)[root bringSubviewToFront:v];
}
void IOSView::load(QUrl u){if(!state_->view)return;NSURL* url=[NSURL URLWithString:ns(u.toString(QUrl::FullyEncoded))];if(url)[state_->view loadRequest:[NSURLRequest requestWithURL:url]];error_.clear();emit changed();}
bool IOSView::canGoBack()const{return state_->view.canGoBack;}
bool IOSView::canGoForward()const{return state_->view.canGoForward;}
bool IOSView::loading()const{return state_->view.loading;}
int IOSView::loadProgress()const{return int(state_->view.estimatedProgress*100);}
void IOSView::goBack(){[state_->view goBack];}
void IOSView::goForward(){[state_->view goForward];}
void IOSView::reload(){[state_->view reload];}
void IOSView::stop(){[state_->view stopLoading];}
void IOSView::searchPage(QString text,bool backwards){
    if(!state_->view)return;
    auto generation=++findGeneration_;
    if(text.isEmpty()){findResult_.clear();emit changed();}
    WKFindConfiguration* config=[WKFindConfiguration new];config.backwards=backwards;config.wraps=YES;config.caseSensitive=NO;
    QPointer<IOSView> guard(this);
    [state_->view findString:ns(text) withConfiguration:config completionHandler:^(WKFindResult* result){
        if(!guard||guard->findGeneration_!=generation||text.isEmpty())return;
        guard->findResult_=result.matchFound?QStringLiteral("Match found"):QStringLiteral("No matches");emit guard->changed();
    }];
}
void IOSView::changeZoom(int delta){
    if(!state_->view)return;
    CGFloat zoom=state_->view.pageZoom;
    state_->view.pageZoom=delta==0?1.0:qBound<CGFloat>(0.25,zoom*(delta>0?1.1:1.0/1.1),5.0);
}
void IOSView::clearWebsiteData(bool cookies){
    // Only the regular profile is affected, never an unrelated private session.
    WKWebsiteDataStore* store=[WKWebsiteDataStore defaultDataStore];
    NSSet* types=cookies?[NSSet setWithObject:WKWebsiteDataTypeCookies]:[NSSet setWithObjects:WKWebsiteDataTypeDiskCache,WKWebsiteDataTypeMemoryCache,nil];
    [store removeDataOfTypes:types modifiedSince:[NSDate distantPast] completionHandler:^{
        if(browser)browser->notify(cookies?QStringLiteral("Saved cookies cleared."):QStringLiteral("HTTP cache cleared."));
    }];
}
void IOSView::report(bool committed,QString e){
    bool changedState=e!=error_||state_->progress!=loadProgress()||state_->loading!=loading()||state_->back!=canGoBack()||state_->forward!=canGoForward();
    error_=std::move(e);state_->progress=loadProgress();state_->loading=loading();state_->back=canGoBack();state_->forward=canGoForward();
    if(tab_){QUrl u(qs(state_->view.URL.absoluteString));auto title=qs(state_->view.title);if(committed){tab_->commit(u,title);state_->committed=u;}else if(!loading()&&error_.isEmpty()&&!state_->committed.isEmpty()&&u!=state_->committed){tab_->sameDocument(u,title);state_->committed=u;}else tab_->report(u,title);}if(changedState||committed)emit changed();
}
