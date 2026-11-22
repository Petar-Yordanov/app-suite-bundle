#include "Store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>
#include <QSet>
#include <QVariantMap>
#include <QHash>
#include <functional>

Store::Store(QString directory):dir_(std::move(directory)),connection_("browser-"+QUuid::createUuid().toString()){
    QDir().mkpath(dir_);
    db_=QSqlDatabase::addDatabase("QSQLITE",connection_);db_.setDatabaseName(dir_+"/browser.sqlite");
    db_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=1000");if(!db_.open()){error_=db_.lastError().text();return;}
    QSqlQuery q(db_);q.exec("PRAGMA journal_mode=WAL");q.exec("PRAGMA synchronous=NORMAL");q.exec("PRAGMA foreign_keys=ON");
    for(auto sql:{"CREATE TABLE IF NOT EXISTS history(id INTEGER PRIMARY KEY, url TEXT UNIQUE NOT NULL,title TEXT NOT NULL,visited INTEGER NOT NULL,visits INTEGER NOT NULL DEFAULT 1)",
        "CREATE INDEX IF NOT EXISTS history_visited ON history(visited DESC)",
        "CREATE TABLE IF NOT EXISTS bookmarks(id INTEGER PRIMARY KEY,parent INTEGER NOT NULL DEFAULT 0,title TEXT NOT NULL,url TEXT NOT NULL DEFAULT '',folder INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS bookmarks_parent ON bookmarks(parent)"}){
        if(!q.exec(sql)){error_=q.lastError().text();db_.close();return;}
    }
}
Store::~Store(){db_.close();db_=QSqlDatabase();QSqlDatabase::removeDatabase(connection_);}
bool Store::webUrl(const QUrl& u){return u.isValid()&&(u.scheme()=="https"||u.scheme()=="http")&&!u.host().isEmpty()&&u.userInfo().isEmpty()&&u.toEncoded().size()<=16384;}
QUrl Store::address(QString input,QString search){
    input=input.trimmed();if(input.isEmpty())return QUrl("about:blank");
    if(input=="about:blank")return QUrl(input);
    auto candidate=QUrl(input,QUrl::StrictMode);
    if(webUrl(candidate))return candidate;
    // Only HTTP(S), local loopback and explicit blank; no arbitrary custom schemes.
    static const QRegularExpression explicitScheme("^[A-Za-z][A-Za-z0-9+.-]*:");
    static const QRegularExpression hostPort("^(?:localhost|[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)+):[0-9]+(?:[/?#]|$)",QRegularExpression::CaseInsensitiveOption);
    if(explicitScheme.match(input).hasMatch()&&!hostPort.match(input).hasMatch())return {};
    if(!input.contains(QRegularExpression("\\s"))&&!input.contains("://")&&
       (input.contains('.')||input.startsWith("localhost")||input.startsWith('['))){
        bool local=input.startsWith("localhost")||input.startsWith("127.")||input.startsWith("[::1]");
        candidate=QUrl((local?"http://":"https://")+input,QUrl::StrictMode);
        if(webUrl(candidate))return candidate;
    }
    if(!search.contains("%s")||!webUrl(QUrl(QString(search).replace("%s","test"))))search="https://www.google.com/search?q=%s";
    candidate=QUrl(search.replace("%s",QString::fromLatin1(QUrl::toPercentEncoding(input))));return webUrl(candidate)?candidate:QUrl{};
}
bool Store::visit(const QUrl& url,QString title){
    if(!valid()||!webUrl(url))return false;
    if(title.isEmpty())title=url.host();
    QUrl u=url;u.setFragment({});
    QSqlQuery q(db_);q.prepare("INSERT INTO history(url,title,visited,visits) VALUES(?,?,?,1) ON CONFLICT(url) DO UPDATE SET title=excluded.title, visited=excluded.visited, visits=visits+1");
    q.addBindValue(u.toString(QUrl::FullyEncoded));q.addBindValue(title.left(1024));q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if(!q.exec())return false;
    QSqlQuery prune(db_);prune.exec("DELETE FROM history WHERE id IN (SELECT id FROM history ORDER BY visited DESC,id DESC LIMIT -1 OFFSET 20000)");return true;
}
bool Store::updateTitle(const QUrl& url,QString title){
    if(!valid()||!webUrl(url)||title.isEmpty())return false;auto u=url;u.setFragment({});
    QSqlQuery q(db_);q.prepare("UPDATE history SET title=? WHERE url=? AND title<>?");
    q.addBindValue(title.left(1024));q.addBindValue(u.toString(QUrl::FullyEncoded));q.addBindValue(title.left(1024));return q.exec()&&q.numRowsAffected()>0;
}
QVariantList Store::history(QString search,int limit)const{
    QVariantList out;if(!valid())return out;
    if(search.isNull())search=QStringLiteral("");
    QSqlQuery q(db_);q.prepare("SELECT id,url,title,visited,visits FROM history WHERE instr(lower(url),lower(?))>0 OR instr(lower(title),lower(?))>0 ORDER BY visited DESC,id DESC LIMIT ?");
    q.addBindValue(search);q.addBindValue(search);q.addBindValue(qBound(1,limit,20000));if(q.exec())while(q.next())out.append(QVariantMap{{"id",q.value(0)},{"url",q.value(1)},{"title",q.value(2)},{"time",QDateTime::fromMSecsSinceEpoch(q.value(3).toLongLong()).toString(Qt::ISODate)},{"visits",q.value(4)}});return out;
}
bool Store::clearHistory(){QSqlQuery q(db_);return valid()&&q.exec("DELETE FROM history");}
bool Store::deleteHistory(qint64 id){QSqlQuery q(db_);q.prepare("DELETE FROM history WHERE id=?");q.addBindValue(id);return valid()&&q.exec();}
QVariantList Store::bookmarks(QString search)const{
    QVariantList result;if(!valid())return result;
    if(search.isNull())search=QStringLiteral("");
    QSqlQuery q(db_);q.prepare("WITH RECURSIVE tree(id,parent,title,url,folder,path,depth,sortpath) AS (SELECT id,parent,title,url,folder,title,0,printf('%010d',id) FROM bookmarks WHERE parent=0 UNION ALL SELECT b.id,b.parent,b.title,b.url,b.folder,t.path||' / '||b.title,t.depth+1,t.sortpath||'/'||printf('%010d',b.id) FROM bookmarks b JOIN tree t ON b.parent=t.id WHERE t.depth<64) SELECT id,parent,title,url,folder,path,depth FROM tree WHERE instr(lower(path),lower(?))>0 OR instr(lower(url),lower(?))>0 ORDER BY sortpath LIMIT 20000");
    q.addBindValue(search);q.addBindValue(search);if(q.exec())while(q.next())result.append(QVariantMap{{"id",q.value(0)},{"parent",q.value(1)},{"title",q.value(2)},{"url",q.value(3)},{"folder",q.value(4).toBool()},{"path",q.value(5)},{"depth",q.value(6)}});return result;
}
QVariantList Store::folders()const{
    QVariantList out{QVariantMap{{"id",0},{"path","Bookmarks"}}};
    for(auto& x:bookmarks())if(x.toMap().value("folder").toBool())out.append(x);
    return out;
}
bool Store::validParent(qint64 parent,qint64 child)const{
    if(parent==0)return true;
    QSet<qint64> seen;
    QSqlQuery q(db_);
    int depth=0;
    for(;depth<64&&parent!=0;++depth){
        if(parent==child||seen.contains(parent))return false;seen.insert(parent);
        q.prepare("SELECT parent,folder FROM bookmarks WHERE id=?");q.addBindValue(parent);if(!q.exec()||!q.next()||!q.value(1).toBool())return false;
        parent=q.value(0).toLongLong();
    }
    if(parent!=0)return false;
    if(child>0){
        q.prepare("WITH RECURSIVE descendants(id,depth) AS (SELECT id,0 FROM bookmarks WHERE id=? UNION ALL SELECT b.id,d.depth+1 FROM bookmarks b JOIN descendants d ON b.parent=d.id WHERE d.depth<65) SELECT max(depth) FROM descendants");
        q.addBindValue(child);if(!q.exec()||!q.next()||depth+q.value(0).toInt()>64)return false;
    }
    return true;
}
qint64 Store::add(QString title,QUrl url,qint64 parent,bool folder){
    if(!valid()||!validParent(parent)||(!folder&&!webUrl(url)))return -1;title=title.trimmed().left(1024);
    if(title.isEmpty())title=folder?"Folder":url.host();
    QSqlQuery count(db_);if(!count.exec("SELECT count(*) FROM bookmarks")||!count.next()||count.value(0).toInt()>=20000)return -1;
    QSqlQuery q(db_);q.prepare("INSERT INTO bookmarks(parent,title,url,folder) VALUES(?,?,?,?)");
    q.addBindValue(parent);q.addBindValue(title);q.addBindValue(folder?QString(""):url.toString(QUrl::FullyEncoded));q.addBindValue(folder?1:0);
    return q.exec()?q.lastInsertId().toLongLong():-1;
}
qint64 Store::addFolder(QString name,qint64 parent){return add(std::move(name),{},parent,true);}
qint64 Store::addBookmark(QString title,const QUrl& u,qint64 parent){return add(std::move(title),u,parent,false);}
bool Store::edit(qint64 id,QString title,qint64 parent){
    title=title.trimmed().left(1024);if(!valid()||title.isEmpty()||id<=0||!validParent(parent,id))return false;
    QSqlQuery q(db_);q.prepare("UPDATE bookmarks SET title=?,parent=? WHERE id=?");q.addBindValue(title);q.addBindValue(parent);q.addBindValue(id);return q.exec()&&q.numRowsAffected()>0;
}
bool Store::remove(qint64 id){
    if(!valid()||id<=0)return false;QSqlQuery q(db_);q.prepare("WITH RECURSIVE descendants(id) AS (SELECT id FROM bookmarks WHERE id=? UNION SELECT b.id FROM bookmarks b JOIN descendants d ON b.parent=d.id) DELETE FROM bookmarks WHERE id IN (SELECT id FROM descendants)");q.addBindValue(id);return q.exec();
}
static QString unescape(QString s){
    s.replace("&quot;","\"").replace("&#39;","'").replace("&lt;","<").replace("&gt;",">").replace("&amp;","&");return s;
}
bool Store::importBookmarks(QString path){
    QFile f(path);if(!valid()||!f.open(QIODevice::ReadOnly)||f.size()>10*1024*1024)return false;
    auto raw=f.readAll();auto doc=QJsonDocument::fromJson(raw);if(!db_.transaction())return false;
    bool ok=true;int count=0;
    if(doc.isObject()&&doc.object().value("format").toString()=="appsuite-bookmarks-1"){
        std::function<void(QJsonArray,qint64,int)> walk=[&](QJsonArray arr,qint64 parent,int depth){
            if(depth>64){ok=false;return;}for(auto v:arr){if(!v.isObject()||++count>20000){ok=false;return;}auto o=v.toObject();auto name=o.value("title").toString();qint64 id;
                if(o.value("folder").toBool()){id=addFolder(name,parent);if(id>0)walk(o.value("children").toArray(),id,depth+1);}
                else id=addBookmark(name,QUrl(o.value("url").toString()),parent);
                if(id<0){ok=false;return;}if(!ok)return;
            }
        };walk(doc.object().value("bookmarks").toArray(),0,0);
    }else{
        // Netscape bookmark HTML: consume folder headers, DL nesting and anchors.
        const auto html=QString::fromUtf8(raw);
        if(!html.contains("NETSCAPE-Bookmark-file",Qt::CaseInsensitive)&&!html.contains("<DL",Qt::CaseInsensitive)){db_.rollback();return false;}
        QRegularExpression re(R"rx(<H3\b[^>]*>([^<]*)</H3\s*>|<A\b[^>]*\bHREF\s*=\s*["']([^"']*)["'][^>]*>([^<]*)</A\s*>|<DL\b[^>]*>|</DL\s*>)rx",QRegularExpression::CaseInsensitiveOption);
        QList<qint64> stack{0};qint64 pending=-1;auto it=re.globalMatch(html);
        while(it.hasNext()&&ok){auto m=it.next();auto token=m.captured(0);
            if(!m.captured(1).isNull()){pending=addFolder(unescape(m.captured(1)),stack.last());if(pending<0)ok=false;++count;}
            else if(!m.captured(2).isNull()){QUrl u(unescape(m.captured(2)));if(webUrl(u)){if(addBookmark(unescape(m.captured(3)),u,stack.last())<0)ok=false;++count;}}
            else if(token.startsWith("</",Qt::CaseInsensitive)){if(stack.size()>1)stack.removeLast();}
            else {stack.append(pending>0?pending:stack.last());pending=-1;if(stack.size()>65)ok=false;}
            if(count>20000)ok=false;
        }
    }
    if(!ok||!count){db_.rollback();return false;}return db_.commit();
}
bool Store::exportBookmarks(QString path)const{
    auto all=bookmarks();QHash<qint64,QVariantList> byParent;for(auto v:all)byParent[v.toMap().value("parent").toLongLong()].append(v);
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;
    if(path.endsWith(".json",Qt::CaseInsensitive)){
        std::function<QJsonArray(qint64)> walk=[&](qint64 p){QJsonArray out;for(auto v:byParent.value(p)){auto m=v.toMap();QJsonObject o{{"title",m["title"].toString()},{"folder",m["folder"].toBool()}};if(m["folder"].toBool())o["children"]=walk(m["id"].toLongLong());else o["url"]=m["url"].toString();out.append(o);}return out;};
        auto data=QJsonDocument(QJsonObject{{"format","appsuite-bookmarks-1"},{"bookmarks",walk(0)}}).toJson();if(f.write(data)!=data.size())return false;
    }else{
        QByteArray html="<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n";
        std::function<void(qint64)> walk=[&](qint64 p){for(auto v:byParent.value(p)){auto m=v.toMap();auto title=m["title"].toString().toHtmlEscaped().toUtf8();if(m["folder"].toBool()){html+="<DT><H3>"+title+"</H3>\n<DL><p>\n";walk(m["id"].toLongLong());html+="</DL><p>\n";}else html+="<DT><A HREF=\""+m["url"].toString().toHtmlEscaped().toUtf8()+"\">"+title+"</A>\n";}};walk(0);html+="</DL><p>\n";if(f.write(html)!=html.size())return false;
    }return f.commit();
}
QJsonObject Store::loadSession()const{QFile f(dir_+"/session.json");if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024)return {};return QJsonDocument::fromJson(f.readAll()).object();}
bool Store::saveSession(const QJsonObject& o)const{QSaveFile f(dir_+"/session.json");if(!f.open(QIODevice::WriteOnly))return false;auto data=QJsonDocument(o).toJson(QJsonDocument::Compact);return f.write(data)==data.size()&&f.commit();}
