#include "Controller.hpp"
#include <QApplication>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTemporaryDir>
#include <QFileInfo>

int main(int argc,char** argv){
    QElapsedTimer startup;startup.start();rec::native::initialize();
    QApplication app(argc,argv);app.setApplicationName("ScreenRecorder");app.setOrganizationName("AppSuite");app.setApplicationVersion("1.0.0");
    app.setQuitOnLastWindowClosed(false);QQuickStyle::setStyle("Basic");
    auto args=app.arguments();
    if(args.contains("--version")){QTextStream(stdout)<<"ScreenRecorder "<<app.applicationVersion()<<Qt::endl;return 0;}
    if(args.contains("--help")){QTextStream(stdout)<<"ScreenRecorder [--background | --show | --toggle | --pause | --stop] [--portable]"<<Qt::endl;return 0;}
    if(args.contains("--capture-smoke")){
        // A real short monitor capture, with two recording segments and pause.
        QTemporaryDir output;if(!output.isValid())return 1;
        auto screens=rec::native::screens();if(screens.isEmpty())return 2;
        Recorder recording;
        rec::Config config;config.output=output.path();config.hardware=false;config.fps=15;
        config.height=720;config.bitrate=1500;config.quality=0;
        int phase=0;
        QObject::connect(&recording,&Recorder::changed,&app,[&]{
            if(recording.state()==rec::State::Recording&&phase==0){
                phase=1;QTimer::singleShot(1500,&recording,&Recorder::pause);
            }else if(recording.state()==rec::State::Paused&&phase==1){
                phase=2;QTimer::singleShot(300,&recording,&Recorder::resume);
            }else if(recording.state()==rec::State::Recording&&phase==2){
                phase=3;QTimer::singleShot(1500,&recording,&Recorder::stop);
            }else if(recording.state()==rec::State::Idle){
                QTextStream(stdout)<<recording.status()<<Qt::endl;
                app.exit(phase==3&&QFileInfo(recording.lastFile()).size()>1024?0:1);
            }
        });
        QTimer::singleShot(0,&recording,[&]{recording.start(screens.first(),{},config,{});});
        QTimer::singleShot(45000,&app,[&]{qCritical("Capture smoke test timed out");app.exit(1);});
        return app.exec();
    }
    QString mode=args.contains("--background")||args.contains("--benchmark-idle")?"background":"show";
    for(const auto* m:{"toggle","pause","stop","show"})if(args.contains("--"+QString(m)))mode=m;
    QString data=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    std::unique_ptr<QTemporaryDir> benchmark;
    if(args.contains("--portable")){
        QDir dir(app.applicationDirPath());
#ifdef Q_OS_MACOS
        if(dir.dirName()=="MacOS"){dir.cdUp();dir.cdUp();dir.cdUp();}
#endif
        data=dir.filePath("ScreenRecorder-data");
    }
    if(args.contains("--benchmark-idle")||args.contains("--smoke-test")){
        benchmark=std::make_unique<QTemporaryDir>();if(!benchmark->isValid())return 1;
        data=benchmark->path();
    }
    if(!QDir().mkpath(data)){qCritical("Cannot create settings directory");return 1;}
    data=QDir(data).canonicalPath();
    auto socketName="appsuite-rec-"+QString::fromLatin1(QCryptographicHash::hash(data.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
    QLockFile lock(data+"/instance.lock");lock.setStaleLockTime(0);
    if(!lock.tryLock(0)){
        QLocalSocket socket;socket.connectToServer(socketName);
        if(!socket.waitForConnected(2000)){qCritical("Screen Recorder is starting or unavailable.");return 1;}
        socket.write(mode.toUtf8()+"\n");socket.flush();socket.waitForBytesWritten(1000);return 0;
    }
    QLocalServer server;server.setSocketOptions(QLocalServer::UserAccessOption);QLocalServer::removeServer(socketName);
    if(!server.listen(socketName)){qCritical("Cannot start local command server");return 1;}
    Controller controller(data);
    QObject::connect(&server,&QLocalServer::newConnection,&controller,[&]{
        while(auto* socket=server.nextPendingConnection()){
            socket->setReadBufferSize(128);
            QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            auto read=[socket,&controller]{
                if(!socket->canReadLine()){if(socket->bytesAvailable()>=128)socket->abort();return;}
                auto cmd=QString::fromUtf8(socket->readLine(128)).trimmed();
                socket->disconnectFromServer();controller.command(cmd);
            };
            QObject::connect(socket,&QLocalSocket::readyRead,&controller,read);read();
            QTimer::singleShot(2000,socket,[socket]{socket->abort();});
        }
    });
    if(args.contains("--smoke-test")){
        controller.show();
        QTimer::singleShot(1200,&app,[&]{if(controller.status().contains("Could not load"))app.exit(1);else app.quit();});
    }else if(mode!="background")controller.command(mode);
    if(args.contains("--benchmark-idle"))QTextStream(stdout)<<"READY_MS="<<startup.elapsed()<<Qt::endl;
    return app.exec();
}
