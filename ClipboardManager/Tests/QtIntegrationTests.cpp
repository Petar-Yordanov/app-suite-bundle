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
#include <QProcess>
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
    if(app.arguments().size()==3&&app.arguments().at(1)=="--clipboard-writer"){
        // Separate GUI process, like a real browser/editor owning the clipboard.
        QApplication::clipboard()->setText(app.arguments().at(2));
        QTimer::singleShot(15000,&app,&QCoreApplication::quit);
        return app.exec();
    }
    try{
        QTemporaryDir temp;require(temp.isValid(),"Temporary directory unavailable");
        const auto data=temp.filePath("data");require(QDir().mkpath(data),"Cannot make data directory");
        {QSettings settings(QDir(data).filePath("settings.ini"),QSettings::IniFormat);
         settings.setValue("autostart",false);settings.setValue("shortcut","Ctrl+Alt+F12");settings.sync();}
        Controller controller(data);
        require(waitFor([&]{return controller.status()=="Clipboard history ready.";}),"Storage failed to initialize");
        auto* clipboard=QApplication::clipboard();
        int saved=0;
        QObject::connect(&controller,&Controller::statusChanged,&app,[&]{
            if(controller.status()=="Clipboard item saved.")++saved;
        });
        QProcess writer;
        auto storedCount=[&]{return QDir(QDir(data).filePath("history")).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size();};
        const bool nativeClipboard=QGuiApplication::platformName()=="windows"||
            QGuiApplication::platformName()=="cocoa"||QGuiApplication::platformName()=="xcb";
        if(nativeClipboard){
            writer.start(QCoreApplication::applicationFilePath(),{"--clipboard-writer","external background copy"});
            require(writer.waitForStarted(5000),"External clipboard writer failed to start");
        }else clipboard->setText("external background copy");
        require(waitFor([&]{return saved>=1&&storedCount()==1;}),"First copy failed with no QML window");
        clipboard->setText("second background copy");
        require(waitFor([&]{return saved>=2&&storedCount()==2;}),"Second copy failed with no QML window");
        if(writer.state()!=QProcess::NotRunning){writer.terminate();if(!writer.waitForFinished(2000)){writer.kill();writer.waitForFinished(2000);}}
        controller.showUi();
        require(waitFor([&]{
            bool first=false,second=false;
            for(const auto& entry:controller.entries()){
                auto preview=entry.toMap().value("preview").toString();
                first|=preview=="external background copy";second|=preview=="second background copy";
            }
            return first&&second;
        }),"Background captures were not present when opening QML");
        controller.hideUi();
        // Let the queued close destroy the QML engine before copying again.
        QCoreApplication::processEvents();
        auto before=saved;
        clipboard->setText("copy after hiding history");
        require(waitFor([&]{return saved>before&&storedCount()==3;}),"Copy after hiding QML failed");
        controller.showUi();
        require(waitFor([&]{
            for(const auto& entry:controller.entries())if(entry.toMap().value("preview")=="copy after hiding history")return true;
            return false;
        }),"Background copy missing after reopening history");
        controller.clear();
        require(waitFor([&]{return controller.entries().isEmpty()&&controller.storageSummary().startsWith("0 entries");}),"Cannot reset test history");
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
        std::cout<<"PASS: background clipboard capture, QML reopen, privacy marker, multi-MIME and file restore integration\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
