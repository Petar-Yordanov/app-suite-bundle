#include "Capture.hpp"
#include <QImageWriter>
#include <QSaveFile>
#include <QFileInfo>
#include <QtMath>
#include <cmath>
#include <algorithm>

namespace shot {
bool validSize(QSize size){return validPixels(size.width(),size.height());}
QRect pixelRegion(QRectF selection,QSizeF logical,QSize pixels){
    auto r=mapRegion(selection.x(),selection.y(),selection.width(),selection.height(),logical.width(),logical.height(),pixels.width(),pixels.height());
    return {r.x,r.y,r.width,r.height};
}
QImage crop(const QImage& image,QRectF selection,QSizeF logical){
    auto r=pixelRegion(selection,logical,image.size());if(r.isEmpty())return {};
    QImage out=r==image.rect()?image:image.copy(r);out.setDevicePixelRatio(1);return out;
}
bool savePng(const QImage& image,const QUrl& destination,QString& error){
    if(image.isNull()||!destination.isLocalFile()){error="Select a local PNG file.";return false;}
    auto path=destination.toLocalFile();if(!path.endsWith(".png",Qt::CaseInsensitive))path+=".png";
    QSaveFile out(path);if(!out.open(QIODevice::WriteOnly)){error=out.errorString();return false;}
    QImageWriter writer(&out,"png");if(!writer.write(image)){error=writer.errorString();out.cancelWriting();return false;}
    if(!out.commit()){error=out.errorString();return false;}return true;
}
}
