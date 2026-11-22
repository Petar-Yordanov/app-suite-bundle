#include "VideoSurface.hpp"
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QMutexLocker>
#include <QMetaObject>
#include <cstring>
#include <cstdint>

VideoSurface::VideoSurface(bool forceSoftware,QWidget* parent):QWidget(parent){
    const auto qpa=QGuiApplication::platformName();
    software_=forceSoftware||!(qpa=="windows"||qpa=="cocoa"||qpa=="xcb");
    setMinimumSize(160,90);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);setAttribute(Qt::WA_OpaquePaintEvent);
    if(!software_){setAttribute(Qt::WA_DontCreateNativeAncestors);setAttribute(Qt::WA_NativeWindow);}
}
void VideoSurface::attach(VlcApi& api,libvlc_media_player_t* mp){
    if(software_){api.libvlc_video_set_callbacks(mp,&lock,&unlock,&display,this);api.libvlc_video_set_format_callbacks(mp,&setup,&cleanup);}
    else{
#ifdef Q_OS_WIN
        api.libvlc_media_player_set_hwnd(mp,reinterpret_cast<void*>(winId()));
#elif defined(Q_OS_MACOS)
        api.libvlc_media_player_set_nsobject(mp,reinterpret_cast<void*>(winId()));
#else
        api.libvlc_media_player_set_xwindow(mp,static_cast<uint32_t>(winId()));
#endif
    }
    api.libvlc_video_set_key_input(mp,0);api.libvlc_video_set_mouse_input(mp,0);
}
void VideoSurface::reset(){
    {QMutexLocker l(&mutex_);frame_={};}
    frames_=0;update();
}
void VideoSurface::setAspect(QString ratio){aspect_=std::move(ratio);update();}
unsigned VideoSurface::setup(void** opaque,char* chroma,unsigned* w,unsigned* h,unsigned* pitches,unsigned* lines){
    auto* self=static_cast<VideoSurface*>(*opaque);
    if(!*w||!*h||*w>8192||*h>8192||quint64(*w)* *h>64ull*1024*1024)return 0;
    QMutexLocker l(&self->mutex_);
    auto pitch=(*w*4+31u)&~31u,rows=(*h+31u)&~31u;
    try{self->storage_.resize(qsizetype(pitch)*rows+31);}catch(...){return 0;}
    auto raw=reinterpret_cast<std::uintptr_t>(self->storage_.data());
    self->pixels_=reinterpret_cast<uchar*>((raw+31u)&~std::uintptr_t(31u));
    self->width_=*w;self->height_=*h;self->pitch_=pitch;*pitches=pitch;*lines=rows;
    std::memcpy(chroma,"RV32",4);return 1;
}
void VideoSurface::cleanup(void* opaque){
    auto* self=static_cast<VideoSurface*>(opaque);QMutexLocker l(&self->mutex_);
    self->pixels_=nullptr;self->storage_.clear();self->frame_={};
}
void* VideoSurface::lock(void* opaque,void** planes){
    auto* self=static_cast<VideoSurface*>(opaque);self->mutex_.lock();*planes=self->pixels_;return nullptr;
}
void VideoSurface::unlock(void* opaque,void*,void* const*){static_cast<VideoSurface*>(opaque)->mutex_.unlock();}
void VideoSurface::display(void* opaque,void*){
    auto* self=static_cast<VideoSurface*>(opaque);self->frames_.fetch_add(1,std::memory_order_relaxed);
    if(self->pending_.exchange(true))return;
    {
        QMutexLocker l(&self->mutex_);
        if(self->pixels_)self->frame_=QImage(self->pixels_,int(self->width_),int(self->height_),qsizetype(self->pitch_),QImage::Format_RGB32).copy();
    }
    QMetaObject::invokeMethod(self,[self]{self->pending_=false;self->update();},Qt::QueuedConnection);
}
void VideoSurface::paintEvent(QPaintEvent*){
    QPainter p(this);p.fillRect(rect(),Qt::black);
    if(!software_)return;
    QImage img;{QMutexLocker l(&mutex_);img=frame_;}
    if(img.isNull())return;
    QSize aspect=img.size();const auto parts=aspect_.split(':');
    if(parts.size()==2&&parts[0].toInt()>0&&parts[1].toInt()>0)aspect=QSize(parts[0].toInt(),parts[1].toInt());
    auto size=aspect.scaled(this->size(),Qt::KeepAspectRatio);
    // QSize::scaled would not enlarge the small ratio pair for a custom ratio
    // unless the target is larger; it implements a fit in both directions.
    QRect dest(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
    p.setRenderHint(QPainter::SmoothPixmapTransform,true);p.drawImage(dest,img);
}
