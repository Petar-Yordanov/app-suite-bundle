#include "Controller.hpp"
#include "Updater.hpp"
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
#include <QFileInfo>
#include <memory>

int main(int argc,char** argv){
    QElapsedTimer clock;clock.start();
    QApplication app(argc,argv);
    app.setApplicationName("DockerGUI");app.setOrganizationName("AppSuite");app.setApplicationVersion("1.0.0");
    QQuickStyle::setStyle("Basic");
#ifdef Q_OS_MACOS
    // Finder-launched applications don't inherit interactive shell PATH.
    qputenv("PATH",qgetenv("PATH")+":/opt/homebrew/bin:/usr/local/bin:"+QDir::homePath().toUtf8()+"/.docker/bin:/Applications/Docker.app/Contents/Resources/bin");
#elif defined(Q_OS_WIN)
    qputenv("PATH",qgetenv("PATH")+";"+qEnvironmentVariable("ProgramFiles").toUtf8()+"/Docker/Docker/resources/bin");
#endif
    auto args=app.arguments();
    if(args.contains("--version")){QTextStream(stdout)<<"DockerGUI "<<app.applicationVersion()<<Qt::endl;return 0;}
    std::unique_ptr<QTemporaryDir> test;
    QString dir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(args.contains("--portable")){
        QDir d(app.applicationDirPath());
#ifdef Q_OS_MACOS
        if(d.dirName()=="MacOS"){d.cdUp();d.cdUp();d.cdUp();}
#endif
        dir=d.filePath("DockerGUI-data");
    }
    if(args.contains("--smoke-test")||args.contains("--benchmark-idle")){
        test=std::make_unique<QTemporaryDir>();if(!test->isValid())return 1;dir=test->path();
    }
    if(dir.isEmpty()||!QDir().mkpath(dir))return 1;
    Controller control(dir+"/settings.ini");
    if(args.contains("--smoke-test")||args.contains("--benchmark-idle"))control.setAutoStart(false);
    Updater updater(dir+"/updates");
    updater.configure(control.feed());
    QObject::connect(&control,&Controller::feedChanged,&updater,[&]{updater.configure(control.feed());});
    QQmlApplicationEngine engine;
    int qmlWarnings=0;
    QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& warnings){qmlWarnings+=warnings.size();});
    qRegisterMetaType<ResourceModel*>();
    engine.rootContext()->setContextProperty("controller",&control);
    engine.rootContext()->setContextProperty("updater",&updater);
    engine.load(QUrl("qrc:/Main.qml"));
    if(engine.rootObjects().isEmpty())return 2;
    QTextStream(stdout)<<"READY_MS="<<clock.elapsed()<<Qt::endl;
    if(args.contains("--smoke-test"))QTimer::singleShot(1000,&app,[&]{app.exit(qmlWarnings?3:0);});
    if(args.contains("--benchmark-idle"))QTimer::singleShot(15000,&app,&QCoreApplication::quit);
    return app.exec();
}
