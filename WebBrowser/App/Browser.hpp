#pragma once
#include "../Core/Store.hpp"
#include <QAbstractListModel>
#include <QSettings>
#include <QTimer>
#include <QJsonArray>
#include <QPointer>

class Browser;
class Tab : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QUrl url READ url NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool privateMode READ privateMode CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
public:
    Tab(Browser*,QUrl,QString,bool);
    QString id()const{return id_;}
    QUrl url()const{return url_;}
    QString title()const{return title_;}
    bool privateMode()const{return private_;}
    bool loaded()const{return loaded_;}
    void activate();
    Q_INVOKABLE void report(QUrl,QString);
    Q_INVOKABLE void commit(QUrl,QString);
    Q_INVOKABLE void sameDocument(QUrl,QString);
    Q_INVOKABLE void navigate(QUrl);
signals:
    void changed();
    void loadRequested(QUrl url);
    void navigationCommitted();
private:
    Browser* browser_;
    QString id_,title_;
    QUrl url_,committedUrl_;
    bool private_,loaded_=false;
};
class Browser : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(Tab* current READ current NOTIFY currentChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int privateCount READ privateCount NOTIFY countChanged)
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    explicit Browser(QString directory,QObject* parent=nullptr);
    ~Browser() override;
    int rowCount(const QModelIndex& parent={})const override{return parent.isValid()?0:tabs_.size();}
    using QAbstractListModel::index;
    QVariant data(const QModelIndex&,int role)const override;
    QHash<int,QByteArray> roleNames()const override{return {{Qt::UserRole+1,"browserTab"}};}
    int count()const{return tabs_.size();}
    int privateCount()const;
    Tab* current()const{return current_;}
    QVariantMap options()const{return options_;}
    QString dataPath()const{return directory_;}
    QString message()const{return message_;}
    Q_INVOKABLE Tab* newTab(QUrl url=QUrl("about:blank"),bool privateMode=false);
    Q_INVOKABLE void select(QString id);
    Q_INVOKABLE void closeTab(QString id);
    Q_INVOKABLE void moveTab(QString id,int delta);
    Q_INVOKABLE void cycle(int delta);
    Q_INVOKABLE void reopen();
    Q_INVOKABLE void go(QString address);
    Q_INVOKABLE void setOption(QString key,QVariant value);
    Q_INVOKABLE QVariantList history(QString search={})const{return store_.history(search,2000);}
    Q_INVOKABLE QVariantList bookmarks(QString search={})const{return store_.bookmarks(search);}
    Q_INVOKABLE QVariantList folders()const{return store_.folders();}
    Q_INVOKABLE void addBookmark(QString title,QUrl url,qint64 parent);
    Q_INVOKABLE void addFolder(QString title,qint64 parent);
    Q_INVOKABLE void editBookmark(qint64 id,QString title,qint64 parent);
    Q_INVOKABLE void removeBookmark(qint64 id);
    Q_INVOKABLE void removeHistory(qint64 id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void importBookmarks(QUrl file);
    Q_INVOKABLE void exportBookmarks(QUrl file);
    void committed(Tab*,QUrl,QString);
    void updateTitle(Tab*,QUrl,QString);
    void save();
    void notify(QString);
signals:
    void currentChanged();
    void countChanged();
    void optionsChanged();
    void historyChanged();
    void bookmarksChanged();
    void messageChanged();
    void privateStarted();
    void privateEnded();
private:
    Tab* insert(QUrl,QString,bool,bool);
    int index(QString)const;
    void schedule();
    QString directory_,message_;
    QSettings settings_;
    Store store_;
    QVariantMap options_;
    QList<Tab*> tabs_;
    Tab* current_=nullptr;
    QPointer<Tab> lastRegular_;
    QJsonArray closed_;
    QTimer saveTimer_;
};
