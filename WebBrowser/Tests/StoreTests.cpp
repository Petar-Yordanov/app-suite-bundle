#include "../App/Browser.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDebug>
#include <cstdlib>
void check(bool b,const char* m){if(!b){qCritical()<<"FAIL"<<m;std::exit(1);}}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);QTemporaryDir d;check(d.isValid(),"temporary data");
    auto url=QUrl("https://example.org/");
    {
        Store s(d.path()+"/store");check(s.valid(),"database");
        check(s.visit(url,"Example"),"history insert");check(s.visit(url,"Example 2"),"history update");
        auto h=s.history();check(h.size()==1&&h.first().toMap()["visits"].toInt()==2,"history deduplication");
        check(s.history("Example").size()==1&&s.history("missing").isEmpty(),"history search");
        auto a=s.addFolder("Work"),b=s.addFolder("Nested",a);check(a>0&&b>0,"nested folders");
        auto c=s.addBookmark("Example",url,b);check(c>0,"add bookmark");
        check(!s.edit(a,"Bad",b),"cycle prevention");check(s.edit(c,"Moved",a),"move");
        check(s.bookmarks().size()==3,"bookmark list");check(s.exportBookmarks(d.filePath("bookmarks.html")),"HTML export");
        check(s.exportBookmarks(d.filePath("bookmarks.json")),"JSON export");check(s.remove(a)&&s.bookmarks().isEmpty(),"recursive removal");
        check(s.importBookmarks(d.filePath("bookmarks.html"))&&s.bookmarks().size()==3,"HTML import");
        check(s.importBookmarks(d.filePath("bookmarks.json"))&&s.bookmarks().size()==6,"JSON import");
        check(Store::address("example.org","https://search.example/?q=%s")==url,"navigation");
        check(Store::address("https://example.org","https://search.example/?q=%s")==url,"explicit root navigation");
        check(Store::address("example.org/docs?x=1","https://search.example/?q=%s")==QUrl("https://example.org/docs?x=1"),"navigation preserves path and query");
        check(Store::address("hello world","https://search.example/?q=%s").host()=="search.example","search");
        check(!Store::webUrl(QUrl("file:///etc/passwd")),"unsafe scheme");
        check(Store::address("file:///private/example","https://search.example/?q=%s").isEmpty(),"do not send local URL to search");
        check(Store::address("javascript:alert(1)","https://search.example/?q=%s").isEmpty(),"reject scripts");
        check(Store::address("localhost:8080/test","https://search.example/?q=%s").port()==8080,"localhost with port");
        check(s.updateTitle(url,"Updated")&&s.history().first().toMap()["visits"].toInt()==2,"title change does not add visit");
    }
    {
        Browser browser(d.path()+"/browser");auto a=browser.current();
        a->commit(url,"Normal");auto b=browser.newTab(QUrl("https://second.example"),false);
        auto p=browser.newTab(QUrl("https://private.example"),true);p->commit(p->url(),"Private");
        check(browser.history().size()==1,"private history isolation");browser.closeTab(p->id());
        browser.moveTab(b->id(),-1);browser.closeTab(a->id());browser.reopen();check(browser.count()==2,"reopen");
        browser.save();
    }
    {Browser b(d.path()+"/browser");check(b.count()==2&&b.privateCount()==0,"session restore");check(b.history().size()==1,"persist history");check(b.current()->loaded(),"active restoration");check(!b.data(b.index(0,0),Qt::UserRole+1).value<Tab*>()->loaded()||!b.data(b.index(1,0),Qt::UserRole+1).value<Tab*>()->loaded(),"lazy tabs");b.setOption("restoreTabs",false);b.save();}
    {Browser b(d.path()+"/browser");check(b.count()==1,"disable restore");}
    {
        Browser b(d.path()+"/selection");b.current()->commit(url,"Initial");
        auto regular=b.newTab(QUrl("https://second.example"),false);regular->commit(regular->url(),"Second");
        regular->sameDocument(QUrl("https://second.example/new"),"SPA");
        check(b.history().size()==3,"same document navigation recorded");
        regular->report(regular->url(),"Changed SPA title");check(b.history("Changed SPA").size()==1,"late title updated");
        b.newTab(QUrl("https://private.example"),true);b.save();
    }
    {Browser b(d.path()+"/selection");check(b.count()==2&&b.current()->url()==QUrl("https://second.example/new"),"last regular selection retained while private active");b.setOption("restoreSelection",false);b.save();}
    {Browser b(d.path()+"/selection");check(b.count()==2&&b.current()->url()==url,"disable selection restore independently");b.setOption("recordHistory",false);b.current()->commit(QUrl("https://no-history.example"),"Disabled");check(b.history().size()==3,"disable history independently");}
    qInfo()<<"Store and session tests PASS";
}
