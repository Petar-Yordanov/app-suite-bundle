#include "../App/Controller.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QMimeData>
#include <QQuickStyle>
#include <QUrl>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool waitFor(F f){
    QElapsedTimer timer;timer.start();
    do {QCoreApplication::processEvents(QEventLoop::AllEvents,20);if(f())return true;QThread::msleep(10);}
    while(timer.elapsed()<5000);
    return false;
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("ClipDeckIntegrationTests");QQuickStyle::setStyle("Basic");
    try{
        QTemporaryDir temp;require(temp.isValid(),"Temporary directory unavailable");
        const auto data=temp.filePath("data");require(QDir().mkpath(data),"Cannot make data directory");
        {QSettings settings(QDir(data).filePath("settings.ini"),QSettings::IniFormat);
         settings.setValue("autostart",false);settings.setValue("shortcut","Ctrl+Alt+F12");settings.sync();}
        Controller controller(data);
        require(waitFor([&]{return controller.status()=="Clipboard history ready.";}),"Storage failed to initialize");
        controller.showUi();
        require(waitFor([&]{return controller.storageSummary().startsWith("0 entries");}),"QML failed to initialize");
        auto* clipboard=QApplication::clipboard();
        auto* rich=new QMimeData;rich->setText("clipdeck integration text");rich->setHtml("<b>clipdeck integration text</b>");
        rich->setData("application/x-clipdeck-integration",QByteArray("a\0b",3));clipboard->setMimeData(rich);
        require(waitFor([&]{return controller.entries().size()==1;}),"Clipboard capture failed");
        auto id=controller.entries().first().toMap().value("id").toString();
        auto* secret=new QMimeData;secret->setText("never save");secret->setData("application/x-kde-passwordManagerHint","1");clipboard->setMimeData(secret);
        require(waitFor([&]{return controller.status().contains("Sensitive");}),"Password marker not honored");
        require(controller.entries().size()==1,"Sensitive entry persisted");
        require(clipboard->text()=="never save","Password clipboard altered");
        controller.restore(id);
        require(waitFor([&]{return clipboard->text()=="clipdeck integration text";}),"Text restore failed");
        require(clipboard->mimeData()->html()=="<b>clipdeck integration text</b>","HTML restore failed");
        require(clipboard->mimeData()->data("application/x-clipdeck-integration")==QByteArray("a\0b",3),"Binary restore failed");
        controller.showUi();
        require(waitFor([&]{return controller.entries().size()==1;}),"History reload failed");
        auto source=temp.filePath("original.txt");
        {QFile file(source);require(file.open(QIODevice::WriteOnly),"File creation failed");require(file.write("snapshot") == 8,"File write failed");}
        auto* files=new QMimeData;files->setUrls({QUrl::fromLocalFile(source)});clipboard->setMimeData(files);
        require(waitFor([&]{return controller.entries().size()==2;}),"File capture failed");
        auto fileId=controller.entries().first().toMap().value("id").toString();
        require(QFile::remove(source),"Original file removal failed");
        controller.restore(fileId);
        require(waitFor([&]{auto urls=clipboard->mimeData()->urls();return urls.size()==1&&urls.first().toLocalFile()!=source;}),"File restore failed");
        QFile snapshot(clipboard->mimeData()->urls().first().toLocalFile());
        require(snapshot.open(QIODevice::ReadOnly)&&snapshot.readAll()=="snapshot","Snapshot is not independent");
        clipboard->clear();
        std::cout<<"PASS: Qt clipboard, QML, privacy marker, multi-MIME and file restore integration\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
