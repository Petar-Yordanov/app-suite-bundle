#include "Controller.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QScreen>
#include <QWindow>
#include <QPainter>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QDir>
#include <QStyleHints>
#include <algorithm>

namespace {
class Provider : public QQuickImageProvider {
public:
    explicit Provider(QImage image):QQuickImageProvider(QQuickImageProvider::Image),image_(std::move(image)){image_.setDevicePixelRatio(1);}
    QImage requestImage(const QString&,QSize* s,const QSize& requested) override{
        if(s)*s=image_.size();
        if(requested.isValid()&&(requested.width()<image_.width()||requested.height()<image_.height()))return image_.scaled(requested,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        return image_;
    }
private: QImage image_;
};
QIcon icon(){QPixmap p(32,32);p.fill(Qt::transparent);QPainter a(&p);a.setRenderHint(QPainter::Antialiasing);a.setPen(QPen(QColor("#80baff"),3));a.setBrush(QColor("#233346"));a.drawRoundedRect(3,3,26,26,5,5);a.drawRect(10,10,12,12);return QIcon(p);}
const QStringList defaults={"Ctrl+Alt+1","Ctrl+Alt+2","Ctrl+Alt+3"};
}
Controller::Controller(QString data,bool testing):data_(std::move(data)),settings_(data_+"/settings.ini",QSettings::IniFormat),testing_(testing){
    pool_.setMaxThreadCount(1);pool_.setExpiryTimeout(1000);
    keys_=settings_.value("shortcuts",defaults).toStringList();if(keys_.size()!=3)keys_=defaults;
    setTheme(theme());
    if(testing_)return;
    if(isPortal()){
#ifdef Q_OS_LINUX
        portal_=std::make_unique<Portal>();
        connect(portal_.get(),&Portal::activated,this,[this](int mode){command(mode==0?"region":mode==1?"screen":"window");});
        connect(portal_.get(),&Portal::status,this,&Controller::status);
#endif
    }else{
        for(int i=0;i<3;++i){hotkeys_[i]=std::make_unique<shot::native::Hotkey>();connect(hotkeys_[i].get(),&shot::native::Hotkey::activated,this,[this,i]{command(i==0?"region":i==1?"screen":"window");});}
    }
    bind(keys_);
    if(!settings_.contains("startupConfigured")&&!QCoreApplication::arguments().contains("--benchmark-idle"))setAutostart(true);
    if(!updateFeed().isEmpty())updater();
    if(QSystemTrayIcon::isSystemTrayAvailable()){
        menu_=std::make_unique<QMenu>();tray_=std::make_unique<QSystemTrayIcon>(icon());
        tray_->setToolTip("Screenshot Tool");
        menu_->addAction("Capture region",this,[this]{command("region");});
        menu_->addAction("Capture screen",this,[this]{command("screen");});
        menu_->addAction("Capture window",this,[this]{command("window");});
        menu_->addSeparator();menu_->addAction("Settings",this,&Controller::showSettings);
        menu_->addAction("Cancel capture",this,&Controller::cancel);
        menu_->addAction("Quit",qApp,&QCoreApplication::quit);
        tray_->setContextMenu(menu_.get());
        connect(tray_.get(),&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason r){if(r==QSystemTrayIcon::Trigger||r==QSystemTrayIcon::DoubleClick)showSettings();});
        tray_->show();
    }
}
Controller::~Controller(){++generation_;pool_.waitForDone();delete engine_;tray_.reset();menu_.reset();}
void Controller::status(QString text){status_=std::move(text);emit changed();if(tray_&&!engine_&&!status_.isEmpty())tray_->showMessage("Screenshot Tool",status_,QSystemTrayIcon::Information,2500);}
void Controller::setTheme(QString t){if(t!="light"&&t!="dark"&&t!="system")return;if(!testing_)settings_.setValue("theme",t);QGuiApplication::styleHints()->setColorScheme(t=="light"?Qt::ColorScheme::Light:t=="dark"?Qt::ColorScheme::Dark:Qt::ColorScheme::Unknown);emit changed();}
void Controller::setCursor(bool v){settings_.setValue("cursor",v);emit changed();}
void Controller::setDelay(int v){settings_.setValue("delay",std::clamp(v,0,60));emit changed();}
void Controller::setAutostart(bool v){QString err;if(shot::native::setAutostart(v,err)){settings_.setValue("startupConfigured",true);emit changed();}else status(err);}
Updater* Controller::updater(){if(!updater_){updater_=std::make_unique<Updater>(data_+"/updates");updater_->configure(updateFeed());}return updater_.get();}
void Controller::setUpdateFeed(QString feed){settings_.setValue("updateFeed",feed.trimmed());updater()->configure(feed);emit changed();}
bool Controller::bind(QStringList keys){
#ifdef Q_OS_LINUX
    if(portal_){portal_->shortcuts(keys);return true;}
#endif
    if(testing_)return true;
    for(auto& k:hotkeys_)k->unbind();
    QStringList errors;
    for(int i=0;i<3;++i){QString err;if(!hotkeys_[i]->bind(QKeySequence::fromString(keys[i],QKeySequence::PortableText),err))errors<<keys[i]+": "+err;}
    if(!errors.isEmpty()){status(errors.join("\n"));return false;}return true;
}
void Controller::setShortcuts(QString r,QString s,QString w){
    QStringList keys={r,s,w};
    for(auto& key:keys){auto seq=QKeySequence::fromString(key,QKeySequence::PortableText);if(seq.isEmpty()||seq.count()!=1||seq[0].key()==Qt::Key_unknown||!(seq[0].keyboardModifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){status("Use a single key combination with Ctrl, Alt or Meta.");return;}key=seq.toString(QKeySequence::PortableText);}
    if(keys[0]==keys[1]||keys[0]==keys[2]||keys[1]==keys[2]){status("Shortcuts must be different.");return;}
    if(!bind(keys)){bind(keys_);return;}keys_=keys;settings_.setValue("shortcuts",keys_);status("Shortcuts saved.");
}
QStringList Controller::screenNames() const{QStringList out;for(auto& t:screens_)out<<QString("%1 (%2 x %3)").arg(t.name).arg(t.native.width()).arg(t.native.height());return out;}
QStringList Controller::windowNames() const{QStringList out;for(auto& t:windows_)out<<t.name;return out;}
void Controller::refreshTargets(){screens_=shot::native::screens();windows_=shot::native::windows();emit changed();}
int Controller::activeScreen() const{
    auto point=QCursor::pos();for(int i=0;i<screens_.size();++i)if(screens_[i].logical.contains(point))return i;return 0;
}
void Controller::closeUi(){
    if(!engine_)return;
    auto* old=engine_;engine_=nullptr;view_.clear();
    for(auto* root:old->rootObjects())if(auto* w=qobject_cast<QWindow*>(root))w->hide();
    old->deleteLater();
}
bool Controller::showUi(QString name,QRect screenGeometry){
    closeUi();engine_=new QQmlApplicationEngine(this);engine_->rootContext()->setContextProperty("shot",this);
    if(!image_.isNull())engine_->addImageProvider("capture",new Provider(image_));
    view_=name;engine_->load(QUrl("qrc:/UI/"+name+".qml"));
    if(engine_->rootObjects().isEmpty()){closeUi();status("Could not load interface.");return false;}
    auto* w=qobject_cast<QWindow*>(engine_->rootObjects().first());if(!w){closeUi();return false;}
    if(name=="Overlay"){
        QScreen* screen=nullptr;for(auto* s:QGuiApplication::screens())if(s->geometry()==screenGeometry){screen=s;break;}
        if(!screen&&screenGeometry.isValid()){closeUi();status("Monitor configuration changed. Retry capture.");return false;}
        if(!screen)screen=QGuiApplication::primaryScreen();if(screen){w->setScreen(screen);w->setGeometry(screen->geometry());}
        w->showFullScreen();
    }else w->show();
    w->raise();w->requestActivate();return true;
}
void Controller::showSettings(){if(busy_)cancel();image_={};refreshTargets();showUi("Settings");}
void Controller::command(const QString& mode){
    if(mode=="cancel"){cancel();return;}if(mode=="show"){showSettings();return;}
    if(mode=="window"&&!isPortal()){showSettings();status("Select the application window and click Capture.");return;}
    if(mode=="region")capture(0);else if(mode=="screen")capture(1);else if(mode=="window")capture(2);
}
void Controller::capture(int mode,int index){
    if(mode<0||mode>2||busy_||inFlight_){status("A capture is already running.");return;}
    if(!shot::native::requestPermission()){status("Allow Screen Recording in system settings, then retry (restart if required).");return;}
    if(mode!=2){if(index<0){screens_=shot::native::screens();index=activeScreen();}}
    shot::Target target;
    if(!isPortal()){
        auto& list=mode==2?windows_:screens_;
        if(index<0||index>=list.size()){status("Select an available capture source.");return;}
        target=list[index];
    }
    const int settle=(engine_||(menu_&&menu_->isVisible()))?150:0;
    closeUi();image_={};busy_=true;status_="Waiting for capture...";emit changed();const auto gen=++generation_;const bool showCursor=cursor();
    QTimer::singleShot(settle+1000*delay(),this,[this,target,gen,mode,showCursor]{
        if(gen!=generation_)return;
#ifdef Q_OS_LINUX
        if(portal_){portal_->screenshot([this,gen,target](shot::Result result){captured(std::move(result),1,target,gen);});return;}
#endif
        inFlight_=true;auto* watcher=new QFutureWatcher<shot::Result>(this);
        connect(watcher,&QFutureWatcher<shot::Result>::finished,this,[this,watcher,gen,mode,target]{
            inFlight_=false;auto result=watcher->result();watcher->deleteLater();captured(std::move(result),mode,target,gen);
        });watcher->setFuture(QtConcurrent::run(&pool_,[target,showCursor]{return shot::native::capture(target,showCursor);}));
    });
}
void Controller::captured(shot::Result result,int mode,const shot::Target& target,quint64 gen){
    if(gen!=generation_)return;
    if(result.image.isNull()){busy_=false;status(result.error);return;}
    status_=result.error;image_=std::move(result.image);image_.setDevicePixelRatio(1);emit changed();
    if(mode==0){if(!showUi("Overlay",target.logical)){busy_=false;image_={};emit changed();}}
    else present();
}
void Controller::finishRegion(double x,double y,double w,double h,double vw,double vh){
    if(view_!="Overlay"||!busy_)return;
    auto selected=shot::crop(image_,QRectF(x,y,w,h),QSizeF(vw,vh));if(selected.isNull())return;
    auto gen=generation_;QTimer::singleShot(0,this,[this,selected=std::move(selected),gen]() mutable{
        if(gen!=generation_)return;closeUi();image_=std::move(selected);present();
    });
}
void Controller::present(){
    busy_=false;emit changed();
    if(!isPortal())copy();
    showUi("Preview");
    if(isPortal())QTimer::singleShot(200,this,[this,gen=generation_]{if(gen==generation_&&view_=="Preview")copy();});
}
void Controller::copy(){if(image_.isNull())return;QGuiApplication::clipboard()->setImage(image_);if(!status_.startsWith("Portal temporary")){status_="Copied to clipboard. No image file saved.";emit changed();}}
void Controller::save(QUrl url){QString error;if(shot::savePng(image_,url,error))status("PNG saved.");else status(error);}
void Controller::cancel(){++generation_;busy_=false;
#ifdef Q_OS_LINUX
    if(portal_)portal_->cancel();
#endif
    closeUi();image_={};status("Capture cancelled.");
}
void Controller::uiClosed(){auto gen=generation_;auto* engine=engine_;QTimer::singleShot(0,this,[this,gen,engine]{if(gen!=generation_||engine!=engine_)return;if(view_=="Overlay")cancel();else{closeUi();image_={};emit changed();}});}
bool Controller::testView(const QString& view,const QImage& image){image_=image;return showUi(view);}
