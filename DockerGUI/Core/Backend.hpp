#pragma once
#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QJsonArray>
#include <QStringList>
#include <functional>

struct Result {
    bool ok=false;
    QByteArray output;
    QString error;
};
using Done=std::function<void(Result)>;
using Stream=std::function<void(QString)>;
class Backend : public QObject {
public:
    using QObject::QObject;
    virtual QString executable() const=0;
    virtual QProcess* run(QStringList,Done,Stream={},QByteArray={},int=30000)=0;
    virtual void containers(Done)=0;
    virtual void resources(QString kind,Done)=0;
    virtual void setContext(QString,QString)=0;
    virtual QStringList arguments(QStringList) const=0;
};
class DockerBackend final : public Backend {
public:
    using Backend::Backend;
    QString executable() const override;
    QProcess* run(QStringList,Done,Stream={},QByteArray={},int=30000) override;
    void containers(Done) override;
    void resources(QString kind,Done) override;
    void setContext(QString context,QString endpoint) override {context_=context;endpoint_=endpoint;}
    QStringList arguments(QStringList) const override;
    static QProcess* process(QObject*,QString,QStringList,Done,Stream={},QByteArray={},int=30000);
    static QJsonArray jsonRows(const QByteArray&);
private:
    void localGet(QString target,QStringList fallback,Done,bool volumeObject=false);
    QString context_,endpoint_;
};
