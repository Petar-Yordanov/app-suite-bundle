#include "Controller.hpp"
#include "TerminalItem.hpp"
#include "Updater.hpp"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QtQml/qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QDir>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTextStream>
#include <memory>

int main(int argc,char** argv){
    QElapsedTimer time;time.start();
    QQuickWindow::setDefaultAlphaBuffer(true);
    QApplication app(argc,argv);app.setApplicationName("TerminalEmulator");app.setOrganizationName("AppSuite");app.setApplicationVersion("1.0.0");
    QQuickStyle::setStyle("Basic");
    auto args=app.arguments();
    if(args.contains("--version")){QTextStream(stdout)<<"TerminalEmulator "<<app.applicationVersion()<<Qt::endl;return 0;}
    QString dir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    std::unique_ptr<QTemporaryDir> temporary;
    bool smoke=args.contains("--smoke-test"),bench=args.contains("--benchmark-idle");
    if(args.contains("--portable")){
        QDir d(app.applicationDirPath());
#ifdef Q_OS_MACOS
        if(d.dirName()=="MacOS"){d.cdUp();d.cdUp();d.cdUp();}
#endif
        dir=d.filePath("TerminalEmulator-data");
    }
    if(smoke||bench){temporary=std::make_unique<QTemporaryDir>();if(!temporary->isValid())return 1;dir=temporary->path();}
    if(dir.isEmpty()||!QDir().mkpath(dir))return 1;
    Controller control(dir+"/settings.ini");
    if(!smoke&&!bench)QTimer::singleShot(0,&control,&Controller::refreshShells);
    Updater updater(dir+"/updates");updater.configure(control.options().value("feed").toString());
    QObject::connect(&control,&Controller::feedChanged,&updater,[&]{updater.configure(control.options().value("feed").toString());});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&control,&Controller::save);
    qmlRegisterType<TerminalItem>("AppSuite.Terminal",1,0,"TerminalSurface");
    qmlRegisterUncreatableType<Session>("AppSuite.Terminal",1,0,"Session","Owned by the controller");
    qmlRegisterUncreatableType<PaneNode>("AppSuite.Terminal",1,0,"PaneNode","Owned by the controller");
    qmlRegisterUncreatableType<TerminalTab>("AppSuite.Terminal",1,0,"TerminalTab","Owned by the controller");
    QQmlApplicationEngine engine;
    int warnings=0;
    QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors){warnings+=errors.size();});
    engine.rootContext()->setContextProperty("controller",&control);
    engine.rootContext()->setContextProperty("updater",&updater);
    engine.load(QUrl("qrc:/Main.qml"));if(engine.rootObjects().isEmpty())return 2;
    QTextStream(stdout)<<"READY_MS="<<time.elapsed()<<Qt::endl;
    if(smoke){
        QTimer::singleShot(300,&control,[&]{control.split(true,"");control.split(false,"");});
        QTimer::singleShot(600,&control,[&]{control.duplicateTab(control.currentIndex());control.moveTab(control.currentIndex(),0);});
        QTimer::singleShot(900,&control,[&]{control.closePane();control.save();});
        QTimer::singleShot(1500,&app,[&]{app.exit(warnings||!control.currentRoot()?3:0);});
    }
    if(bench)QTimer::singleShot(15000,&app,&QCoreApplication::quit);
    return app.exec();
}
