#pragma once
#include <QObject>
#include <QVariantMap>
#include <functional>

class PortalRequest : public QObject {
    Q_OBJECT
public:
    using Callback=std::function<void(uint,QVariantMap)>;
    PortalRequest(QObject* parent,QString iface,QString method,QVariantList args,QVariantMap options,Callback done);
    ~PortalRequest() override;
    void cancel();
private slots:
    void response(uint code,const QVariantMap& values);
private:
    QString path_;
    Callback done_;
    void disconnectBus();
    void listen(QString path);
    void finish(uint code,QVariantMap values);
};
