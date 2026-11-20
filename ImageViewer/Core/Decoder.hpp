#pragma once
#include <QImage>
#include <QColorSpace>
#include <QImageReader>
#include <QStringList>
#include <QVariantMap>
#include <memory>

namespace imageview {
constexpr qint64 MiB=1024*1024;
constexpr qint64 MaxDisplayPixels=32*1024*1024;
constexpr int MaxDisplayEdge=8192;
constexpr qint64 MaxFullPixels=100*1024*1024;
constexpr qint64 CacheBytes=128*MiB;
struct Animation {
    QString path;
    QSize scaled;
    std::unique_ptr<QImageReader> reader;
    int repeats=0;
    int delay=100;
    int orientation=1;
    QColorSpace colorSpace;
};
struct Decoded {
    QImage image;
    QSize original;
    QString format,error;
    QVariantMap metadata;
    std::shared_ptr<Animation> animation;
    bool reduced=false;
};
struct Frame { QImage image; int delay=0; QString error; bool ended=false; };
QStringList extensions();
QStringList neighbors(const QString& path);
QString fingerprint(const QString& path);
QSize boundedSize(QSize size);
Decoded decode(const QString& path, bool allowAnimation=true);
Frame nextFrame(Animation& animation);
}
