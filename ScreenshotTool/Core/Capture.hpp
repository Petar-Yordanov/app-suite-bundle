#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <QUrl>
#include <QList>
#include "Geometry.hpp"

namespace shot {
struct Target {
    quint64 id=0;
    QString name;
    QRect logical;  // Qt screen coordinates; empty for standalone windows
    QRect native;   // physical pixels for Windows/X11
    bool window=false;
};
struct Result { QImage image; QString error; };
bool validSize(QSize size);
QRect pixelRegion(QRectF selection,QSizeF logicalSize,QSize pixelSize);
QImage crop(const QImage& image,QRectF selection,QSizeF logicalSize);
bool savePng(const QImage& image,const QUrl& destination,QString& error);
}
