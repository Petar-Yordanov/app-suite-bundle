#pragma once
#include <QImage>
#include <QColorSpace>
#include <QImageReader>
#include <QRect>
#include <QStringList>
#include <QVariantMap>
#include <memory>
#include <functional>

namespace imageview {
constexpr qint64 MiB=1024*1024;
constexpr qint64 MaxDisplayPixels=32*1024*1024;
constexpr int MaxDisplayEdge=8192;
constexpr qint64 MaxFullPixels=100*1024*1024;
constexpr qint64 CacheBytes=128*MiB;
constexpr int MaxDetailEdge=4096;
constexpr qint64 MaxDetailPixels=12*1024*1024;
struct Frame { QImage image; int delay=0; QString error; bool ended=false; };
struct Animation {
    QString path;
    QSize scaled;
    std::unique_ptr<QImageReader> reader;
    int repeats=0;
    int delay=100;
    int orientation=1;
    QColorSpace colorSpace;
    std::function<Frame()> nativeNext;
};
struct Decoded {
    QImage image;
    QSize original;
    QString format,error;
    QVariantMap metadata;
    std::shared_ptr<Animation> animation;
    bool reduced=false;
    bool detailSupported=false;
};
QStringList extensions();
QStringList neighbors(const QString& path);
QString fingerprint(const QString& path);
QSize boundedSize(QSize size);
Decoded decode(const QString& path, bool allowAnimation=true);
QImage decodeDetail(const QString& path, const QRect& region);
Frame nextFrame(Animation& animation);
}
