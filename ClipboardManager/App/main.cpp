#include "Controller.hpp"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQuickStyle>
#include <QStandardPaths>
#include <cstdio>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("ClipDeck");app.setApplicationVersion("1.0.0");app.setOrganizationName("ClipDeck");
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;parser.setApplicationDescription("Local, lightweight clipboard history.");
    parser.addHelpOption();parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("background"),"Start without opening the history window."));
    parser.addOption(QCommandLineOption(QStringLiteral("show"),"Open the history window, including in an already running instance."));
    parser.addOption(QCommandLineOption(QStringLiteral("portable"),"Keep settings and clipboard history next to the executable."));
    parser.process(app);
    const bool portable=parser.isSet("portable");
    QString directory=portable?QDir(app.applicationDirPath()).filePath("ClipDeckData"):QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(!QDir().mkpath(directory)){std::fputs("Cannot create the ClipDeck data directory.\n",stderr);return 1;}
    auto dataPath=QFileInfo(directory).canonicalFilePath();if(dataPath.isEmpty())dataPath=directory;
    const auto serverName="clipdeck-"+QString::fromLatin1(QCryptographicHash::hash(dataPath.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
    QLockFile lock(QDir(dataPath).filePath("instance.lock"));
    lock.setStaleLockTime(0);
    if(!lock.tryLock(0)){
        QLocalSocket socket;socket.connectToServer(serverName);
        if(socket.waitForConnected(1500)){
            socket.write(parser.isSet("background")&&!parser.isSet("show")?"ping":"show");
            socket.flush();socket.waitForBytesWritten(1500);return 0;
        }
        std::fputs("A ClipDeck instance already holds this data directory.\n",stderr);return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);QLocalServer::removeServer(serverName);
    if(!server.listen(serverName)){std::fputs("Could not start ClipDeck local IPC.\n",stderr);return 1;}
    Controller controller(dataPath);
    QObject::connect(&server,&QLocalServer::newConnection,&controller,[&]{
        while(auto* socket=server.nextPendingConnection()){
            socket->setParent(&server);
            auto read=[socket,&controller]{
                auto cmd=socket->read(32);
                if(cmd=="show")controller.showUi();
                socket->disconnectFromServer();
            };
            QObject::connect(socket,&QLocalSocket::readyRead,&controller,read);
            QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            if(socket->bytesAvailable())read();
            QTimer::singleShot(3000,socket,[socket]{socket->disconnectFromServer();});
        }
    });
    if(parser.isSet("show")||!parser.isSet("background")||!controller.hasTray())controller.showUi();
    return app.exec();
}
