#include "../App/Controller.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>

namespace {
int assertions=0;
void require(bool condition,const char* msg){++assertions;if(!condition)throw std::runtime_error(msg);}
void write(const QString& path,const QByteArray& bytes){QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"write fixture");}
template <class F> bool waitFor(F f){QElapsedTimer timer;timer.start();do{QCoreApplication::processEvents(QEventLoop::AllEvents,20);if(f())return true;QThread::msleep(5);}while(timer.elapsed()<15000);return false;}
QByteArray little16(std::initializer_list<unsigned> values){QByteArray b;for(auto n:values){b.append(char(n));b.append(char(n>>8));}return b;}
QByteArray little32(std::initializer_list<unsigned> values){QByteArray b;for(auto n:values)for(int s=0;s<32;s+=8)b.append(char(n>>s));return b;}
// A small real uncompressed Bayer DNG. No camera sample download or private photo is needed.
QByteArray makeDng(){
    struct Entry{unsigned tag,type,count;QByteArray bytes;};
    std::vector<Entry> tags={{256,4,1,little32({64})},{257,4,1,little32({64})},{258,3,1,little16({16})},{259,3,1,little16({1})},{262,3,1,little16({32803})},
        {271,2,6,QByteArray("TestX\0",6)},{272,2,8,QByteArray("Fixture\0",8)},{273,4,1,little32({0})},{274,3,1,little16({1})},{277,3,1,little16({1})},{278,4,1,little32({64})},{279,4,1,little32({8192})},{284,3,1,little16({1})},
        {33421,3,2,little16({2,2})},{33422,1,4,QByteArray::fromHex("00010102")},{50706,1,4,QByteArray::fromHex("01040000")},{50707,1,4,QByteArray::fromHex("01010000")},
        {50708,2,14,QByteArray("TestX Fixture\0",14)},{50714,3,1,little16({0})},{50717,4,1,little32({4095})},
        {50721,10,9,little32({1,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,1,1})},{50728,5,3,little32({1,1,1,1,1,1})},{50778,3,1,little16({21})}};
    unsigned start=8+2+12*unsigned(tags.size())+4,pixels=start;for(auto& t:tags)if(t.bytes.size()>4)pixels+=unsigned(t.bytes.size())+unsigned(t.bytes.size()%2);
    QByteArray b="II";b+=little16({42});b+=little32({8});b+=little16({unsigned(tags.size())});QByteArray extras;
    for(auto& t:tags){if(t.tag==273)t.bytes=little32({pixels});b+=little16({t.tag,t.type});b+=little32({t.count});
        if(t.bytes.size()<=4)b+=t.bytes+QByteArray(4-t.bytes.size(),'\0');else{b+=little32({start+unsigned(extras.size())});extras+=t.bytes;if(t.bytes.size()%2)extras.append('\0');}}
    b+=little32({0});b+=extras;for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)b+=little16({1500+(x+y)%128});return b;
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setApplicationName("ImageViewerTests");app.setApplicationVersion("1.0.0");QQuickStyle::setStyle("Basic");QImageReader::setAllocationLimit(512);
    try{
        QTemporaryDir temp;require(temp.isValid(),"temporary directory");auto file=[&](const QString& name){return temp.filePath(name);};
        QImage fixture(160,80,QImage::Format_RGB32);fixture.fill(Qt::red);
        for(auto* ext:{"png","jpg","bmp"}){
            const QString format=QString::fromLatin1(ext);
            auto path=file(QString("sample.")+format);require(fixture.save(path),qPrintable(QString("Cannot write %1 test fixture").arg(format)));
            auto result=imageview::decode(path);require(!result.image.isNull()&&result.original==fixture.size(),qPrintable(QString("Cannot decode %1: %2").arg(format,result.error)));
        }
        // Test read support independently of the SDK's optional write codecs.
        // The following fixed fixtures are genuine 4x3 RGB TIFF and WebP images.
        write(file("sample.tiff"),QByteArray::fromBase64("SUkqAAgAAAAKAAABBAABAAAABAAAAAEBBAABAAAAAwAAAAIBAwADAAAAhgAAAAMBAwABAAAAAQAAAAYBAwABAAAAAgAAABEBBAABAAAAjAAAABUBAwABAAAAAwAAABYBBAABAAAAAwAAABcBBAABAAAAJAAAABwBAwABAAAAAQAAAAAAAAAIAAgACAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAD/AAA="));
        auto tiff=imageview::decode(file("sample.tiff"));require(!tiff.image.isNull()&&tiff.original==QSize(4,3),qPrintable(QString("TIFF decoder unavailable (install Qt Image Formats): %1").arg(tiff.error)));
        write(file("sample.webp"),QByteArray::fromBase64("UklGRjwAAABXRUJQVlA4IDAAAADQAQCdASoEAAMAAUAmJaACdLoB+AADsAD+8ut//NgVzXPv9//S4P0uD9Lg/9KQAAA="));
        auto webpStill=imageview::decode(file("sample.webp"));require(!webpStill.image.isNull()&&webpStill.original==QSize(4,3),qPrintable(QString("WebP decoder unavailable (install Qt Image Formats): %1").arg(webpStill.error)));
        write(file("vector.svg"),"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" height=\"80\"><rect width=\"160\" height=\"80\" fill=\"red\"/></svg>");
        require(imageview::decode(file("vector.svg")).original==fixture.size(),"SVG dimensions");
        write(file("a.gif"),QByteArray::fromBase64("R0lGODlhAwACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAwACAAAIBgABCBwYEAAh+QQBCgABACwAAAAAAwACAIEAAP8AAAAAAAAAAAAIBgABCBwYEAA7"));
        auto gif=imageview::decode(file("a.gif"));require(!gif.image.isNull()&&bool(gif.animation),"GIF animation");auto frame=imageview::nextFrame(*gif.animation);require(!frame.ended&&!frame.image.isNull(),"GIF next frame");
        auto still=imageview::decode(file("a.gif"),false);require(!still.animation&&still.metadata.value("Animated source").toBool(),"animation cache guard");
        write(file("b.webp"),QByteArray::fromBase64("UklGRsQAAABXRUJQVlA4WAoAAAACAAAAAgAAAQAAQU5JTQYAAAAAAAAAAABBTk1GSgAAAAAAAAAAAAIAAAEAAGQAAAJWUDggMgAAADABAJ0BKgMAAgABQCYloAADcAD+8ut///mwP/bz/wR6Af//0uD//pcH//S4P/SkAAAAQU5NRkYAAAAAAAAAAAACAAABAABkAAAAVlA4IC4AAAA0AQCdASoDAAIAAAAmJaAAA3AA/vtV4///S4P/+lwf/9Lg/9Lg//rV5Vesq6AA"));
        auto webp=imageview::decode(file("b.webp"));require(!webp.image.isNull()&&bool(webp.animation),"WebP animation");require(!imageview::nextFrame(*webp.animation).image.isNull(),"WebP next frame");
        write(file("sample.avif"),QByteArray::fromBase64("AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUIAAADrbWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAAB5pbG9jAAAAAEQAAAEAAQAAAAEAAAETAAAAKgAAAChpaW5mAAAAAAABAAAAGmluZmUCAAAAAAEAAGF2MDFDb2xvcgAAAABqaXBycAAAAEtpcGNvAAAAFGlzcGUAAAAAAAAAAwAAAAIAAAAQcGl4aQAAAAADCAgIAAAADGF2MUOBAAwAAAAAE2NvbHJuY2x4AAEADQAGgAAAABdpcG1hAAAAAAAAAAEAAQQBAoMEAAAAMm1kYXQSAAoIGAQrRAQ0GhAyHBTHh4ZlAgggnlAAAABIWtlc1jIgMQsbXgqRN4A="));
        auto avif=imageview::decode(file("sample.avif"));require(!avif.image.isNull()&&avif.original==QSize(3,2),"AVIF decoder not available");
        write(file("sample.heic"),QByteArray::fromBase64("AAAAHGZ0eXBoZWljAAAAAGhlaWNtaWYxbWlhZgAAAVVtZXRhAAAAAAAAACFoZGxyAAAAAAAAAABwaWN0AAAAAAAAAAAAAAAAAAAAAA5waXRtAAAAAAABAAAAHmlsb2MAAAAARAAAAQABAAAAAQAAAXkAAAAaAAAAKGlpbmYAAAAAAAEAAAAaaW5mZQIAAAAAAQAAaHZjMUltYWdlAAAAANRpcHJwAAAAtWlwY28AAAAUaXNwZQAAAAAAAABAAAAAQAAAABBwaXhpAAAAAAMICAgAAAB2aHZjQwEBYAAAAJAAAAAAAB7wAPz9+PgAAA8DoAABABhAAQwB//8BYAAAAwCQAAADAAADAB6VmAmhAAEAKUIBAQFgAAADAJAAAAMAAAMAHqAggQWWVmkkyvAWgIAAAAMAgAAAAwCEogABAAdEAcFytCJAAAAAE2NvbHJuY2x4AAEADQAGAAAAABdpcG1hAAAAAAAAAAEAAQQBAoMEAAAAIm1kYXQAAAAWKAGvE4DlMlH/8kqP/sLzw9dw9ACF+A=="));
        auto heic=imageview::decode(file("sample.heic"));require(!heic.image.isNull()&&heic.original==QSize(64,64),"HEIC decoder not available");
        write(file("sample.dng"),makeDng());auto raw=imageview::decode(file("sample.dng"));require(!raw.image.isNull()&&raw.original.isValid()&&raw.metadata.contains("Camera make"),"RAW decoder not available");
        QByteArray psd=QByteArray::fromHex("38425053000100000000000000030000000100000001000800030000000000000000000000000000ff0000");
        write(file("sample.psd"),psd);auto p=imageview::decode(file("sample.psd"));require(!p.image.isNull()&&p.original==QSize(1,1),"PSD merged pixels");
        // JPEG's EXIF orientation 6 must rotate a 160×80 image to 80×160.
        QFile jpg(file("sample.jpg"));require(jpg.open(QIODevice::ReadOnly),"read JPEG bytes");auto jpeg=jpg.readAll();jpg.close();
        auto exif=QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
        // Length includes its own two bytes: two + six EXIF bytes + 26 TIFF bytes.
        jpeg.insert(2,exif);write(file("oriented.jpg"),jpeg);auto rotated=imageview::decode(file("oriented.jpg"));require(rotated.original==QSize(80,160)&&rotated.image.size()==QSize(80,160),"EXIF orientation applied once");
        auto p3=fixture;p3.setColorSpace(QColorSpace(QColorSpace::DisplayP3));require(p3.save(file("profile.png")),"write embedded ICC profile");auto srgb=imageview::decode(file("profile.png"));require(srgb.image.colorSpace()==QColorSpace(QColorSpace::SRgb),"embedded ICC converted to sRGB");
        // Filename mismatch should not prevent magic-based decoding.
        require(fixture.save(file("mystery"),"PNG"),"write extensionless image");require(!imageview::decode(file("mystery")).image.isNull(),"extensionless image");
        write(file("bad.jpg"),"broken");require(imageview::decode(file("bad.jpg")).image.isNull(),"invalid image rejected");
        const auto big=imageview::boundedSize({40000,20000});require(big.width()<=imageview::MaxDisplayEdge&&qint64(big.width())*big.height()<=imageview::MaxDisplayPixels,"large image bound");
        auto list=imageview::neighbors(file("sample.png"));require(list.contains(file("mystery"))&&!list.contains(file("settings.ini")),"directory scan");
        require(fixture.save(file("z.png")),"write last fixture");
        auto data=file("data");QDir().mkpath(data);Controller controller(data);QQmlApplicationEngine engine;engine.addImageProvider("view",new ImageProvider(controller.buffer()));engine.rootContext()->setContextProperty("viewer",&controller);
        engine.load(QUrl("qrc:/UI/Main.qml"));require(!engine.rootObjects().isEmpty(),"QML failed to load");
        controller.open(QUrl::fromLocalFile(file("sample.png")));require(waitFor([&]{return !controller.busy()&&controller.imageWidth()==160&&controller.count()>2&&engine.rootObjects().first()->property("imageReady").toBool();}),"async open, QML presentation and scan");
        require(controller.hasNext(),"next exists");auto old=controller.path();controller.next();require(waitFor([&]{return !controller.busy()&&controller.path()!=old;}),"next image");
        controller.previous();require(waitFor([&]{return !controller.busy()&&controller.path()==old&&controller.imageWidth()>0;}),"previous image");
        controller.open(QUrl::fromLocalFile(file("bad.jpg")));controller.open(QUrl::fromLocalFile(file("z.png")));require(waitFor([&]{return !controller.busy()&&controller.fileName()=="z.png"&&controller.imageWidth()==160;}),"stale request discarded");
        controller.moveToTrash(file("bad.jpg"));require(QFile::exists(file("z.png")),"stale trash confirmation rejected");
        controller.open(QUrl::fromLocalFile(file("b.webp")));require(waitFor([&]{return controller.animated()&&!controller.busy()&&controller.imageWidth()==3;}),"asynchronous animation");
        auto firstFrame=controller.imageUrl();require(waitFor([&]{return controller.imageUrl()!=firstFrame;}),"animation updated image provider");
        controller.togglePlayback();require(!controller.playing(),"animation paused");controller.togglePlayback();require(controller.playing(),"animation resumed");
        require(controller.setShortcut("next","Ctrl+Right"),"custom shortcut");require(!controller.setShortcut("previous","Ctrl+Right"),"duplicate shortcut rejected");controller.resetShortcuts();require(controller.shortcuts().value("next").toString()=="Right","default shortcut restored");
        controller.setTheme("dark");require(controller.theme()=="dark","theme setting");
        std::cout<<"PASS: "<<assertions<<" decoder, navigation, QML, settings and animation assertions\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
