#include "Browser.hpp"
#include "Downloads.hpp"
#include "Updater.hpp"
#ifdef Q_OS_IOS
#include "../Platform/IOSView.hpp"
#include <QGuiApplication>
#else
#include "../Platform/DesktopProfiles.hpp"
#include <QApplication>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCommandLineParser>
#include <QDebug>
#include <QUuid>
#include <memory>
#ifndef BROWSER_VERSION
#define BROWSER_VERSION "1.0.0"
#endif

#ifndef Q_OS_IOS
// Qt itself searches its known Chrome/Widevine locations. An explicit path
// overrides that search; configure it before Chromium is initialized.
static bool configureWidevine(int argc,char** argv){
    QByteArray path;
    bool specified=false;
    for(int i=1;i<argc;++i){
        const QByteArray arg(argv[i]);
        if(arg=="--")break;
        if(arg=="--widevine-path"||arg.startsWith("--widevine-path=")){
            if(specified){qCritical()<<"Specify --widevine-path only once";return false;}
            specified=true;
            if(arg=="--widevine-path"){
                if(i+1>=argc){qCritical()<<"--widevine-path requires a file";return false;}
                path=argv[++i];
            }else path=arg.mid(16);
        }
    }
    if(!specified)return true;
    QFileInfo info(QString::fromLocal8Bit(path));
#if defined(Q_OS_WIN)
    const QString expected="widevinecdm.dll";
#elif defined(Q_OS_MACOS)
    const QString expected="libwidevinecdm.dylib";
#else
    const QString expected="libwidevinecdm.so";
#endif
    const auto file=info.canonicalFilePath();
    if(!info.isAbsolute()||!info.isFile()||!info.isReadable()||info.fileName().compare(expected,Qt::CaseInsensitive)!=0||file.isEmpty()||file.contains('"')||file.contains('\r')||file.contains('\n')){
        qCritical().noquote()<<"--widevine-path must be an absolute readable"<<expected<<"file";return false;
    }
    auto flags=qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if(flags.contains("--widevine-path")){
        qCritical()<<"Choose either QTWEBENGINE_CHROMIUM_FLAGS or --widevine-path, not both";return false;
    }
    flags+=" --widevine-path=\""+file.toUtf8()+"\"";
    if(!qputenv("QTWEBENGINE_CHROMIUM_FLAGS",flags)){qCritical()<<"Could not configure Widevine";return false;}
    return true;
}
#endif

// A private, loopback-only HTTP origin is a trustworthy origin for browser
// media APIs. It serves one immutable embedded page, never reads local files
// or accepts data submitted by a page.
static QUrl startCompatibilityServer(QTcpServer& server,bool codecs,bool widevine){
    QFile resource(":/Compatibility.html");
    if(!resource.open(QIODevice::ReadOnly)||!server.listen(QHostAddress::LocalHost,0))return {};
    const auto body=resource.readAll();
    const QByteArray host="127.0.0.1:"+QByteArray::number(server.serverPort());
    QByteArray target="/"+QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
    if(codecs||widevine)target+=widevine?"?widevine=1":"?codecs=1";
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&server,body,host,target]{
        while(auto socket=server.nextPendingConnection()){
            socket->setReadBufferSize(16384);
            auto request=std::make_shared<QByteArray>();
            auto read=[socket,request,body,host,target]{
                request->append(socket->readAll());
                if(request->size()>16384){socket->disconnectFromHost();return;}
                if(!request->contains("\r\n\r\n"))return;
                auto lines=request->split('\n');
                auto first=lines.value(0).trimmed().split(' ');
                bool localHost=false;
                for(const auto& line:lines)if(line.toLower().startsWith("host:"))localHost=line.mid(5).trimmed()==host;
                const bool accepted=first.size()==3&&first[0]=="GET"&&first[1]==target&&localHost;
                QByteArray content=accepted?body:QByteArray("Not found");
                QByteArray response=accepted?"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n":"HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n";
                response+="Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                          "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; media-src blob:; connect-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\n"
                          "Content-Length: "+QByteArray::number(content.size())+"\r\n\r\n";
                socket->write(response+content);socket->disconnectFromHost();
            };
            QObject::connect(socket,&QTcpSocket::readyRead,socket,read);
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QTimer::singleShot(5000,socket,[socket]{if(socket->state()!=QAbstractSocket::UnconnectedState)socket->disconnectFromHost();});
            if(socket->bytesAvailable())read();
        }
    });
    return QUrl::fromEncoded("http://"+host+target);
}

