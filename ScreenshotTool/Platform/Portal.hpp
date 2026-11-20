#pragma once
#include "../Core/Capture.hpp"
#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QDBusObjectPath>
#include <QTimer>
#include <functional>

class PortalRequest;
class Portal : public QObject {
    Q_OBJECT
public:
    explicit Portal(QObject* parent=nullptr);
    ~Portal() override;
    void screenshot(std::function<void(shot::Result)> callback);
    void cancel();
    void shortcuts(const QStringList&);
signals:
    void activated(int mode);
    void status(QString text);
private slots:
    void onActivated(const QDBusObjectPath&,const QString&,qulonglong,const QVariantMap&);
private:
    PortalRequest *capture_=nullptr,*registration_=nullptr;
    QString session_;
    void closeSession();
};
