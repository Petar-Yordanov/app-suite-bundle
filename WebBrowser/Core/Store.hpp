#pragma once
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QSqlDatabase>
#include <QJsonObject>

class Store {
public:
    explicit Store(QString directory);
    ~Store();
    bool valid()const{return db_.isOpen();}
    QString error()const{return error_;}
    bool visit(const QUrl&,QString title);
    bool updateTitle(const QUrl&,QString title);
    QVariantList history(QString search={},int limit=1000)const;
    bool clearHistory();
    bool deleteHistory(qint64 id);
    QVariantList bookmarks(QString search={})const;
    QVariantList folders()const;
    qint64 addFolder(QString name,qint64 parent=0);
    qint64 addBookmark(QString title,const QUrl&,qint64 parent=0);
    bool edit(qint64 id,QString title,qint64 parent);
    bool remove(qint64 id);
    bool importBookmarks(QString file);
    bool exportBookmarks(QString file)const;
    QJsonObject loadSession()const;
    bool saveSession(const QJsonObject&)const;
    static bool webUrl(const QUrl&);
    static QUrl address(QString input,QString searchTemplate);
private:
    QString dir_,connection_,error_;
    QSqlDatabase db_;
    qint64 add(QString title,QUrl url,qint64 parent,bool folder);
    bool validParent(qint64 parent,qint64 child=0)const;
};
