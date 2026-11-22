#include "Controller.hpp"
#include <QApplication>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <QStyleHints>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QUrl>
#ifdef Q_OS_LINUX
#include "../Platform/Portal.hpp"
#include <QQuickImageProvider>
#endif

using namespace rec;
namespace {
const QVariantMap defaults{
    {"fps",30},{"bitrate",12000},{"quality",1},{"height",1080},{"cursor",true},
    {"micEnabled",false},{"systemAudio",false},{"hardware",true},{"mic",QString()},
    {"theme","System"},{"shortcut","Ctrl+Alt+R"},{"pauseShortcut","Ctrl+Alt+P"},{"updateUrl",QString()}
};
QVariantList targets(const QList<Target>& values){
    QVariantList list;
    for(const auto& t:values)list.append(QVariantMap{{"name",t.native.isEmpty()?t.name:t.name+"  ·  "+QString::number(t.native.width())+" × "+QString::number(t.native.height())}});
    return list;
}
#ifdef Q_OS_LINUX
class PreviewProvider : public QQuickImageProvider {
    QImage preview_;
public:
    explicit PreviewProvider(QImage image):QQuickImageProvider(QQuickImageProvider::Image),preview_(std::move(image)){}
    QImage requestImage(const QString&,QSize* size,const QSize&)override{if(size)*size=preview_.size();return preview_;}
};
#endif
}
Controller::Controller(const QString& data)
    :prefs_(data+"/settings.ini",QSettings::IniFormat),updater_(data+"/updates"),recorder_(this),
     toggleKey_(this),pauseKey_(this),tray_(this){
    for(auto i=defaults.begin();i!=defaults.end();++i)if(!prefs_.contains(i.key()))prefs_.setValue(i.key(),i.value());
    if(!prefs_.contains("output")){
        auto movies=QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if(movies.isEmpty())movies=QDir::home().filePath("Videos");
        prefs_.setValue("output",QDir(movies).filePath("ScreenRecorder"));
    }
    // Restore safe bounds if a settings file was edited by hand.
    if(!validSettings(prefs_.value("fps").toInt(),prefs_.value("bitrate").toInt(),
                      prefs_.value("quality").toInt(),prefs_.value("height").toInt()))
        for(const auto& key:{"fps","bitrate","quality","height"})prefs_.setValue(key,defaults.value(key));
    connect(&toggleKey_,&native::Hotkey::activated,this,[this]{command("toggle");});
    connect(&pauseKey_,&native::Hotkey::activated,this,[this]{command("pause");});
    bindShortcuts();
    tray_.setIcon(icon(false));tray_.setToolTip("Screen Recorder");
    durationAction_=menu_.addAction("Idle");durationAction_->setEnabled(false);
    recordAction_=menu_.addAction("New recording",this,[this]{command("toggle");});
    pauseAction_=menu_.addAction("Pause",this,&Controller::pauseResume);
    menu_.addAction("Open settings / capture",this,&Controller::show);
    menu_.addAction("Open recordings",this,&Controller::openOutput);
    menu_.addSeparator();quitAction_=menu_.addAction("Quit",this,&Controller::quit);
    tray_.setContextMenu(&menu_);
    connect(&tray_,&QSystemTrayIcon::activated,this,[this](auto reason){
        if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)show();
    });
    if(QSystemTrayIcon::isSystemTrayAvailable())tray_.show();
    connect(&recorder_,&Recorder::changed,this,[this]{
        const auto old=previousState_;previousState_=state();
        uiError_.clear();updateTray();emit changed();
        if(quitPending_&&recorder_.state()==State::Idle)QCoreApplication::quit();
        else if(old!=0&&state()==0){
            if(tray_.isVisible())tray_.showMessage("Screen Recorder",recorder_.status(),QSystemTrayIcon::Information,4000);
            else show();
        }else if(old!=2&&state()==2&&!tray_.isVisible())show();
    });
    connect(&discovery_,&QFutureWatcher<Devices>::finished,this,[this]{
        devices_=discovery_.result();
        auto old=prefs_.value("mic").toString();
        bool found=false;for(const auto& d:devices_.microphones)if(d.id==old)found=true;
        if(!found)prefs_.setValue("mic",devices_.microphones.isEmpty()?QString():devices_.microphones.first().id);
        emit settingsChanged();emit devicesChanged();
    });
    connect(qApp->styleHints(),&QStyleHints::colorSchemeChanged,this,[this]{emit settingsChanged();});
    updater_.configure(prefs_.value("updateUrl").toString());
    updateTray();
}
Controller::~Controller(){discovery_.waitForFinished();tray_.hide();destroyOverlay();}
QVariantMap Controller::settings()const{
    QVariantMap out;
    for(auto i=defaults.begin();i!=defaults.end();++i)out.insert(i.key(),prefs_.value(i.key()));
    out["output"]=prefs_.value("output");out["autostart"]=native::autostartEnabled();
    return out;
}
QVariantList Controller::screens()const{return targets(screens_);}
QVariantList Controller::windows()const{return targets(windows_);}
QVariantList Controller::microphones()const{
    QVariantList out;for(const auto& d:devices_.microphones)out.append(QVariantMap{{"name",d.name},{"id",d.id}});return out;
}
QString Controller::duration()const{
    auto seconds=recorder_.durationMs()/1000;
    return QString("%1:%2:%3").arg(seconds/3600,2,10,QChar('0')).arg((seconds/60)%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
}
QString Controller::platformLimits()const{
    if(native::wayland())return "Wayland: choose a single monitor or window in the compositor's permission dialog. Region selection uses a preview. Available sources and cursor modes depend on the portal. Bind screenrecorder --toggle / --pause / --stop in desktop shortcuts.";
#ifdef Q_OS_WIN
    return "Protected/minimized windows may not be capturable. System audio uses the default playback device.";
#elif defined(Q_OS_MACOS)
    return "Screen Recording and microphone permissions are required. Window capture includes audio associated with the selected content where macOS permits it.";
#else
    return "X11 capture; microphone and desktop audio use PulseAudio or PipeWire's Pulse compatibility service.";
#endif
}
bool Controller::dark()const{
    const auto choice=prefs_.value("theme").toString();
    return choice=="Dark"||(choice=="System"&&qApp->styleHints()->colorScheme()==Qt::ColorScheme::Dark);
}
void Controller::bindShortcuts(){
    QString a,b;toggleKey_.bind(QKeySequence::fromString(prefs_.value("shortcut").toString(),QKeySequence::PortableText),a);
    pauseKey_.bind(QKeySequence::fromString(prefs_.value("pauseShortcut").toString(),QKeySequence::PortableText),b);
    hotkeyStatus_=a.isEmpty()&&b.isEmpty()?"Global shortcuts registered.":QString("Record: %1  Pause: %2").arg(a.isEmpty()?"OK":a,b.isEmpty()?"OK":b);
    emit settingsChanged();
}
void Controller::setSetting(const QString& key,const QVariant& value){
    if(recorder_.state()!=State::Idle||starting_||selectionPending_)return;
    if(key=="autostart"){
        QString message;if(!native::setAutostart(value.toBool(),message))error(message);
        emit settingsChanged();return;
    }
    if(key=="output"){
        if(value.toString().trimmed().isEmpty()){error("Choose a recording directory.");return;}
        prefs_.setValue(key,QDir::cleanPath(value.toString()));emit settingsChanged();return;
    }
    if(!defaults.contains(key))return;
    QVariant normalized=value;
    if(key=="fps"||key=="bitrate"||key=="height"||key=="quality"){
        auto c=config();const int n=value.toInt();
        if(key=="fps")c.fps=n;else if(key=="bitrate")c.bitrate=n;else if(key=="quality")c.quality=n;else c.height=n;
        if(!validSettings(c.fps,c.bitrate,c.quality,c.height))return;
        normalized=n;
    }
    if(key=="theme"&&!QStringList{"System","Light","Dark"}.contains(value.toString()))return;
    if(key=="shortcut"||key=="pauseShortcut"){
        auto seq=QKeySequence::fromString(value.toString(),QKeySequence::PortableText);
        if(seq.count()!=1||seq.isEmpty()){error("Enter one shortcut, e.g. Ctrl+Alt+R.");return;}
        normalized=seq.toString(QKeySequence::PortableText);
        if(normalized==prefs_.value(key=="shortcut"?"pauseShortcut":"shortcut")){error("Shortcuts must be distinct.");return;}
        auto& hotkey=key=="shortcut"?toggleKey_:pauseKey_;
        if(!native::wayland()){
            QString message;
            if(!hotkey.bind(seq,message)){
                QString ignored;hotkey.bind(QKeySequence::fromString(prefs_.value(key).toString(),QKeySequence::PortableText),ignored);
                error(message);return;
            }
        }
    }
    prefs_.setValue(key,normalized);prefs_.sync();
    if(key=="shortcut"||key=="pauseShortcut")bindShortcuts();
    if(key=="updateUrl")updater_.configure(normalized.toString());
    emit settingsChanged();
}
void Controller::setOutputUrl(const QUrl& url){if(url.isLocalFile())setSetting("output",url.toLocalFile());}
Config Controller::config()const{
    Config c;c.fps=prefs_.value("fps").toInt();c.bitrate=prefs_.value("bitrate").toInt();c.quality=prefs_.value("quality").toInt();
    c.height=prefs_.value("height").toInt();c.cursor=prefs_.value("cursor").toBool();c.microphone=prefs_.value("micEnabled").toBool();
    c.systemAudio=prefs_.value("systemAudio").toBool();c.hardware=prefs_.value("hardware").toBool();
    c.mic=prefs_.value("mic").toString();c.output=prefs_.value("output").toString();return c;
}
void Controller::refresh(){
    if(recorder_.state()!=State::Idle||starting_)return;
    if(native::wayland()){
        screens_={{0,"Choose a screen in the system dialog",{},{},false}};
        windows_={{0,"Choose a window in the system dialog",{},{},true}};
    }else {screens_=native::screens();windows_=native::windows();}
    emit targetsChanged();
    if(!discovering()){
        auto path=ffmpegPath();
        discovery_.setFuture(QtConcurrent::run([path]{return discoverDevices(path);}));
        emit devicesChanged();
    }
}
void Controller::loadWindow(){
    if(main_)return;
    engine_=std::make_unique<QQmlApplicationEngine>();
    engine_->rootContext()->setContextProperty("recorder",this);
    engine_->rootContext()->setContextProperty("updater",&updater_);
    engine_->load(QUrl("qrc:/Main.qml"));
    if(!engine_->rootObjects().isEmpty())main_=qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
    if(!main_)error("Could not load recording interface.");
}
void Controller::show(){
    if(selectionPending_||starting_)return;
    loadWindow();if(!main_)return;
    main_->show();main_->raise();main_->requestActivate();refresh();
}
void Controller::closeUi(){
    if(main_)main_->hide();
    // Destroy the QML engine only after the current QML handler returns.
    QTimer::singleShot(0,this,[this]{
        if(main_&&!main_->isVisible()){main_.clear();engine_.reset();}
    });
}
void Controller::begin(int mode,int index){
    if(recorder_.state()!=State::Idle||starting_||selectionPending_)return;
    uiError_.clear();emit changed();
    auto values=mode==2?windows_:screens_;
    if(mode<0||mode>2||index<0||index>=values.size()){error("Choose a capture source.");return;}
    if(discovering()&&(config().microphone||config().systemAudio)){error("Wait for audio device discovery.");return;}
#ifdef Q_OS_LINUX
    if(native::wayland()){choosePortal(mode);return;}
#endif
    auto selected=values[index];
    if(mode==1){showOverlay(selected);return;}
    start(selected,{});
}
#ifdef Q_OS_LINUX
void Controller::choosePortal(int mode){
    selectionPending_=true;if(main_)main_->hide();
    portal_=std::make_shared<PortalSession>();
    auto* source=portal_.get();
    connect(source,&PortalSession::failed,this,[this,source](QString message){
        if(portal_.get()!=source||!selectionPending_)return;
        selectionPending_=false;destroyOverlay();portal_.reset();show();error(message);
    },Qt::QueuedConnection);
    connect(source,&PortalSession::ready,this,[this,source,mode]{
        if(portal_.get()!=source||!selectionPending_)return;
        const auto dimensions=portal_->size();
        Target target{0,"Portal source",QRect(QPoint(),dimensions),QRect(QPoint(),dimensions),mode==2};
        if(mode==1){showPortalRegion(target);return;}
        selectionPending_=false;start(target,{});
    },Qt::QueuedConnection);
    source->choose(mode==2,config().cursor);
}
void Controller::showPortalRegion(const Target& target){
    selecting_=target;
    overlayEngine_=std::make_unique<QQmlApplicationEngine>();
    overlayEngine_->rootContext()->setContextProperty("recorder",this);
    overlayEngine_->addImageProvider("portal",new PreviewProvider(portal_->preview()));
    overlayEngine_->load(QUrl("qrc:/PortalRegion.qml"));
    if(!overlayEngine_->rootObjects().isEmpty())overlay_=qobject_cast<QQuickWindow*>(overlayEngine_->rootObjects().first());
    if(!overlay_){selectionPending_=false;portal_.reset();show();error("Cannot open portal region selector.");return;}
    overlay_->show();overlay_->raise();overlay_->requestActivate();
}
#endif
void Controller::showOverlay(const Target& target){
    selecting_=target;selectionPending_=true;
    if(main_)main_->hide();
    overlayEngine_=std::make_unique<QQmlApplicationEngine>();
    overlayEngine_->rootContext()->setContextProperty("recorder",this);
    overlayEngine_->load(QUrl("qrc:/Region.qml"));
    if(!overlayEngine_->rootObjects().isEmpty())overlay_=qobject_cast<QQuickWindow*>(overlayEngine_->rootObjects().first());
    if(!overlay_){selectionPending_=false;error("Unable to open region selector.");show();return;}
    for(auto* screen:QGuiApplication::screens())if(screen->geometry()==target.logical){overlay_->setScreen(screen);break;}
    overlay_->setGeometry(target.logical);overlay_->show();overlay_->raise();overlay_->requestActivate();
}
void Controller::destroyOverlay(){if(overlay_)overlay_->hide();overlay_.clear();overlayEngine_.reset();}
void Controller::cancelRegion(){
    if(!selectionPending_)return;
    selectionPending_=false;if(overlay_)overlay_->hide();
    QTimer::singleShot(0,this,[this]{
        destroyOverlay();
#ifdef Q_OS_LINUX
        portal_.reset();
#endif
        show();
    });
}
void Controller::finishRegion(double x,double y,double w,double h,double logicalW,double logicalH){
    if(!selectionPending_)return;
    const auto area=mapRegion(x,y,w,h,logicalW,logicalH,selecting_.native.width(),selecting_.native.height());
    if(area.width<2||area.height<2){cancelRegion();return;}
    auto target=selecting_;selectionPending_=false;
    if(overlay_)overlay_->hide();
    QTimer::singleShot(0,this,[this,target,area]{
        destroyOverlay();start(target,QRect(area.x,area.y,area.width,area.height));
    });
}
void Controller::start(Target target,QRect region){
    if(starting_||recorder_.state()!=State::Idle)return;
    const auto settings=config();const auto audio=devices_.systemSource;
    starting_=true;closeUi();
    // Allow compositor to remove UI and selection overlay before opening capture.
    auto launch=[this,target,region,settings,audio
#ifdef Q_OS_LINUX
        ,session=portal_
#endif
    ]{
      QTimer::singleShot(350,this,[this,target,region,settings,audio
#ifdef Q_OS_LINUX
        ,session
#endif
      ]{
        if(!starting_)return;
        starting_=false;recorder_.start(target,region,settings,audio
#ifdef Q_OS_LINUX
            ,session
#endif
        );
#ifdef Q_OS_LINUX
        portal_.reset();
#endif
        if(recorder_.state()==State::Idle)show();
      });
    };
#ifdef Q_OS_MACOS
    if(settings.microphone){
        native::requestMicrophonePermission(this,[this,launch](bool allowed){
            if(!starting_)return;
            if(allowed)launch();
            else {starting_=false;show();error("Microphone permission denied. Allow the app in Privacy & Security.");}
        });return;
    }
#endif
    launch();
}
void Controller::command(const QString& cmd){
    if(cmd=="show")show();
    else if(cmd=="toggle"){
        if(selectionPending_)cancelRegion();
        else if(starting_)stop();
        else if(recorder_.state()==State::Idle)show();
        else stop();
    }else if(cmd=="pause")pauseResume();
    else if(cmd=="stop")stop();
}
void Controller::pauseResume(){recorder_.togglePause();}
void Controller::stop(){
    if(starting_){starting_=false;
#ifdef Q_OS_LINUX
        portal_.reset();
#endif
        show();return;}
    recorder_.stop();
}
void Controller::openOutput(){
    auto output=prefs_.value("output").toString();
    if(!QDir().mkpath(output)||!QDesktopServices::openUrl(QUrl::fromLocalFile(output)))error("Cannot open output directory.");
}
void Controller::openLast(){if(!lastFile().isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(lastFile()));}
void Controller::quit(){
    if(recorder_.state()==State::Idle){starting_=false;QCoreApplication::quit();return;}
    quitPending_=true;recorder_.stop();
}
void Controller::error(QString message){uiError_=std::move(message);emit changed();}
QIcon Controller::icon(bool recording)const{
    QPixmap pixels(32,32);pixels.fill(Qt::transparent);QPainter p(&pixels);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);p.setBrush(recording?QColor("#e84949"):QColor("#7389a8"));
    p.drawRoundedRect(QRect(3,6,26,20),4,4);p.setBrush(QColor("#ffffff"));
    if(recording)p.drawEllipse(QRect(12,11,8,8));else p.drawRoundedRect(QRect(11,11,10,10),2,2);
    return QIcon(pixels);
}
void Controller::updateTray(){
    const auto s=recorder_.state();const bool idle=s==State::Idle;
    durationAction_->setText(idle?"Idle":duration()+" · "+recorder_.status());
    recordAction_->setText(idle?"New recording":"Stop recording");recordAction_->setEnabled(idle||canStop(s));
    pauseAction_->setText(s==State::Paused?"Resume":"Pause");pauseAction_->setEnabled(canPause(s)||canResume(s));
    static const auto readyIcon=icon(false),recordingIcon=icon(true);
    tray_.setIcon(idle?readyIcon:recordingIcon);tray_.setToolTip(idle?"Screen Recorder":duration()+" · "+recorder_.status());
}
