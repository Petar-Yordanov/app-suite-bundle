#pragma once
#include <QObject>
#include <QVariantMap>
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <atomic>
#include <memory>
#include "../Core/Rules.hpp"
class Browser;
class Downloads;
class Filter : public QWebEngineUrlRequestInterceptor {
public:
    std::shared_ptr<const web::Rules> rules;
    void interceptRequest(QWebEngineUrlRequestInfo&)override;
};
class DesktopProfiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickWebEngineProfile* normal READ normal CONSTANT)
    Q_PROPERTY(QQuickWebEngineProfile* privateProfile READ privateProfile NOTIFY privateChanged)
public:
    DesktopProfiles(Browser*,Downloads*,QObject* p=nullptr);
    ~DesktopProfiles() override;
    QQuickWebEngineProfile* normal()const{return normal_;}
    QQuickWebEngineProfile* privateProfile()const{return private_;}
signals:
    void privateChanged();
private:
    void configure();
    Browser* browser_;
    Filter filter_;
    QQuickWebEngineProfile* normal_;
    QQuickWebEngineProfile* private_=nullptr;
    QVariantMap applied_;
};
