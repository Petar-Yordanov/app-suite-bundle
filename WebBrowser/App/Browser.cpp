#include "Browser.hpp"
#include <QUuid>
#include <QJsonObject>
#include <QCoreApplication>

Tab::Tab(Browser* b,QUrl u,QString t,bool p):QObject(b),browser_(b),id_(QUuid::createUuid().toString()),title_(std::move(t)),url_(std::move(u)),private_(p){}
void Tab::activate(){if(!loaded_){loaded_=true;emit changed();}}
void Tab::report(QUrl u,QString t){
    if(!Store::webUrl(u)&&u!=QUrl("about:blank"))return;
    t=t.left(1024);if(u==url_&&t==title_)return;url_=u;title_=t;
    if(u==committedUrl_&&!t.isEmpty())browser_->updateTitle(this,u,t);
    emit changed();
}
void Tab::commit(QUrl u,QString t){if(!Store::webUrl(u)&&u!=QUrl("about:blank"))return;report(u,t);committedUrl_=u;browser_->committed(this,u,t);emit navigationCommitted();}
void Tab::sameDocument(QUrl u,QString t){if(!committedUrl_.isEmpty()&&u!=committedUrl_&&Store::webUrl(u))commit(u,t);}
void Tab::navigate(QUrl u){if(Store::webUrl(u)||u==QUrl("about:blank")){activate();report(u,{});emit loadRequested(u);}}
Browser::Browser(QString dir,QObject* p):QAbstractListModel(p),directory_(std::move(dir)),settings_(directory_+"/settings.ini",QSettings::IniFormat),store_(directory_){
    options_={{"theme","System"},{"search","https://www.google.com/search?q=%s"},{"restoreTabs",true},{"restoreSelection",true},{"restoreClosed",true},{"restoreSessionCookies",false},{"recordHistory",true},{"adBlock",true},{"adHosts",""},{"adExceptions",""},{"updateFeed",""},{"newTabKey","Ctrl+T"},{"privateKey","Ctrl+Shift+N"},{"closeKey","Ctrl+W"},{"reopenKey","Ctrl+Shift+T"},{"addressKey","Ctrl+L"},{"nextKey","Ctrl+Tab"},{"previousKey","Ctrl+Shift+Tab"},{"reloadKey","Ctrl+R"},{"backKey","Alt+Left"},{"forwardKey","Alt+Right"},{"bookmarkKey","Ctrl+D"},{"historyKey","Ctrl+H"},{"downloadsKey","Ctrl+J"},{"fullscreenKey","F11"}};
    options_.insert("allowSitePopups",false);
    for(auto it=options_.begin();it!=options_.end();++it){auto v=settings_.value(it.key(),it.value());if(it.value().typeId()==QMetaType::Bool)v=v.toBool();it.value()=v;}
    saveTimer_.setSingleShot(true);saveTimer_.setInterval(500);connect(&saveTimer_,&QTimer::timeout,this,&Browser::save);
    auto session=store_.loadSession();
    if(options_["restoreTabs"].toBool())for(auto value:session["tabs"].toArray()){
        if(count()>=128)break;auto o=value.toObject();QUrl u(o["url"].toString());if(Store::webUrl(u)||u==QUrl("about:blank"))insert(u,o["title"].toString(),false,false);
    }
    if(options_["restoreClosed"].toBool())closed_=session["closed"].toArray();
    while(closed_.size()>30)closed_.removeFirst();
    if(tabs_.isEmpty())newTab();else select(tabs_.at(options_["restoreSelection"].toBool()?qBound(0,session["selected"].toInt(),count()-1):0)->id());
    if(!store_.valid())notify(store_.error());
    connect(qApp,&QCoreApplication::aboutToQuit,this,&Browser::save);
}
Browser::~Browser(){save();}
QVariant Browser::data(const QModelIndex& i,int role)const{return i.isValid()&&i.row()>=0&&i.row()<count()&&role==Qt::UserRole+1?QVariant::fromValue(tabs_[i.row()]):QVariant{};}
int Browser::privateCount()const{int n=0;for(auto t:tabs_)if(t->privateMode())++n;return n;}
int Browser::index(QString id)const{for(int i=0;i<count();++i)if(tabs_[i]->id()==id)return i;return -1;}
Tab* Browser::insert(QUrl u,QString title,bool p,bool selected){
    if(count()>=128){notify("Tab limit reached (128)");return nullptr;}
    if(p&&!privateCount())emit privateStarted();
    auto t=new Tab(this,u,title,p);connect(t,&Tab::changed,this,[this,t]{if(!t->privateMode())schedule();});
    beginInsertRows({},count(),count());tabs_.append(t);endInsertRows();emit countChanged();
    if(selected)select(t->id());if(!p)schedule();return t;
}
Tab* Browser::newTab(QUrl u,bool p){if(!Store::webUrl(u)&&u!=QUrl("about:blank"))u=QUrl("about:blank");return insert(u,{},p,true);}
void Browser::select(QString id){int i=index(id);if(i<0)return;current_=tabs_[i];current_->activate();emit currentChanged();if(!current_->privateMode()){lastRegular_=current_;schedule();}}
void Browser::closeTab(QString id){
    int i=index(id);if(i<0)return;auto t=tabs_[i];bool p=t->privateMode();
    if(!p){closed_.append(QJsonObject{{"url",t->url().toString()},{"title",t->title()}});while(closed_.size()>30)closed_.removeFirst();}
    bool selected=current_==t;if(selected)current_=nullptr;if(lastRegular_==t)lastRegular_=nullptr;
    beginRemoveRows({},i,i);tabs_.removeAt(i);endRemoveRows();t->deleteLater();emit countChanged();
    if(p&&!privateCount())emit privateEnded();
    if(tabs_.isEmpty())newTab();else if(selected)select(tabs_.at(qMin(i,count()-1))->id());
    if(!p)schedule();
}
void Browser::moveTab(QString id,int delta){int i=index(id),j=qBound(0,i+delta,count()-1);if(i<0||i==j)return;if(beginMoveRows({},i,i,{},j>i?j+1:j)){tabs_.move(i,j);endMoveRows();schedule();}}
void Browser::cycle(int d){if(count()&&current_)select(tabs_[(index(current_->id())+d%count()+count())%count()]->id());}
void Browser::reopen(){if(closed_.isEmpty())return;auto o=closed_.last().toObject();if(auto t=newTab(QUrl(o["url"].toString()),false)){Q_UNUSED(t);closed_.removeLast();schedule();}}
void Browser::go(QString a){if(current_){auto u=Store::address(a,options_["search"].toString());if(u.isEmpty())notify("Only HTTP and HTTPS addresses are supported");else current_->navigate(u);}}
void Browser::schedule(){saveTimer_.start();}
void Browser::save(){
    QJsonArray a;int selected=0;
    for(auto t:tabs_)if(!t->privateMode()){if(options_["restoreSelection"].toBool()&&t==lastRegular_)selected=a.size();a.append(QJsonObject{{"url",t->url().toString()},{"title",t->title()}});}
    store_.saveSession({{"tabs",options_["restoreTabs"].toBool()?a:QJsonArray{}},{"selected",selected},{"closed",options_["restoreClosed"].toBool()?closed_:QJsonArray{}}});
}
void Browser::setOption(QString key,QVariant v){
    if(!options_.contains(key))return;auto old=options_.value(key);
    if(old.typeId()==QMetaType::Bool)v=v.toBool();else {auto s=v.toString();if(s.size()>1024*1024)return;if(key=="theme"&&!QStringList{"System","Light","Dark"}.contains(s))return;if(key=="search"&&(!s.contains("%s")||!Store::webUrl(QUrl(QString(s).replace("%s","test"))))){notify("Search template requires an HTTP(S) URL and %s");return;}v=s;}
    if(old==v)return;options_[key]=v;settings_.setValue(key,v);emit optionsChanged();schedule();
}
void Browser::committed(Tab* t,QUrl u,QString title){if(!t->privateMode()&&options_["recordHistory"].toBool()&&store_.visit(u,title))emit historyChanged();}
void Browser::updateTitle(Tab* t,QUrl u,QString title){if(!t->privateMode()&&options_["recordHistory"].toBool()&&store_.updateTitle(u,title))emit historyChanged();}
void Browser::notify(QString m){message_=std::move(m);emit messageChanged();}
void Browser::addBookmark(QString t,QUrl u,qint64 p){if(store_.addBookmark(t,u,p)>0)emit bookmarksChanged();else notify("Cannot add bookmark");}
void Browser::addFolder(QString t,qint64 p){if(store_.addFolder(t,p)>0)emit bookmarksChanged();else notify("Cannot add folder");}
void Browser::editBookmark(qint64 i,QString t,qint64 p){if(store_.edit(i,t,p))emit bookmarksChanged();else notify("Cannot move or rename bookmark");}
void Browser::removeBookmark(qint64 i){if(store_.remove(i))emit bookmarksChanged();}
void Browser::removeHistory(qint64 i){if(store_.deleteHistory(i))emit historyChanged();}
void Browser::clearHistory(){if(store_.clearHistory())emit historyChanged();}
void Browser::importBookmarks(QUrl f){if(f.isLocalFile()&&store_.importBookmarks(f.toLocalFile()))emit bookmarksChanged();else notify("Bookmark import failed");}
void Browser::exportBookmarks(QUrl f){notify(f.isLocalFile()&&store_.exportBookmarks(f.toLocalFile())?"Bookmarks exported":"Bookmark export failed");}