int main(int argc,char** argv){
    QElapsedTimer startup;startup.start();
#ifndef Q_OS_IOS
    if(!configureWidevine(argc,argv))return 9;
    QtWebEngineQuick::initialize();
    QApplication app(argc,argv);
#else
    QGuiApplication app(argc,argv);
#endif
    app.setApplicationName("WebBrowser");app.setOrganizationName("AppSuite");app.setApplicationVersion(BROWSER_VERSION);
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"smoke-test","Open a local fixture in an isolated profile and verify browser integration."});
    parser.addOption({"compatibility-test","Open a local media/DRM/WebRTC diagnostic page using a temporary profile."});
    parser.addOption({"compatibility-smoke","Run automated capability checks and exit with pass/fail."});
    parser.addOption({"require-codecs","Require H.264/AAC and Media Source support in compatibility checks."});
    parser.addOption({"require-widevine","Require a working Widevine CDM and H.264/AAC in compatibility checks."});
#ifndef Q_OS_IOS
    parser.addOption({"widevine-path","Use a trusted, locally installed Widevine CDM (absolute library path).","file"});
#endif
    parser.addOption({"benchmark","Use an isolated profile and print startup latency."});
    parser.addOption({"portable","Keep browser data next to the executable (desktop)."});
    parser.addOption({"data-dir","Explicit browser data directory.","directory"});
    parser.addPositionalArgument("url","Optional HTTP(S) URL(s) to open.","[urls...]");
    parser.process(app);
    bool smoke=parser.isSet("smoke-test"),benchmark=parser.isSet("benchmark");
    const bool compatSmoke=parser.isSet("compatibility-smoke");
    const bool compatibility=parser.isSet("compatibility-test")||compatSmoke;
    if((smoke&&compatibility)||(compatibility&&benchmark)||(!compatibility&&(parser.isSet("require-codecs")||parser.isSet("require-widevine")))){
        qCritical()<<"Choose one test mode; codec and DRM requirements apply only to compatibility tests";return 2;
    }
    std::unique_ptr<QTemporaryDir> temporary;
    auto data=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#ifndef Q_OS_IOS
    if(parser.isSet("portable")||QFileInfo::exists(app.applicationDirPath()+"/portable.ini"))data=app.applicationDirPath()+"/data";
#endif
    if(parser.isSet("data-dir"))data=parser.value("data-dir");
    if(smoke||benchmark||compatibility){temporary=std::make_unique<QTemporaryDir>();if(!temporary->isValid())return 2;data=temporary->path();}
    if(!QDir().mkpath(data))return 2;
    qmlRegisterUncreatableType<Tab>("Browser.Core",1,0,"BrowserTab","Tab objects are managed by the browser");
    Browser browser(data);Downloads downloads;Updater updater(data+"/updates");
#ifdef Q_OS_IOS
    IOSView::initialize(&browser,&downloads);qmlRegisterType<IOSView>("Browser.Native",1,0,"NativeWebView");
#else
    DesktopProfiles profiles(&browser,&downloads);
    if(!smoke&&!benchmark&&!compatibility){auto feed=browser.options()["updateFeed"].toString();updater.configure(feed);QObject::connect(&browser,&Browser::optionsChanged,&updater,[&,feed]()mutable{auto next=browser.options()["updateFeed"].toString();if(next!=feed){feed=next;updater.configure(feed);}});}
