#include "Controller.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

class Application final:public QApplication {
public:
    using QApplication::QApplication;
    Controller* controller=nullptr;
    QUrl pending;
    bool event(QEvent* e) override {
        if(e->type()==QEvent::FileOpen){
            auto* open=static_cast<QFileOpenEvent*>(e);auto url=open->url();if(url.isEmpty())url=QUrl::fromLocalFile(open->file());
            if(controller)controller->open(url);else pending=url;return true;
        }
        return QApplication::event(e);
    }
};
int main(int argc,char** argv){
    const auto start=QDateTime::currentMSecsSinceEpoch();
    Application app(argc,argv);app.setApplicationName("ImageViewer");app.setApplicationDisplayName("Image Viewer");app.setApplicationVersion("1.0.0");app.setOrganizationName("AppSuite");
    QQuickStyle::setStyle("Basic");QImageReader::setAllocationLimit(512);
    QCommandLineParser parser;parser.setApplicationDescription("Lightweight native image viewer.");parser.addHelpOption();parser.addVersionOption();
    parser.addOption(QCommandLineOption("portable","Keep settings alongside the application (requires a writable folder)."));
    parser.addOption(QCommandLineOption("benchmark","Print elapsed startup/decode timing to stdout."));
    parser.addOption(QCommandLineOption("formats","Print supported filename extensions and exit."));
    parser.addPositionalArgument("image","Image file to open.","[image]");parser.process(app);
    if(parser.isSet("formats")){std::puts(qPrintable(imageview::extensions().join(' ')));return 0;}
    QString directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(parser.isSet("portable")){
        QDir base(app.applicationDirPath());
#ifdef Q_OS_MACOS
        // Sibling of the .app, not inside its signed bundle.
        if(base.dirName()=="MacOS"){base.cdUp();base.cdUp();base.cdUp();}
#endif
        directory=base.filePath("ImageViewerData");
    }
    if(!QDir().mkpath(directory)){std::fputs("Unable to create settings directory.\n",stderr);return 1;}
    Controller controller(directory);app.controller=&controller;
    if(parser.isSet("benchmark"))controller.enableBenchmark(start);
    // The initial image can decode while the QML scene is being constructed.
    if(!parser.positionalArguments().isEmpty())controller.open(QUrl::fromLocalFile(QFileInfo(parser.positionalArguments().first()).absoluteFilePath()));
    else if(!app.pending.isEmpty())controller.open(app.pending);
    QQmlApplicationEngine engine;engine.addImageProvider("view",new ImageProvider(controller.buffer()));
    engine.rootContext()->setContextProperty("viewer",&controller);engine.load(QUrl("qrc:/UI/Main.qml"));
    if(engine.rootObjects().isEmpty())return 2;
    if(auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first())){
        QObject::connect(window,&QQuickWindow::frameSwapped,&controller,[&controller,window]{
            if(window->property("imageReady").toBool())controller.imagePresented();
        },Qt::QueuedConnection);
    }
    auto code=app.exec();app.controller=nullptr;return code;
}
