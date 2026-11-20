#include "Portal.hpp"
#include "PortalRequest.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QKeySequence>

namespace {
const QString service="org.freedesktop.portal.Desktop",desktop="/org/freedesktop/portal/desktop";
const QString requestInterface="org.freedesktop.portal.Request",shortcutInterface="org.freedesktop.portal.GlobalShortcuts";
QString token(){return "shot"+QUuid::createUuid().toString(QUuid::Id128);}
void closeObject(const QString& path,const QString& interface){
    if(!path.isEmpty())QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(service,path,interface,"Close"));
}
}
struct PortalShortcut {QString id;QVariantMap options;};
Q_DECLARE_METATYPE(PortalShortcut)
QDBusArgument& operator<<(QDBusArgument& a,const PortalShortcut& s){a.beginStructure();a<<s.id<<s.options;a.endStructure();return a;}
const QDBusArgument& operator>>(const QDBusArgument& a,PortalShortcut& s){a.beginStructure();a>>s.id>>s.options;a.endStructure();return a;}

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

Portal::Portal(QObject* parent):QObject(parent){
    qDBusRegisterMetaType<PortalShortcut>();qDBusRegisterMetaType<QList<PortalShortcut>>();
    QDBusConnection::sessionBus().connect(service,desktop,shortcutInterface,"Activated",this,SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}
Portal::~Portal(){cancel();closeSession();}
void Portal::cancel(){if(capture_){capture_->cancel();capture_=nullptr;}}
void Portal::closeSession(){
    if(registration_){registration_->cancel();registration_=nullptr;}
    closeObject(session_,"org.freedesktop.portal.Session");session_.clear();
}
void Portal::screenshot(std::function<void(shot::Result)> cb){
    cancel();capture_=new PortalRequest(this,"org.freedesktop.portal.Screenshot","Screenshot",{QString() },{{"interactive",true}},[this,cb=std::move(cb)](uint code,QVariantMap values){
        capture_=nullptr;
        if(code){cb({{},code==1?"Capture cancelled.":values.value("error","Screenshot portal unavailable.").toString()});return;}
        QUrl uri(values.value("uri").toString());
        QFileInfo file(uri.toLocalFile());
        if(!uri.isLocalFile()||!file.isFile()||file.isSymLink()){cb({{},"Portal did not provide a local image."});return;}
        QImageReader reader(file.absoluteFilePath());QImage image;
        if(shot::validSize(reader.size()))image=reader.read();
        bool removed=QFile::remove(file.absoluteFilePath());
        cb({image,!removed?QString("Portal temporary image could not be removed: ")+file.absoluteFilePath():image.isNull()?QString("Cannot decode portal screenshot."):QString()});
    });
}
void Portal::shortcuts(const QStringList& keys){
    closeSession();
    registration_=new PortalRequest(this,shortcutInterface,"CreateSession",{},{{"session_handle_token",token()}},[this,keys](uint code,QVariantMap values){
        registration_=nullptr;
        if(code){emit status("GlobalShortcuts portal unavailable. Use compositor custom shortcuts.");return;}
        auto v=values.value("session_handle");session_=v.canConvert<QDBusObjectPath>()?v.value<QDBusObjectPath>().path():v.toString();
        if(session_.isEmpty()){emit status("Portal did not return a shortcut session.");return;}
        const QStringList ids={"region","screen","window"},names={"Capture region","Capture screen","Capture window"};
        QList<PortalShortcut> list;
        for(int i=0;i<3;++i){QString key=keys.value(i);key.replace("Ctrl","CTRL",Qt::CaseInsensitive).replace("Alt","ALT",Qt::CaseInsensitive).replace("Shift","SHIFT",Qt::CaseInsensitive).replace("Meta","LOGO",Qt::CaseInsensitive);list.append({ids[i],{{"description",names[i]},{"preferred_trigger",key}}});}
        registration_=new PortalRequest(this,shortcutInterface,"BindShortcuts",{QVariant::fromValue(QDBusObjectPath(session_)),QVariant::fromValue(list),QString()}, {},[this](uint result,QVariantMap){
            registration_=nullptr;emit status(result?"Global shortcut registration denied. Use compositor settings.":"Wayland shortcuts registered; actual bindings are controlled by the compositor.");
        });
    });
}
void Portal::onActivated(const QDBusObjectPath& session,const QString& id,qulonglong,const QVariantMap&){
    if(session.path()!=session_)return;
    if(id=="region")emit activated(0);else if(id=="screen")emit activated(1);else if(id=="window")emit activated(2);
}
