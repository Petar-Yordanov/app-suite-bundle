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
QString policyName(const QString& f){
    for(auto* name:{"CanIncludeInClipboardHistory","CanUploadToCloudClipboard"}){
        const auto wrapped=QStringLiteral("application/x-qt-windows-mime;value=\"")+QString::fromLatin1(name)+"\"";
        if(f.compare(QString::fromLatin1(name),Qt::CaseInsensitive)==0||
           f.compare(wrapped,Qt::CaseInsensitive)==0||f.compare(wrapped+";index=0",Qt::CaseInsensitive)==0)
            return QString::fromLatin1(name);
    }
    return {};
}
bool hasMarker(const QStringList& formats,const native::ClipboardInfo& info){
    return std::any_of(formats.begin(),formats.end(),[&](const QString& f){
        if(!clip::sensitiveMarker(bytes(f)))return false;
        const auto name=policyName(f);
        if(!name.isEmpty()&&info.allowedPolicyFormats.contains(name,Qt::CaseInsensitive))return false;
        return true;
    });
}
QString exceptionText(const std::exception&){return QStringLiteral("Storage operation failed. Check permissions and available disk space.");}
}
Controller::Controller(QString directory,QObject* parent):
    QObject(parent),directory_(std::move(directory)),settings_(QDir(directory_).filePath("settings.ini"),QSettings::IniFormat),
    hotkey_(this),updater_(QDir(directory_).filePath("updates"),this){
    QDir().mkpath(directory_);
    QImageReader::setAllocationLimit(64);
    clipboard_=QGuiApplication::clipboard();
    lastSequence_=native::clipboardSequence();
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
    captureTimer_.setSingleShot(true);
    connect(&captureTimer_,&QTimer::timeout,this,&Controller::clipboardChanged);
    connect(clipboard_,&QClipboard::dataChanged,this,&Controller::queueClipboardCapture);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Independent of the QML window; polling is a cheap change-counter check,
    // not repeated reading/decoding of clipboard contents.
    clipboardPoll_.setInterval(250);clipboardPoll_.setTimerType(Qt::CoarseTimer);
    connect(&clipboardPoll_,&QTimer::timeout,this,[this]{
        if(capturing_||captureTimer_.isActive()||writingClipboard_)return;
        auto sequence=native::clipboardSequence();
        if(sequence&&sequence!=lastSequence_)queueClipboardCapture();
    });
    clipboardPoll_.start();
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
    closing_=true;clipboardPoll_.stop();captureTimer_.stop();queryTimer_.stop();engine_.reset();tray_.reset();
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
void Controller::queueClipboardCapture(){
    if(closing_||writingClipboard_)return;
    ++epoch_;
    if(capturing_){retryCapture_=true;return;}
    captureRetries_=0;
    // Give a clipboard writer time to release its native lock.
    if(!captureTimer_.isActive())captureTimer_.start(25);
}
void Controller::retryClipboardCapture(){
    if(closing_||captureTimer_.isActive())return;
    if(++captureRetries_<=8)captureTimer_.start(75);
    else {
        lastSequence_=native::clipboardSequence();
        setStatus("Clipboard capture could not complete; copy the item again.");
    }
}
void Controller::clipboardChanged(){
    if(closing_||writingClipboard_)return;
    if(capturing_){++epoch_;retryCapture_=true;return;}
    QScopedValueRollback<bool> guard(capturing_,true);
    retryCapture_=false;
    struct Retry {
        Controller* self;bool& needed;
        ~Retry(){if(needed){needed=false;self->retryClipboardCapture();}}
    } retry{this,retryCapture_};
    auto info=native::clipboardInfo();
    // A notification may precede delayed rendering, so an unchanged
    // sequence is not grounds to ignore an event or a scheduled retry.
    lastSequence_=info.sequence;
    if(!info.available){retryClipboardCapture();return;}
    if(info.secret||hasMarker(info.formats,info)){
        ++epoch_;
        QMetaObject::invokeMethod(worker_,[this]{if(store_)store_->protect({});},Qt::QueuedConnection);
        setStatus("Sensitive clipboard content excluded.");return;
    }
    const auto* mime=clipboard_->mimeData();
    const bool self=mime&&mime->hasFormat(SelfMime);
    if(retryCapture_||self)return;
    ++epoch_;
    // Release the previous file-snapshot lease even if the new clipboard is sensitive.
    QMetaObject::invokeMethod(worker_,[this]{if(store_)store_->protect({});},Qt::QueuedConnection);
    if(paused_)return;
    if(!ready_||settingsBusy_||!mime){retryClipboardCapture();return;}
    auto formats=mime->formats();
    if(retryCapture_)return;
    if(hasMarker(formats,info)){setStatus("Sensitive clipboard content excluded.");return;}
    if(pendingCaptures_>=2){retryClipboardCapture();return;}
    clip::Capture capture;
    if(formats.size()>static_cast<int>(clip::MaxFormats)){setStatus("Clipboard item has too many formats.");return;}
    const bool hasUrls=mime->hasUrls();
    if(retryCapture_)return;
    const auto urls=hasUrls?mime->urls():QList<QUrl>{};
    if(retryCapture_)return;
    if(urls.size()>static_cast<qsizetype>(clip::MaxNodes)){setStatus("Too many clipboard file references.");return;}
    bool localFiles=false;
    for(const auto& url:urls)if(url.isLocalFile()){localFiles=true;capture.files.push_back(clip::path(bytes(url.toLocalFile())));}
    if(localFiles&&static_cast<std::size_t>(urls.size())!=capture.files.size()){setStatus("Mixed local and remote file references are not snapshotted.");return;}
    capture.kind=localFiles?"Files":"Data";capture.preview=localFiles?bytes(QFileInfo(urls.first().toLocalFile()).fileName()):"";
    if(localFiles&&urls.size()>1)capture.preview+=" (+"+std::to_string(urls.size()-1)+")";
    quint64 total=0;bool hasImage=false;
    for(const auto& type:formats){
        if(type==SelfMime||!policyName(type).isEmpty())continue;
        if(localFiles&&isFileMime(type))continue;
        if(!localFiles&&isFileMime(type)&&type!="text/uri-list"){
            // Ignore transport-only metadata, not the entire clipboard item.
            // Virtual file promises are still not saved as filesystem snapshots.
            continue;
        }
        if(type=="application/x-qt-image"){
            const bool hasQtImage=mime->hasImage();
            if(retryCapture_)return;
            if(!hasQtImage)continue;
            auto image=qvariant_cast<QImage>(mime->imageData());
            if(retryCapture_)return;
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
        if(retryCapture_)return;
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
    if(after.secret||hasMarker(after.formats,after)){
        lastSequence_=after.sequence;setStatus("Sensitive clipboard content excluded.");return;
    }
    if(!after.available||retryCapture_||(info.sequence&&after.sequence!=info.sequence)){
        // Windows can advance its sequence when a lazily rendered format is
        // fetched. Retry from a fresh MIME object instead of losing the item.
        retryClipboardCapture();return;
    }
    lastSequence_=after.sequence;
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
            lastSequence_=native::clipboardSequence();
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
