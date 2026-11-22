#include "MainWindow.hpp"
#include <QApplication>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <QDir>
#include <QTemporaryDir>
#include <QFileOpenEvent>
#include <QJsonDocument>
#include <QTextStream>
#include <QElapsedTimer>
#include <QFileInfo>
#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#endif

namespace {
class OpenEvents : public QObject {
public:
    explicit OpenEvents(Player& p):player_(p){timer_.setSingleShot(true);timer_.setInterval(80);connect(&timer_,&QTimer::timeout,this,[this]{auto urls=std::move(pending_);pending_.clear();player_.openUrls(urls);});}
    bool eventFilter(QObject*,QEvent* event) override {
        if(event->type()!=QEvent::FileOpen)return false;
        auto* open=static_cast<QFileOpenEvent*>(event);if(open->url().isLocalFile()){pending_.append(open->url());timer_.start();}return true;
    }
private:
    Player& player_;QList<QUrl> pending_;QTimer timer_;
};
}
int main(int argc,char** argv){
    QElapsedTimer total;total.start();
#ifdef Q_OS_LINUX
    // Required before Qt or libVLC opens an X11 connection.
    XInitThreads();
    // libVLC 3 embeds X11 drawables; use XWayland whenever available. Users
    // can explicitly set QT_QPA_PLATFORM=wayland for the software fallback.
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")&&!qEnvironmentVariableIsEmpty("DISPLAY")&&!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))qputenv("QT_QPA_PLATFORM","xcb");
#endif
    QApplication app(argc,argv);app.setApplicationName("MediaPlayer");app.setOrganizationName("AppSuite");app.setApplicationVersion("1.0.0");
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;parser.setApplicationDescription("Lightweight local libVLC video player");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"portable","Store settings beside the application"});parser.addOption({"software-output","Use the bounded software frame callback renderer"});
    parser.addOption({"check-engine","Check that the libVLC runtime loads and initializes"});parser.addOption({"benchmark","Measure playback initialization and video statistics using an isolated profile"});
    parser.addOption({"benchmark-idle","Run an isolated idle UI for the specified benchmark duration"});
    parser.addOption({"benchmark-seconds","Playback sample duration","seconds","15"});
    parser.addPositionalArgument("video","Local video file(s)","[video...]");parser.process(app);
    auto profile=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(parser.isSet("portable")){
        QDir dir(app.applicationDirPath());
#ifdef Q_OS_MACOS
        if(dir.dirName()=="MacOS"){dir.cdUp();dir.cdUp();dir.cdUp();}
#endif
        profile=dir.filePath("MediaPlayer-data");
    }
    std::unique_ptr<QTemporaryDir> temporary;
    if(parser.isSet("benchmark")||parser.isSet("benchmark-idle")||parser.isSet("check-engine")){temporary=std::make_unique<QTemporaryDir>();if(!temporary->isValid())return 1;profile=temporary->path();}
    if(!QDir().mkpath(profile)){qCritical("Cannot create settings directory");return 1;}
    if(parser.isSet("check-engine")){
        VideoSurface surface(true);Player player(profile,&surface,nullptr,true);QString error;
        if(!player.checkEngine(error)){QTextStream(stderr)<<error<<Qt::endl;return 1;}
        QTextStream(stdout)<<"libVLC "<<player.engineVersion()<<Qt::endl;return 0;
    }
    MainWindow window(profile,parser.isSet("software-output"));if(!window.valid())return 1;
    OpenEvents opens(window.player());app.installEventFilter(&opens);window.show();
    QList<QUrl> urls;for(const auto& file:parser.positionalArguments())urls.append(QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()));
    bool bench=parser.isSet("benchmark");
    if(parser.isSet("benchmark-idle")){
        bool ok=false;int seconds=parser.value("benchmark-seconds").toInt(&ok);
        if(!ok||seconds<1||seconds>600||!urls.isEmpty()||bench)return 2;
        QTextStream(stdout)<<"UI_READY_MS="<<total.elapsed()<<Qt::endl;
        QTimer::singleShot(seconds*1000,&app,&QCoreApplication::quit);
    }
    if(bench&&urls.size()!=1){qCritical("Benchmark requires exactly one local video");return 2;}
    if(!urls.isEmpty())QTimer::singleShot(0,&window,[&window,urls]{window.player().openUrls(urls);});
    QTimer sample;QElapsedTimer playback; qint64 firstFrame=-1;
    if(bench){
        bool ok=false;int seconds=parser.value("benchmark-seconds").toInt(&ok);
        if(!ok||seconds<1||seconds>600){qCritical("Benchmark duration must be 1..600 seconds");return 2;}
        const auto uiReady=total.elapsed();sample.setInterval(100);
        QObject::connect(&sample,&QTimer::timeout,&window,[&,uiReady,seconds]{
            auto stats=window.player().stats();
            if(firstFrame<0&&(stats.value("displayed").toInt()>0||stats.value("callbackFrames").toULongLong()>0)){
                firstFrame=total.elapsed();playback.start();
            }
            if(firstFrame>=0&&(playback.elapsed()>=seconds*1000||(!window.player().playing()&&!window.player().busy()&&window.player().position()>=window.player().duration()-500))){
                stats["uiReadyMs"]=uiReady;stats["firstFrameMs"]=firstFrame;stats["sampleMs"]=playback.elapsed();stats["engine"]=window.player().engineVersion();stats["renderer"]=window.player().rendering();
                QTextStream(stdout)<<QJsonDocument::fromVariant(stats).toJson(QJsonDocument::Compact)<<Qt::endl;app.exit(0);
            }else if(firstFrame<0&&total.elapsed()>30000){qCritical("No displayed video frame within 30 seconds");app.exit(1);}
        });sample.start();
    }
    int result=app.exec();app.removeEventFilter(&opens);return result;
}
