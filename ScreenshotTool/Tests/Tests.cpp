#include "../App/Controller.hpp"
#include <QApplication>
#include <QClipboard>
#include <QTemporaryDir>
#include <QImageReader>
#include <QQuickStyle>
#include <QFileInfo>
#include <QDir>
#include <iostream>

int main(int argc,char** argv){
    shot::native::initialize();QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QQuickStyle::setStyle("Basic");
    QTemporaryDir dir;int failures=0;
    auto require=[&](bool b,const char* msg){if(!b){std::cerr<<"FAIL: "<<msg<<"\n";++failures;}};
    require(dir.isValid(),"temporary directory");
    QImage src(600,400,QImage::Format_RGB32);src.fill(Qt::red);src.setPixelColor(200,100,Qt::blue);
    auto image=shot::crop(src,QRectF(100,50,100,50),QSizeF(300,200));
    require(image.size()==QSize(200,100),"2x DPI dimensions");
    require(image.pixelColor(0,0)==QColor(Qt::blue),"pixel coordinates");
    require(shot::crop(src,QRectF(400,400,10,10),QSizeF(300,200)).isNull(),"out of bounds");
    Controller c(dir.path(),true);
    require(c.testView("Settings",{}),"settings QML");
    require(c.testView("Overlay",src),"selection QML");
    require(c.testView("Preview",image),"preview QML");
    c.copy();require(QApplication::clipboard()->image().size()==image.size(),"clipboard image");
    require(!QFileInfo::exists(dir.filePath("shot.png")),"not saved automatically");
    QString err;require(shot::savePng(image,QUrl::fromLocalFile(dir.filePath("shot")),err),"explicit PNG save");
    require(QImageReader(dir.filePath("shot.png")).read().convertToFormat(QImage::Format_RGB32)==image,"PNG roundtrip");
    require(!shot::savePng(image,QUrl("https://example.invalid/test.png"),err),"nonlocal save rejected");
    c.cancel();require(c.imageWidth()==0,"cancel releases image");
    if(qEnvironmentVariableIsSet("SCREENSHOT_NATIVE_TEST")){
        auto screens=shot::native::screens();require(!screens.isEmpty(),"native screen enumeration");
        if(!screens.isEmpty()){auto r=shot::native::capture(screens.first(),false);require(!r.image.isNull(),"native screen capture");require(r.image.size()==screens.first().native.size(),"native dimensions");}
    }
    std::cout<<(failures?"Tests failed":"Screenshot integration tests passed")<<"\n";return failures?1:0;
}
