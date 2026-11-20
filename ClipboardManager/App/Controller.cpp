#include "Controller.hpp"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStyleHints>
#include <QScopedValueRollback>
#include <QUrl>
#include <algorithm>

namespace {
constexpr auto SelfMime="application/x-clipdeck-restored";
std::string bytes(const QString& s){return s.toUtf8().toStdString();}
QString str(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
QByteArray byteArray(const std::string& s){return QByteArray(s.data(),static_cast<qsizetype>(s.size()));}
QString humanSize(quint64 n){
    if(n<1024)return QString::number(n)+" B";
    if(n<1024*1024)return QString::number(n/1024.0,'f',1)+" KB";
    return QString::number(n/(1024.0*1024.0),'f',1)+" MB";
}
QIcon trayIcon(){
    QPixmap pix(64,64);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
    p.setBrush(QColor("#157a73"));p.drawRoundedRect(QRectF(4,4,56,56),13,13);
    p.setPen(QPen(Qt::white,3));p.setBrush(Qt::NoBrush);p.drawRoundedRect(QRectF(18,18,28,34),4,4);
    p.setBrush(QColor("#157a73"));p.drawRoundedRect(QRectF(24,12,16,12),3,3);
    p.drawLine(24,32,40,32);p.drawLine(24,39,40,39);p.drawLine(24,46,35,46);
    return QIcon(pix);
}
bool isFileMime(const QString& name){
    auto n=name.toLower();
    for(auto* s:{"text/uri-list","x-special/gnome-copied-files","application/x-kde-cutselection",
        "filename","filegroupdescriptor","filecontents","shell idlist","preferred dropeffect","performed dropeffect","cf_hdrop","public.file-url","nsfilenamespboardtype"})
        if(n.contains(s))return true;
    return false;
}
bool hasMarker(const QStringList& formats){
    return std::any_of(formats.begin(),formats.end(),[](const QString& f){return clip::sensitiveMarker(bytes(f));});
}
QString exceptionText(const std::exception&){return QStringLiteral("Storage operation failed. Check permissions and available disk space.");}
}
Controller::Controller(QString directory,QObject* parent):
    QObject(parent),directory_(std::move(directory)),settings_(QDir(directory_).filePath("settings.ini"),QSettings::IniFormat),
    hotkey_(this),updater_(QDir(directory_).filePath("updates"),this){
    QDir().mkpath(directory_);
    QImageReader::setAllocationLimit(64);
    clipboard_=QGuiApplication::clipboard();
    lastSequence_=native::clipboardInfo().sequence;
    worker_=new QObject;worker_->moveToThread(&thread_);
    connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);thread_.start();
    const bool persistent=settings_.value("persistent",true).toBool();
    clip::Limits limits{static_cast<std::size_t>(settings_.value("maxEntries",100).toInt()),static_cast<std::uint64_t>(settings_.value("maxMiB",512).toInt())*clip::MiB};
    const auto persistentPath=storePath(true),sessionPath=storePath(false);
    QMetaObject::invokeMethod(worker_,[this,persistent,limits,persistentPath,sessionPath]{
        try{
            // Both locations are owned solely by this single-instance application.
            clip::fs::remove_all(sessionPath);
            if(!persistent)clip::fs::remove_all(persistentPath);
            store_=std::make_unique<clip::Store>(persistent?persistentPath:sessionPath,limits);
            QMetaObject::invokeMethod(this,[this]{ready_=true;setStatus("Clipboard history ready.");refresh();},Qt::QueuedConnection);
        }catch(const std::exception&){QMetaObject::invokeMethod(this,[this]{setStatus("Cannot initialize clipboard history storage.");},Qt::QueuedConnection);}
    },Qt::QueuedConnection);
    connect(clipboard_,&QClipboard::dataChanged,this,&Controller::clipboardChanged);
#ifdef Q_OS_MACOS
    macPoll_.setInterval(350);macPoll_.setTimerType(Qt::CoarseTimer);
    connect(&macPoll_,&QTimer::timeout,this,&Controller::clipboardChanged);macPoll_.start();
#endif
    queryTimer_.setSingleShot(true);queryTimer_.setInterval(150);
    connect(&queryTimer_,&QTimer::timeout,this,&Controller::refresh);
    connect(&hotkey_,&native::Hotkey::activated,this,&Controller::showUi);
    applyShortcut(preferences().value("shortcut").toString());
    menu_=std::make_unique<QMenu>();
    menu_->addAction("Show clipboard history",this,&Controller::showUi);
    auto* pause=menu_->addAction("Pause clipboard monitoring");pause->setCheckable(true);
    connect(pause,&QAction::toggled,this,&Controller::setPaused);
    connect(this,&Controller::pausedChanged,pause,[this,pause]{pause->setChecked(paused_);});
    menu_->addSeparator();menu_->addAction("Quit ClipDeck",this,&Controller::quit);
    tray_=std::make_unique<QSystemTrayIcon>(trayIcon());tray_->setToolTip("ClipDeck — Clipboard history");tray_->setContextMenu(menu_.get());
    connect(tray_.get(),&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
        if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)showUi();
    });
    if(QSystemTrayIcon::isSystemTrayAvailable())tray_->show();
    if(!settings_.contains("autostart")){
        QString error;const bool ok=native::setAutostart(true,error);
        settings_.setValue("autostart",ok);
        if(!ok)setStatus(error);
    }
    updater_.configure(settings_.value("updateFeed").toString());
}
Controller::~Controller(){
    closing_=true;macPoll_.stop();queryTimer_.stop();engine_.reset();tray_.reset();
    if(worker_&&thread_.isRunning()){
        const bool persistent=settings_.value("persistent",true).toBool();
        auto path=storePath(false);
        QMetaObject::invokeMethod(worker_,[this,persistent,path]{
            store_.reset();if(!persistent){std::error_code ec;clip::fs::remove_all(path,ec);}
        },Qt::BlockingQueuedConnection);
        thread_.quit();thread_.wait();
    }
}
clip::fs::path Controller::storePath(bool persistent) const {
    return clip::path(bytes(directory_))/(persistent?"history":"session-history");
}
QVariantMap Controller::preferences() const {
    return {{"maxEntries",settings_.value("maxEntries",100)},{"maxMiB",settings_.value("maxMiB",512)},
        {"persistent",settings_.value("persistent",true)},{"theme",settings_.value("theme","system")},
        {"shortcut",settings_.value("shortcut","Ctrl+Shift+V")},{"autostart",settings_.value("autostart",true)},
        {"updateFeed",settings_.value("updateFeed","")}};
}
void Controller::setStatus(QString s){status_=std::move(s);emit statusChanged();}
bool Controller::hasTray() const{return QSystemTrayIcon::isSystemTrayAvailable();}
void Controller::showUi(){
    if(!engine_){
        engine_=std::make_unique<QQmlApplicationEngine>();
        engine_->rootContext()->setContextProperty("controller",this);
        engine_->load(QUrl("qrc:/UI/Main.qml"));
        if(engine_->rootObjects().isEmpty()){engine_.reset();setStatus("Could not load QML interface.");return;}
    }
    if(auto* w=qobject_cast<QQuickWindow*>(engine_->rootObjects().value(0))){
        w->show();w->raise();w->requestActivate();
    }
    refresh();
}
void Controller::hideUi(){
    if(!engine_)return;
    if(auto* w=qobject_cast<QQuickWindow*>(engine_->rootObjects().value(0)))w->hide();
    QTimer::singleShot(0,this,[this]{
        if(engine_){auto* w=qobject_cast<QQuickWindow*>(engine_->rootObjects().value(0));if(w&&!w->isVisible()){engine_.reset();entries_.clear();query_.clear();}}
    });
}
void Controller::quit(){QCoreApplication::quit();}
void Controller::setPaused(bool p){if(paused_==p)return;paused_=p;emit pausedChanged();setStatus(p?"Clipboard monitoring paused.":"Clipboard monitoring resumed.");}
void Controller::applyShortcut(const QString& shortcut){
    QString error;auto seq=QKeySequence::fromString(shortcut,QKeySequence::PortableText);
    if(!hotkey_.registerShortcut(seq,error))setStatus(error);
}
void Controller::clipboardChanged(){
    if(closing_||writingClipboard_)return;
    if(capturing_){++epoch_;retryCapture_=true;return;}
    QScopedValueRollback<bool> guard(capturing_,true);
    struct Retry {
        Controller* self;bool& needed;
        ~Retry(){if(needed){needed=false;QTimer::singleShot(0,self,[s=self]{s->clipboardChanged();});}}
    } retry{this,retryCapture_};
    auto info=native::clipboardInfo();
    if(info.sequence&&info.sequence==lastSequence_)return;
    lastSequence_=info.sequence;
    if(info.secret||hasMarker(info.formats)){
        ++epoch_;
        QMetaObject::invokeMethod(worker_,[this]{if(store_)store_->protect({});},Qt::QueuedConnection);
        setStatus("Sensitive clipboard content excluded.");return;
    }
    const auto* mime=clipboard_->mimeData();
    if(mime&&mime->hasFormat(SelfMime))return;
    ++epoch_;
    // Release the previous file-snapshot lease even if the new clipboard is sensitive.
    QMetaObject::invokeMethod(worker_,[this]{if(store_)store_->protect({});},Qt::QueuedConnection);
    if(paused_||!ready_||settingsBusy_||!mime)return;
    if(hasMarker(mime->formats())){setStatus("Sensitive clipboard content excluded.");return;}
    if(pendingCaptures_>=2){setStatus("Clipboard history busy; this item was not recorded.");return;}
    clip::Capture capture; auto formats=mime->formats();
    if(formats.size()>static_cast<int>(clip::MaxFormats)){setStatus("Clipboard item has too many formats.");return;}
    const auto urls=mime->urls();
    if(urls.size()>static_cast<qsizetype>(clip::MaxNodes)){setStatus("Too many clipboard file references.");return;}
    bool localFiles=false;
    for(const auto& url:urls)if(url.isLocalFile()){localFiles=true;capture.files.push_back(clip::path(bytes(url.toLocalFile())));}
    if(localFiles&&static_cast<std::size_t>(urls.size())!=capture.files.size()){setStatus("Mixed local and remote file references are not snapshotted.");return;}
    capture.kind=localFiles?"Files":"Data";capture.preview=localFiles?bytes(QFileInfo(urls.first().toLocalFile()).fileName()):"";
    if(localFiles&&urls.size()>1)capture.preview+=" (+"+std::to_string(urls.size()-1)+")";
    quint64 total=0;bool hasImage=false;
    for(const auto& type:formats){
        if(type==SelfMime)continue;
        if(localFiles&&isFileMime(type))continue;
        if(!localFiles&&isFileMime(type)&&type!="text/uri-list"){
            // Virtual file promises cannot be snapshotted safely without the provider.
            setStatus("Unsupported virtual file clipboard item.");return;
        }
        if(type=="application/x-qt-image"){
            if(!mime->hasImage())continue;
            auto image=qvariant_cast<QImage>(mime->imageData());
            if(image.isNull()||static_cast<quint64>(image.sizeInBytes())>clip::MaxPayload){setStatus("Clipboard image exceeds capture memory limit.");return;}
            QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
            if(!image.save(&buffer,"PNG")){setStatus("Could not encode clipboard image.");return;}
            if(png.size()>static_cast<qsizetype>(clip::MaxPayload-total)){setStatus("Clipboard item exceeds capture memory limit.");return;}
            total+=png.size();
            if(std::none_of(capture.formats.begin(),capture.formats.end(),[](auto& f){return f.type=="image/png";}))
                capture.formats.push_back({"image/png",png.toStdString()});
            hasImage=true;continue;
        }
        auto data=mime->data(type);
        if(static_cast<quint64>(data.size())>clip::MaxPayload-total){setStatus("Clipboard item exceeds capture memory limit.");return;}
        total+=data.size();
        if(type.startsWith("image/"))hasImage=true;
        if(!localFiles&&type=="text/plain"){
            capture.preview=bytes(QString::fromUtf8(data.constData(),std::min<qsizetype>(data.size(),4096)).left(512));
            capture.kind="Text";
        }
        auto key=bytes(type);
        if(std::none_of(capture.formats.begin(),capture.formats.end(),[&](auto& f){return f.type==key;}))
            capture.formats.push_back({std::move(key),data.toStdString()});
    }
    if(!localFiles&&hasImage){capture.kind="Image";if(capture.preview.empty())capture.preview="Image";}
    if(!localFiles&&capture.preview.empty())capture.preview=capture.formats.empty()?"":capture.formats.front().type;
    // Reject changes/races and newly observed secret markers before anything is sent to storage.
    auto after=native::clipboardInfo();
    if(retryCapture_||after.secret||hasMarker(after.formats)||(info.sequence&&after.sequence!=info.sequence)){setStatus("Clipboard changed or was marked sensitive during capture.");return;}
    if(capture.formats.empty()&&capture.files.empty())return;
    ++pendingCaptures_;
    QMetaObject::invokeMethod(worker_,[this,capture=std::move(capture)]() mutable {
        auto result=store_?store_->put(std::move(capture)):clip::Result{};
        QMetaObject::invokeMethod(this,[this,result=std::move(result)]{
            --pendingCaptures_;setStatus(result.ok?"Clipboard item saved.":str(result.message));refresh();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void Controller::search(const QString& q){query_=q.left(512);queryTimer_.start();}
void Controller::refresh(){
    if(!ready_||closing_||!engine_)return;
    if(refreshing_){refreshDirty_=true;return;}
    refreshing_=true;auto q=bytes(query_);
    QMetaObject::invokeMethod(worker_,[this,q]{
        auto entries=store_?store_->search(q):std::vector<clip::Entry>{};
        auto count=store_?store_->entries().size():0;auto used=store_?store_->used():0;
        QMetaObject::invokeMethod(this,[this,entries=std::move(entries),count,used]() mutable{finishRefresh(std::move(entries),count,used);},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void Controller::finishRefresh(std::vector<clip::Entry> records,std::size_t count,std::uint64_t used){
    if(!engine_){refreshing_=false;refreshDirty_=false;return;}
    entries_.clear();
    for(auto& e:records){
        QStringList formats;for(auto& f:e.formats)formats.push_back(str(f));
        entries_.push_back(QVariantMap{{"id",str(e.id)},{"preview",str(e.preview)},{"kind",str(e.kind)},
            {"size",humanSize(e.bytes)},{"formats",formats.join(", ")},{"date",QDateTime::fromMSecsSinceEpoch(e.timestamp).toString("MMM d, HH:mm:ss")}});
    }
    summary_=QString::number(static_cast<qulonglong>(count))+" entries · "+humanSize(used);
    refreshing_=false;emit entriesChanged();
    if(refreshDirty_){refreshDirty_=false;refresh();}
}
void Controller::restore(const QString& id){
    if(!ready_||settingsBusy_||restoring_)return;
    const auto generation=epoch_;auto key=bytes(id);restoring_=true;
    QMetaObject::invokeMethod(worker_,[this,key,generation]{
        auto data=store_?store_->restore(key):std::optional<clip::Restored>{};
        if(data&&store_)store_->protect(data->files.empty()?std::string{}:key);
        QMetaObject::invokeMethod(this,[this,generation,data=std::move(data)]() mutable{
            if(!data||generation!=epoch_){
                restoring_=false;setStatus("Could not restore: clipboard or history changed.");
                if(generation!=epoch_)QMetaObject::invokeMethod(worker_,[this]{if(store_)store_->protect({});},Qt::QueuedConnection);
                return;
            }
            auto* mime=new QMimeData;
            QImage image;
            for(auto& f:data->formats){
                auto type=str(f.type);auto payload=byteArray(f.bytes);mime->setData(type,payload);
                if(image.isNull()&&type.startsWith("image/"))image=QImage::fromData(payload);
            }
            if(!image.isNull())mime->setImageData(image);
            if(!data->files.empty()){
                QList<QUrl> urls;QByteArray gnome="copy\n";
                for(auto& f:data->files){auto url=QUrl::fromLocalFile(str(clip::utf8(f)));urls.push_back(url);gnome+=url.toEncoded()+"\n";}
                mime->setUrls(urls);
#ifdef Q_OS_LINUX
                mime->setData("x-special/gnome-copied-files",gnome);mime->setData("application/x-kde-cutselection","0");
#endif
            }
            mime->setData(SelfMime,"1");writingClipboard_=true;clipboard_->setMimeData(mime);writingClipboard_=false;
            lastSequence_=native::clipboardInfo().sequence;
            restoring_=false;setStatus("Restored to system clipboard. Paste normally.");hideUi();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void Controller::remove(const QString& id){
    if(!ready_||settingsBusy_)return;auto key=bytes(id);
    QMetaObject::invokeMethod(worker_,[this,key]{
        bool ok=store_&&store_->remove(key);
        QMetaObject::invokeMethod(this,[this,ok]{setStatus(ok?"History entry deleted.":"Entry is in use or could not be deleted.");refresh();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void Controller::clear(){
    if(!ready_||settingsBusy_)return;
    QMetaObject::invokeMethod(worker_,[this]{
        bool ok=store_&&store_->clear();
        QMetaObject::invokeMethod(this,[this,ok]{setStatus(ok?"Clipboard history cleared.":"History cleared except for the active file snapshot or an unavailable entry.");refresh();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void Controller::applySettings(int count,int maxMiB,bool persistent,QString theme,QString shortcut,bool autostart,QString feed){
    if(settingsBusy_||!ready_)return;
    if(count<1||count>10000||maxMiB<1||maxMiB>8192){setStatus("Choose 1–10000 entries and 1–8192 MB.");return;}
    if(theme!="system"&&theme!="light"&&theme!="dark"){setStatus("Invalid theme.");return;}
    auto sequence=QKeySequence::fromString(shortcut,QKeySequence::PortableText);
    if(sequence.isEmpty()||sequence.count()!=1){setStatus("Enter a single shortcut, e.g. Ctrl+Shift+V.");return;}
    if(!feed.trimmed().isEmpty()&&(QUrl(feed).scheme()!="https"||QUrl(feed).host().isEmpty())){setStatus("Update feed must use HTTPS.");return;}
    settingsBusy_=true;emit settingsBusyChanged();
    bool oldPersistent=settings_.value("persistent",true).toBool();
    auto oldPath=storePath(oldPersistent),newPath=storePath(persistent);
    clip::Limits limits{static_cast<std::size_t>(count),static_cast<std::uint64_t>(maxMiB)*clip::MiB};
    QMetaObject::invokeMethod(worker_,[this,count,maxMiB,persistent,theme,shortcut,autostart,feed,oldPersistent,oldPath,newPath,limits]{
        QString error;
        try{
            if(!store_)throw std::runtime_error("Storage unavailable");
            if(persistent!=oldPersistent&&!store_->protectedId().empty())error="Paste or replace the active file clipboard before changing persistence.";
            else if(!store_->setLimits(limits))error="Cannot set the limit while a restored file entry is active.";
            else if(persistent!=oldPersistent){
                store_.reset();
                std::error_code ec;clip::fs::remove_all(newPath,ec);
                if(ec)throw std::runtime_error("Could not clear destination");
                clip::fs::rename(oldPath,newPath);
                store_=std::make_unique<clip::Store>(newPath,limits);
            }
        }catch(const std::exception& ex){
            error=exceptionText(ex);
            if(!store_){try{if(clip::fs::exists(newPath)&&!clip::fs::exists(oldPath))clip::fs::rename(newPath,oldPath);store_=std::make_unique<clip::Store>(oldPath,limits);}catch(...){}}
        }
        QMetaObject::invokeMethod(this,[this,count,maxMiB,persistent,theme,shortcut,autostart,feed,error]{
            settingsBusy_=false;emit settingsBusyChanged();
            if(!error.isEmpty()){setStatus(error);refresh();return;}
            settings_.setValue("maxEntries",count);settings_.setValue("maxMiB",maxMiB);settings_.setValue("persistent",persistent);
            settings_.setValue("theme",theme);settings_.setValue("shortcut",shortcut);settings_.setValue("updateFeed",feed);
            QString autoError;auto ok=native::setAutostart(autostart,autoError);settings_.setValue("autostart",ok?autostart:native::autostartEnabled());
            settings_.sync();setStatus(ok?"Settings saved.":autoError);applyShortcut(shortcut);updater_.configure(feed);emit preferencesChanged();refresh();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
