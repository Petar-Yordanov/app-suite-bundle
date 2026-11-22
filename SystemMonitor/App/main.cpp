#include "Controller.hpp"
#include "Updater.hpp"
#include "Management.hpp"
#include <QCoreApplication>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTextStream>
#include <QtQml/qqml.h>
#include <memory>
#include <string_view>
int main(int argc,char** argv){
    if(argc>1&&std::string_view(argv[1]).starts_with("--privileged-")){
        QCoreApplication app(argc,argv);return management::helper(app.arguments());
    }
    QElapsedTimer elapsed;elapsed.start();QApplication app(argc,argv);
    app.setOrganizationName("AppSuite");app.setApplicationName("SystemMonitor");app.setApplicationVersion("1.0.0");QQuickStyle::setStyle("Basic");
    auto args=app.arguments();
    if(args.contains("--version")){QTextStream(stdout)<<"SystemMonitor "<<app.applicationVersion()<<Qt::endl;return 0;}
    bool smoke=args.contains("--smoke-test"),bench=args.contains("--benchmark-idle");
    QString path=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    std::unique_ptr<QTemporaryDir> temp;
    if(args.contains("--portable")){
        QDir d(app.applicationDirPath());
#ifdef Q_OS_MACOS
        if(d.dirName()=="MacOS"){d.cdUp();d.cdUp();d.cdUp();}
#endif
        path=d.filePath("SystemMonitor-data");
    }
    if(smoke||bench){temp=std::make_unique<QTemporaryDir>();if(!temp->isValid())return 1;path=temp->path();}
    if(path.isEmpty()||!QDir().mkpath(path))return 1;
    Controller controller(path+"/settings.ini");Updater updater(path+"/updates");updater.configure(controller.options().value("feed").toString());
    QObject::connect(&controller,&Controller::feedChanged,&updater,[&]{updater.configure(controller.options().value("feed").toString());});
    qmlRegisterUncreatableType<ProcessProxy>("AppSuite.Monitor",1,0,"ProcessProxy","Owned by controller");
    qmlRegisterUncreatableType<MetricsModel>("AppSuite.Monitor",1,0,"MetricsModel","Owned by controller");
    QQmlApplicationEngine engine;int warnings=0;
    QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& list){warnings+=list.size();});
    engine.rootContext()->setContextProperty("controller",&controller);engine.rootContext()->setContextProperty("updater",&updater);
    engine.load(QUrl("qrc:/Main.qml"));if(engine.rootObjects().isEmpty())return 2;
    QTextStream(stdout)<<"READY_MS="<<elapsed.elapsed()<<Qt::endl;
    if(smoke){
        QTimer::singleShot(1200,&controller,[&]{controller.processes()->query("systemmonitor");controller.processes()->order("memory",true);});
        QTimer::singleShot(2200,&controller,[&]{controller.processes()->query("");controller.loadEntries("startup");});
        QTimer::singleShot(4000,&app,[&]{app.exit(warnings||controller.processCount()==0?3:0);});
    }
    if(bench)QTimer::singleShot(15000,&app,&QCoreApplication::quit);
    return app.exec();
}
