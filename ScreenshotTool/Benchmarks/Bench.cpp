#include "../Core/Capture.hpp"
#include "../Platform/Native.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <iostream>
int main(int argc,char** argv){
    QElapsedTimer elapsed;elapsed.start();shot::native::initialize();QApplication app(argc,argv);
    std::cout<<"Qt application startup ms: "<<elapsed.nsecsElapsed()/1e6<<"\n";
    QImage image(3840,2160,QImage::Format_RGB32);if(image.isNull())return 1;
    image.fill(Qt::blue);QImage result;
    elapsed.restart();for(int i=0;i<40;++i)result=shot::crop(image,QRectF(100,100,960,540),QSizeF(1920,1080));
    std::cout<<"1080p crop from 4K mean ms: "<<elapsed.nsecsElapsed()/40e6<<"\n";
    QTemporaryDir dir;QString error;elapsed.restart();
    if(!shot::savePng(result,QUrl::fromLocalFile(dir.filePath("bench.png")),error)){std::cerr<<error.toStdString()<<"\n";return 1;}
    std::cout<<"1080p PNG encode + atomic save ms: "<<elapsed.nsecsElapsed()/1e6<<"\n";
    if(app.arguments().contains("--capture")&&shot::native::requestPermission()){
        auto screens=shot::native::screens();if(!screens.isEmpty()){elapsed.restart();auto c=shot::native::capture(screens.first(),false);if(c.image.isNull()){std::cerr<<c.error.toStdString()<<"\n";return 1;}std::cout<<"Native monitor capture ms: "<<elapsed.nsecsElapsed()/1e6<<"\n";}
    }
    return 0;
}
