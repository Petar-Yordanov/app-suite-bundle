#include "PortalRequest.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QUuid>
#include <QTimer>
#include <QDBusObjectPath>

namespace {
const QString service="org.freedesktop.portal.Desktop",desktop="/org/freedesktop/portal/desktop";
const QString requestInterface="org.freedesktop.portal.Request";
QString token(){return "rec"+QUuid::createUuid().toString(QUuid::Id128);}
void closeObject(const QString& path,const QString& interface){
    if(!path.isEmpty())QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(service,path,interface,"Close"));
}
}
PortalRequest::PortalRequest(QObject* parent,QString iface,QString method,QVariantList args,QVariantMap options,Callback done):QObject(parent),done_(std::move(done)){
        auto bus=QDBusConnection::sessionBus();
        QString sender=bus.baseService();sender.remove(0,1);sender.replace('.','_');
        const auto id=token();options["handle_token"]=id;
        listen(desktop+"/request/"+sender+"/"+id);
        auto msg=QDBusMessage::createMethodCall(service,desktop,iface,method);args.append(options);msg.setArguments(args);
        auto* watcher=new QDBusPendingCallWatcher(bus.asyncCall(msg),this);
        connect(watcher,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher* w){
            QDBusPendingReply<QDBusObjectPath> reply=*w;w->deleteLater();
            if(!done_)return;
            if(reply.isError()){finish(2,{{"error",reply.error().message()}});return;}
            if(reply.value().path()!=path_)listen(reply.value().path());
        });
        QTimer::singleShot(180000,this,[this]{if(done_){closeObject(path_,requestInterface);finish(2,{{"error","Portal request timed out."}});}});
}
PortalRequest::~PortalRequest(){if(done_)closeObject(path_,requestInterface);disconnectBus();}
void PortalRequest::cancel(){closeObject(path_,requestInterface);done_={};disconnectBus();deleteLater();}
void PortalRequest::response(uint code,const QVariantMap& values){finish(code,values);}
void PortalRequest::disconnectBus(){if(!path_.isEmpty())QDBusConnection::sessionBus().disconnect(service,path_,requestInterface,"Response",this,SLOT(response(uint,QVariantMap)));}
void PortalRequest::listen(QString path){disconnectBus();path_=std::move(path);QDBusConnection::sessionBus().connect(service,path_,requestInterface,"Response",this,SLOT(response(uint,QVariantMap)));}
void PortalRequest::finish(uint code,QVariantMap values){if(!done_)return;disconnectBus();auto cb=std::move(done_);done_={};cb(code,std::move(values));deleteLater();}
