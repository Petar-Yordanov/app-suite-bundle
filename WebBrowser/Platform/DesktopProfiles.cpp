#include "DesktopProfiles.hpp"
#include "../App/Browser.hpp"
#include "../App/Downloads.hpp"
#include <QWebEngineUrlRequestInfo>
#include <QTimer>
void Filter::interceptRequest(QWebEngineUrlRequestInfo& info){
    auto r=std::atomic_load(&rules);if(r&&r->blocks(info.requestUrl().host().toStdString(),info.firstPartyUrl().host().toStdString(),info.resourceType()==QWebEngineUrlRequestInfo::ResourceTypeMainFrame))info.block(true);
}
DesktopProfiles::DesktopProfiles(Browser* b,Downloads* d,QObject* p):QObject(p),browser_(b),normal_(new QQuickWebEngineProfile("Default",this)){
    normal_->setPersistentStoragePath(b->dataPath()+"/profile");
    normal_->setCachePath(b->dataPath()+"/cache");
    normal_->setHttpCacheMaximumSize(128*1024*1024);
    normal_->setUrlRequestInterceptor(&filter_);
    normal_->setPersistentPermissionsPolicy(QQuickWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);
    configure();
    connect(b,&Browser::optionsChanged,this,&DesktopProfiles::configure);
    connect(b,&Browser::privateStarted,this,[this]{private_=new QQuickWebEngineProfile(this);private_->setUrlRequestInterceptor(&filter_);private_->setPersistentPermissionsPolicy(QQuickWebEngineProfile::PersistentPermissionsPolicy::AskEveryTime);emit privateChanged();});
    connect(b,&Browser::privateEnded,this,[this,d]{d->clearPrivate();auto old=private_;private_=nullptr;emit privateChanged();if(old)QTimer::singleShot(500,old,&QObject::deleteLater);});
}
DesktopProfiles::~DesktopProfiles(){for(auto p:findChildren<QQuickWebEngineProfile*>(QString(),Qt::FindDirectChildrenOnly))delete p;}
void DesktopProfiles::configure(){
    auto o=browser_->options();
    if(applied_.isEmpty()||o["adBlock"]!=applied_["adBlock"]||o["adHosts"]!=applied_["adHosts"]||o["adExceptions"]!=applied_["adExceptions"])
        std::atomic_store(&filter_.rules,std::make_shared<const web::Rules>(web::Rules::make(o["adBlock"].toBool(),o["adHosts"].toString().toStdString(),o["adExceptions"].toString().toStdString())));
    if(applied_.isEmpty()||o["restoreSessionCookies"]!=applied_["restoreSessionCookies"])
        normal_->setPersistentCookiesPolicy(o["restoreSessionCookies"].toBool()?QQuickWebEngineProfile::ForcePersistentCookies:QQuickWebEngineProfile::OnlyPersistentCookies);
    applied_=o;
}
