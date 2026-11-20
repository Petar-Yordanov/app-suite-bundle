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

int main(int argc,char** argv){
    QElapsedTimer startup;startup.start();shot::native::initialize();
    QApplication app(argc,argv);app.setApplicationName("ScreenshotTool");app.setOrganizationName("AppSuite");app.setApplicationVersion("1.0.0");
    app.setQuitOnLastWindowClosed(false);QQuickStyle::setStyle("Basic");
    auto args=app.arguments();
    if(args.contains("--version")){QTextStream(stdout)<<"ScreenshotTool "<<app.applicationVersion()<<Qt::endl;return 0;}
    if(args.contains("--help")){QTextStream(stdout)<<"ScreenshotTool [--background | --region | --screen | --window | --show | --cancel] [--portable]"<<Qt::endl;return 0;}
    QString mode=args.contains("--background")?"background":"show";
    for(const auto& m:{"region","screen","window","cancel","show"})if(args.contains("--"+QString(m)))mode=m;
    QString data=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    std::unique_ptr<QTemporaryDir> benchmark;
    if(args.contains("--portable")){
        QDir dir(app.applicationDirPath());
#ifdef Q_OS_MACOS
        if(dir.dirName()=="MacOS"){dir.cdUp();dir.cdUp();dir.cdUp();}
#endif
        data=dir.filePath("ScreenshotTool-data");
    }
    if(args.contains("--benchmark-idle")){
        benchmark=std::make_unique<QTemporaryDir>();
        if(!benchmark->isValid())return 1;
        data=benchmark->path();
    }
    if(!QDir().mkpath(data)){qCritical("Cannot create settings directory");return 1;}
    data=QDir(data).canonicalPath();
    auto socketName="appsuite-shot-"+QString::fromLatin1(QCryptographicHash::hash(data.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
    QLockFile lock(data+"/instance.lock");
    lock.setStaleLockTime(0);
    if(!lock.tryLock(0)){
        QLocalSocket socket;socket.connectToServer(socketName);
        if(!socket.waitForConnected(2000)){qCritical("Screenshot Tool is starting or its communication socket is unavailable.");return 1;}
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
                auto mode=QString::fromUtf8(socket->readLine(128)).trimmed();socket->disconnectFromServer();controller.command(mode);
            };
            QObject::connect(socket,&QLocalSocket::readyRead,&controller,read);read();
            QTimer::singleShot(2000,socket,[socket]{socket->abort();});
        }
    });
    if(mode!="background")controller.command(mode);
    if(args.contains("--benchmark-idle"))QTextStream(stdout)<<"READY_MS="<<startup.elapsed()<<Qt::endl;
    return app.exec();
}
