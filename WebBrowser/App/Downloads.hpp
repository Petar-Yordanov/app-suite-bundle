#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QUrl>
#include <functional>

class Downloads : public QAbstractListModel {
    Q_OBJECT
public:
    explicit Downloads(QString directory={},QObject* p=nullptr);
    ~Downloads() override;
    int rowCount(const QModelIndex& p={})const override{return p.isValid()?0:rows_.size();}
    QVariant data(const QModelIndex&,int)const override;
    QHash<int,QByteArray> roleNames()const override{return {{Qt::UserRole+1,"entry"}};}
    QString add(QString name,QString path,bool privateMode);
    void update(QString id,QVariantMap fields);
    void handler(QString id,std::function<void(QString)>);
    Q_INVOKABLE void acceptDesktop(QObject*,bool,QString directory={});
    Q_INVOKABLE void action(QString id,QString action);
    Q_INVOKABLE void clearFinished();
    void clearPrivate();
signals:
    void retryRequested(QUrl url);
private:
    struct Row {QVariantMap fields;std::function<void(QString)> act;QPointer<QObject> object;bool desktop=false;};
    using QAbstractListModel::index;
    int index(QString)const;
    void poll();
    void save()const;
    QString historyPath_;
    QList<Row> rows_;
    QTimer timer_;
};
