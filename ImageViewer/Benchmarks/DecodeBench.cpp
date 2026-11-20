#include "../Core/Decoder.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QImageReader>
#include <iostream>
#include <algorithm>
#include <vector>
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QImageReader::setAllocationLimit(512);QTemporaryDir tmp;
    QStringList files=app.arguments().mid(1);
    if(files.isEmpty()){
        QImage im(3840,2160,QImage::Format_RGB32);
        for(int y=0;y<im.height();++y){auto* p=reinterpret_cast<QRgb*>(im.scanLine(y));for(int x=0;x<im.width();++x)p[x]=qRgb(x%256,y%256,(x+y)%256);}
        for(auto* ext:{"png","jpg","webp"}){auto file=tmp.filePath(QString("sample.")+ext);if(im.save(file))files<<file;}
    }
    std::cout<<"format\twidth\theight\tmedian_ms\tp95_ms\tdecoded_bytes\n";
    for(const auto& f:files){std::vector<double> measurements;imageview::Decoded result;
        for(int i=0;i<7;++i){QElapsedTimer t;t.start();result=imageview::decode(f,false);if(result.image.isNull()){std::cerr<<result.error.toStdString()<<'\n';return 1;}measurements.push_back(double(t.nsecsElapsed())/1e6);}
        std::sort(measurements.begin(),measurements.end());std::cout<<result.format.toStdString()<<'\t'<<result.original.width()<<'\t'<<result.original.height()<<'\t'<<measurements[measurements.size()/2]<<'\t'<<measurements.back()<<'\t'<<result.image.sizeInBytes()<<'\n';
    }
}
