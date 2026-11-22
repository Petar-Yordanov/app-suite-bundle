#include "Portal.hpp"
#include "PortalRequest.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QUuid>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QMetaObject>
#pragma push_macro("signals")
#pragma push_macro("slots")
#undef signals
#undef slots
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#pragma pop_macro("slots")
#pragma pop_macro("signals")
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

namespace {
const QString service="org.freedesktop.portal.Desktop",desktop="/org/freedesktop/portal/desktop";
const QString cast="org.freedesktop.portal.ScreenCast",sessionIface="org.freedesktop.portal.Session";
QVariant object(const QString& path){return QVariant::fromValue(QDBusObjectPath(path));}
}
struct PortalStream {uint node=0;QVariantMap props;};
Q_DECLARE_METATYPE(PortalStream)
QDBusArgument& operator<<(QDBusArgument& arg,const PortalStream& value){arg.beginStructure();arg<<value.node<<value.props;arg.endStructure();return arg;}
const QDBusArgument& operator>>(const QDBusArgument& arg,PortalStream& value){arg.beginStructure();arg>>value.node>>value.props;arg.endStructure();return arg;}

PortalSession::PortalSession(){qDBusRegisterMetaType<PortalStream>();qDBusRegisterMetaType<QList<PortalStream>>();}
PortalSession::~PortalSession(){
    if(request_)request_->cancel();
    if(worker_.joinable()){worker_.request_stop();worker_.join();}
    if(!session_.isEmpty()){
        auto bus=QDBusConnection::sessionBus();
        bus.disconnect(service,session_,sessionIface,"Closed",this,SLOT(sessionClosed()));
        bus.asyncCall(QDBusMessage::createMethodCall(service,session_,sessionIface,"Close"));
    }
}
void PortalSession::choose(bool window,bool cursor){
    if(!QDBusConnection::sessionBus().isConnected()){emit failed("No desktop portal session bus.");return;}
    request_=new PortalRequest(this,cast,"CreateSession",{},
         {{"session_handle_token","rec"+QUuid::createUuid().toString(QUuid::Id128)}},
         [this,window,cursor](uint code,QVariantMap values){
        request_=nullptr;
        if(code){emit failed(values.value("error","ScreenCast portal unavailable or cancelled.").toString());return;}
        auto handle=values.value("session_handle");
        session_=handle.canConvert<QDBusObjectPath>()?handle.value<QDBusObjectPath>().path():handle.toString();
        if(session_.isEmpty()){emit failed("Portal did not return a session.");return;}
        QDBusConnection::sessionBus().connect(service,session_,sessionIface,"Closed",this,SLOT(sessionClosed()));
        select(window,cursor);
    });
}
void PortalSession::select(bool window,bool cursor){
    request_=new PortalRequest(this,cast,"SelectSources",{object(session_)},
       {{"types",uint(window?2:1)},{"multiple",false},{"cursor_mode",uint(cursor?2:1)}},
       [this](uint code,QVariantMap values){
        request_=nullptr;
        if(code){emit failed(values.value("error","The compositor denied the requested source or cursor mode.").toString());return;}
        launch();
    });
}
void PortalSession::launch(){
    request_=new PortalRequest(this,cast,"Start",{object(session_),QString()},{},
        [this](uint code,QVariantMap values){
        request_=nullptr;
        if(code){emit failed(values.value("error","Screen recording was cancelled.").toString());return;}
        const auto streams=qdbus_cast<QList<PortalStream>>(values.value("streams"));
        if(streams.size()!=1){emit failed("The portal must grant exactly one source.");return;}
        remote(streams.first().node);
    });
}
void PortalSession::remote(uint node){
    auto msg=QDBusMessage::createMethodCall(service,desktop,cast,"OpenPipeWireRemote");
    msg.setArguments({object(session_),QVariantMap{}});
    auto* watch=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg),this);
    connect(watch,&QDBusPendingCallWatcher::finished,this,[this,node](QDBusPendingCallWatcher* w){
        QDBusPendingReply<QDBusUnixFileDescriptor> reply=*w;w->deleteLater();
        if(reply.isError()||!reply.value().isValid()){emit failed("The portal did not provide a PipeWire connection.");return;}
        // The portal restricts this connection to the selected source.
        const int fd=dup(reply.value().fileDescriptor());
        if(fd<0){emit failed("Cannot duplicate PipeWire connection.");return;}
        worker_=std::jthread([this,fd,node](std::stop_token stop){consume(stop,fd,node);});
    });
}
void PortalSession::record(std::shared_ptr<rec::Mailbox> mailbox,QRect region,rec::Size output){
    QMutexLocker lock(&mutex_);mailbox_=std::move(mailbox);region_=region;output_=output;
}
void PortalSession::pause(){QMutexLocker lock(&mutex_);mailbox_.reset();}
void PortalSession::sessionClosed(){notifyFailure("The compositor ended the screen capture session.");}
void PortalSession::notifyFailure(QString message){
    QMutexLocker lock(&mutex_);
    if(mailbox_)mailbox_->fail(message);
    QMetaObject::invokeMethod(this,[this,message=std::move(message)]{emit failed(message);},Qt::QueuedConnection);
}
void PortalSession::consume(std::stop_token stop,int fd,uint node){
    struct stat original{};
    const bool known=fstat(fd,&original)==0;
    // Preserve the descriptor while the PipeWire plugin is alive; avoid closing
    // a recycled descriptor if a plugin variant consumed the original.
    struct CloseFd {
        int fd;struct stat original;bool known;
        ~CloseFd(){struct stat current{};if(known&&fstat(fd,&current)==0&&
            current.st_dev==original.st_dev&&current.st_ino==original.st_ino)::close(fd);}
    } fdGuard{fd,original,known};
    GError* error=nullptr;
    if(!gst_init_check(nullptr,nullptr,&error)){
        if(error)g_error_free(error);notifyFailure("GStreamer initialization failed.");return;
    }
    // Only the permission-granted descriptor and node are passed to the source.
    auto description=QString("pipewiresrc fd=%1 path=%2 do-timestamp=true keepalive-time=250 ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream ! videoconvert ! video/x-raw,format=BGRA ! appsink name=frames max-buffers=1 drop=true sync=false")
        .arg(fd).arg(node).toUtf8();
    auto* pipeline=gst_parse_launch(description.constData(),&error);
    if(error||!pipeline){
        QString why=error?QString::fromUtf8(error->message):QString("PipeWire pipeline unavailable.");
        if(error)g_error_free(error);if(pipeline)gst_object_unref(pipeline);
        notifyFailure(why);return;
    }
    auto* sink=gst_bin_get_by_name(GST_BIN(pipeline),"frames");
    auto* bus=gst_element_get_bus(pipeline);
    if(!sink||gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE){
        if(sink)gst_object_unref(sink);gst_object_unref(bus);gst_object_unref(pipeline);
        notifyFailure("Unable to start the PipeWire stream.");return;
    }
    bool delivered=false;QElapsedTimer startup;startup.start();
    while(!stop.stop_requested()){
        if(auto* message=gst_bus_pop_filtered(bus,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS))){
            if(!stop.stop_requested())notifyFailure("The PipeWire source stopped or failed.");
            gst_message_unref(message);break;
        }
        auto* sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),100*GST_MSECOND);
        if(!sample){
            if(!delivered&&startup.elapsed()>15000){notifyFailure("Timed out waiting for the first PipeWire frame.");break;}
            continue;
        }
        GstVideoInfo info{};GstVideoFrame frame{};
        if(gst_video_info_from_caps(&info,gst_sample_get_caps(sample))&&
            rec::validFrame({int(info.width),int(info.height)})&&
            gst_video_frame_map(&frame,&info,gst_sample_get_buffer(sample),GST_MAP_READ)){
            auto* data=static_cast<const uchar*>(GST_VIDEO_FRAME_PLANE_DATA(&frame,0));
            const auto stride=GST_VIDEO_FRAME_PLANE_STRIDE(&frame,0);
            if(data&&stride>=int(info.width)*4){
                QImage image(data,int(info.width),int(info.height),stride,QImage::Format_RGB32);
                if(!delivered){
                    delivered=true;auto first=image.copy();
                    QMetaObject::invokeMethod(this,[this,first=std::move(first)]{preview_=first;emit ready();},Qt::QueuedConnection);
                }
                std::shared_ptr<rec::Mailbox> target;QRect region;rec::Size out;
                {QMutexLocker lock(&mutex_);target=mailbox_;region=region_;out=output_;}
                if(target&&rec::validFrame(out)){
                    if(!region.isEmpty()){
                        if(!image.rect().contains(region))target->fail("The selected region no longer fits the source.");
                        else image=image.copy(region);
                    }
                    if(image.size()!=QSize(out.w,out.h))image=image.scaled(out.w,out.h,Qt::IgnoreAspectRatio,Qt::FastTransformation);
                    if(!image.isNull()){
                        QByteArray data(qsizetype(out.w)*out.h*4,Qt::Uninitialized);
                        for(int y=0;y<out.h;++y)std::memcpy(data.data()+qsizetype(y)*out.w*4,image.constScanLine(y),size_t(out.w)*4);
                        target->video(std::move(data));
                    }
                }
            }
            gst_video_frame_unmap(&frame);
        }else if(info.width&&info.height&&!rec::validFrame({int(info.width),int(info.height)})){
            notifyFailure("The PipeWire source is larger than the bounded frame limit.");gst_sample_unref(sample);break;
        }
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline,GST_STATE_NULL);
    gst_object_unref(sink);gst_object_unref(bus);gst_object_unref(pipeline);
}