#endif
    QTcpServer server;
    if(compatibility){
        const auto url=startCompatibilityServer(server,parser.isSet("require-codecs"),parser.isSet("require-widevine"));
        if(url.isEmpty()){qCritical()<<"Could not start local compatibility server";return 10;}
        auto tab=browser.current();
        tab->navigate(url);
        if(compatSmoke){
            QObject::connect(tab,&Tab::changed,&app,[&app,tab,url]{
                if(tab->url()!=url)return;
                if(tab->title().startsWith("COMPAT-PASS")||tab->title().startsWith("COMPAT-FAIL")){
                    const bool passed=tab->title().startsWith("COMPAT-PASS");
                    qInfo().noquote()<<"Browser compatibility"<<tab->title();
                    QTimer::singleShot(0,&app,[&app,passed]{app.exit(passed?0:11);});
                }
            });
            QTimer::singleShot(45000,&app,[&]{qCritical()<<"Browser compatibility timeout";app.exit(12);});
        }
    }else if(smoke){
        if(!server.listen(QHostAddress::LocalHost,0))return 3;
        QObject::connect(&server,&QTcpServer::newConnection,&app,[&]{
            while(auto socket=server.nextPendingConnection()){
                auto request=std::make_shared<QByteArray>();socket->setReadBufferSize(16384);
                QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket,request]{
                    request->append(socket->readAll());if(request->size()>16384){socket->disconnectFromHost();return;}if(!request->contains("\r\n\r\n"))return;
                    bool cookie=false;for(const auto& line:request->split('\n'))if(line.toLower().startsWith("cookie:")&&line.contains("fixture=1"))cookie=true;
                    QByteArray body="<html><head><title>Browser fixture "+QByteArray(cookie?"cookie":"empty")+"</title></head><body>Local test</body></html>";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nCache-Control: no-store\r\nSet-Cookie: fixture=1; Max-Age=600; Path=/\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
        browser.current()->navigate(QUrl("http://127.0.0.1:"+QString::number(server.serverPort())+"/"));
    }else if(!parser.positionalArguments().isEmpty()){
        auto urls=parser.positionalArguments();browser.go(urls.takeFirst());
        for(auto& u:urls)browser.newTab(Store::address(u,browser.options()["search"].toString()));
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("browser",&browser);
    engine.rootContext()->setContextProperty("downloads",&downloads);
    engine.rootContext()->setContextProperty("updater",&updater);
#ifndef Q_OS_IOS
    engine.rootContext()->setContextProperty("profiles",&profiles);
#else
    engine.rootContext()->setContextProperty("profiles",QVariant());
#endif
    engine.load(QUrl("qrc:/Main.qml"));
    if(engine.rootObjects().isEmpty())return 4;
    qInfo().noquote()<<"READY_MS"<<startup.elapsed();
    if(smoke){
        auto regular=browser.current();
        const QUrl fixture=regular->url();auto phase=std::make_shared<int>(0);
        QObject::connect(regular,&Tab::navigationCommitted,&app,[&,regular,fixture,phase]{
            if(regular->url()!=fixture)return;
            if(*phase==0){if(regular->title()!="Browser fixture empty"){app.exit(5);return;}*phase=1;QTimer::singleShot(0,&app,[regular,fixture]{regular->navigate(fixture);});return;}
            if(*phase!=1)return;*phase=2;
            if(regular->title()!="Browser fixture cookie"){qCritical()<<"Regular cookie was not retained";app.exit(5);return;}
            QTimer::singleShot(0,&app,[&,fixture]{
                auto p=browser.newTab(QUrl("about:blank"),true);if(!p){app.exit(6);return;}
                QObject::connect(p,&Tab::navigationCommitted,&app,[&,p,fixture]{
                    if(p->url()!=fixture)return;
                    if(p->title()!="Browser fixture empty"){qCritical()<<"Private profile inherited regular cookies";app.exit(7);return;}
                    QTimer::singleShot(0,&app,[&,p,fixture]{
                        browser.closeTab(p->id());auto next=browser.newTab(QUrl("about:blank"),true);if(!next){app.exit(6);return;}
                        QObject::connect(next,&Tab::navigationCommitted,&app,[&,next,fixture]{
                            if(next->url()!=fixture)return;
                            bool isolated=next->title()=="Browser fixture empty";auto h=browser.history();bool ok=isolated&&h.size()==1&&h[0].toMap()["visits"].toInt()==2;
                            QTimer::singleShot(0,&app,[&,next,ok]{browser.closeTab(next->id());qInfo()<<"Browser cookie and history integration"<<(ok?"PASS":"FAIL");app.exit(ok?0:7);});
                        });next->navigate(fixture);
                    });
                });p->navigate(fixture);
            });
        });
        QTimer::singleShot(30000,&app,[&]{qCritical()<<"Browser integration timeout";app.exit(8);});
    }
    return app.exec();
}
