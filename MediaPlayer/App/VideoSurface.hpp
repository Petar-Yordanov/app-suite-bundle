#pragma once
#include "../Core/VlcApi.hpp"
#include <QWidget>
#include <QImage>
#include <QByteArray>
#include <QMutex>
#include <atomic>

// The native surface is separate from Qt Quick's scene graph; VLC owns its
// video rendering pipeline. The callback path is only for unsupported QPA
// backends (e.g. native Wayland) or explicitly requested diagnostic mode.
class VideoSurface : public QWidget {
public:
    explicit VideoSurface(bool forceSoftware=false,QWidget* parent=nullptr);
    bool software() const{return software_;}
    void attach(VlcApi&,libvlc_media_player_t*);
    void reset();
    void setAspect(QString ratio);
    quint64 frames() const{return frames_.load(std::memory_order_relaxed);}
protected:
    void paintEvent(QPaintEvent*) override;
private:
    static unsigned setup(void**,char*,unsigned*,unsigned*,unsigned*,unsigned*);
    static void cleanup(void*);
    static void* lock(void*,void**);
    static void unlock(void*,void*,void* const*);
    static void display(void*,void*);
    bool software_;
    QMutex mutex_;
    QByteArray storage_;
    uchar* pixels_=nullptr;
    unsigned width_=0,height_=0,pitch_=0;
    QImage frame_;
    QString aspect_;
    std::atomic_bool pending_{false};
    std::atomic<quint64> frames_{0};
};
