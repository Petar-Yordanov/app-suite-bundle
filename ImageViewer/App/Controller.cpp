#include "Controller.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMutexLocker>
#include <QProcess>
#include <QRunnable>
#include <QStyleHints>
#include <algorithm>
#include <cstdio>

QImage ImageProvider::requestImage(const QString&,QSize* size,const QSize&){
    QMutexLocker locker(&buffer_->mutex);if(size)*size=buffer_->image.size();return buffer_->image; // implicit sharing, no pixel copy
}
Controller::Controller(QString dataDirectory,QObject* parent):QObject(parent),directory_(std::move(dataDirectory)),
    settings_(QDir(directory_).filePath("settings.ini"),QSettings::IniFormat),updater_(QDir(directory_).filePath("updates"),this){
    pool_.setMaxThreadCount(1);pool_.setExpiryTimeout(30000);
    animationTimer_.setSingleShot(true);animationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&animationTimer_,&QTimer::timeout,this,[this]{wantedFrame_=true;schedule();});
    scanTimer_.setSingleShot(true);scanTimer_.setInterval(200);
    connect(&watcher_,&QFileSystemWatcher::directoryChanged,this,[this]{scanTimer_.start();});
    connect(&watcher_,&QFileSystemWatcher::fileChanged,this,[this](const QString& file){if(file==path_&&QFileInfo::exists(file))reload();});
    connect(&scanTimer_,&QTimer::timeout,this,&Controller::requestScan);
    connect(QGuiApplication::styleHints(),&QStyleHints::colorSchemeChanged,this,&Controller::preferencesChanged);
    updater_.configure(updateFeed());
}
Controller::~Controller(){closing_=true;animationTimer_.stop();scanTimer_.stop();pool_.waitForDone();animation_.reset();}
QString Controller::fileName()const{return QFileInfo(path_).fileName();}
bool Controller::systemDark()const{return QGuiApplication::styleHints()->colorScheme()==Qt::ColorScheme::Dark;}
QString Controller::imageUrl()const{return displayed_.isNull()?QString():QStringLiteral("image://view/current/%1").arg(revision_);}
QStringList Controller::nameFilters()const{
    QStringList patterns;for(const auto& suffix:imageview::extensions())patterns.append("*."+suffix);
    return {"Images ("+patterns.join(' ')+")","All files (*)"};
}
void Controller::setStatus(QString text){status_=std::move(text);emit stateChanged();}
void Controller::open(const QUrl& url){if(!url.isLocalFile()){setStatus("Only local image files are supported.");return;}select(url.toLocalFile());}
void Controller::select(QString path,bool force){
    path=QFileInfo(path).absoluteFilePath();
    if(!force&&path==path_&&!displayed_.isNull())return;
    if(!QFileInfo(path).isFile()){setStatus("Image file does not exist.");return;}
    if(!force&&!current_.image.isNull()&&currentFingerprint_==imageview::fingerprint(path_)&&!current_.metadata.value("Animated source").toBool()&&QFileInfo(path).absolutePath()==activeDirectory_){
        auto cost=std::max<qint64>(1,(current_.image.sizeInBytes()+1023)/1024);
        if(cost<=cache_.maxCost())cache_.insert(currentFingerprint_,new imageview::Decoded(current_),int(cost));
    }
    current_={};currentFingerprint_.clear();
    ++generation_;wantedDecode_=true;wantedFrame_=false;loading_=true;animationTimer_.stop();animation_.reset();play_=true;preloads_.clear();
    if(!watcher_.files().isEmpty())watcher_.removePaths(watcher_.files());watcher_.addPath(path);
    const auto dir=QFileInfo(path).absolutePath();
    if(activeDirectory_!=dir){
        activeDirectory_=dir;files_.clear();cache_.clear();wantedScan_=true;
        if(!watcher_.directories().isEmpty())watcher_.removePaths(watcher_.directories());watcher_.addPath(dir);
    }
    path_=std::move(path);loadTimer_.restart();original_={};reduced_=false;metadata_.clear();emit pathChanged();emit infoChanged();emit navigationChanged();
    if(force)cache_.remove(imageview::fingerprint(path_));
    auto key=imageview::fingerprint(path_);
    if(!force){if(auto* decoded=cache_.object(key)){auto cached=*decoded;wantedDecode_=false;applyDecoded(std::move(cached));schedule();return;}}
    publishImage({});setStatus("Loading image…");schedule();
}
void Controller::publishImage(QImage image){
    displayed_=std::move(image);{QMutexLocker lock(&buffer_->mutex);buffer_->image=displayed_;}++revision_;emit imageChanged();
}
void Controller::applyDecoded(imageview::Decoded result){
    loading_=false;original_=result.original;reduced_=result.reduced;metadata_.clear();
    if(!result.metadata.value("Animated source").toBool()){current_=result;currentFingerprint_=imageview::fingerprint(path_);}
    // Present core fields first; retain only bounded metadata supplied by decoders.
    for(const auto& key:QStringList{"Dimensions","Format","File size","Color profile"}){
        if(!result.metadata.contains(key))continue;auto value=result.metadata.take(key);
        if(key=="File size")value=QStringLiteral("%1 bytes (%2 MiB)").arg(value.toLongLong()).arg(value.toDouble()/imageview::MiB,0,'f',2);
        metadata_.append(QVariantMap{{"name",key},{"value",value.toString()}});
    }
    result.metadata.remove("Animated source");
    for(auto it=result.metadata.cbegin();it!=result.metadata.cend();++it)metadata_.append(QVariantMap{{"name",it.key()},{"value",it.value().toString()}});
    animation_=std::move(result.animation);
    publishImage(std::move(result.image));emit infoChanged();
    if(displayed_.isNull()){setStatus(result.error.isEmpty()?"Unsupported or corrupt image.":result.error);return;}
    const auto suffix=reduced_?QStringLiteral(" · scaled preview for memory safety"):QString();
    setStatus(QStringLiteral("%1 × %2 · %3 ms%4").arg(original_.width()).arg(original_.height()).arg(loadTimer_.elapsed()).arg(suffix));
    if(benchmark_&&!benchmarkPrinted_)std::fprintf(stdout,"DECODE_READY_MS=%lld\n",static_cast<long long>(QDateTime::currentMSecsSinceEpoch()-startMs_));
    if(animation_)armAnimation(animation_->delay);else planPreload();
}
void Controller::schedule(){
    if(closing_||work_!=Work::None)return;
    if(wantedDecode_){wantedDecode_=false;dispatchDecode(path_,generation_,false);return;}
    if(wantedScan_){wantedScan_=false;dispatchScan(activeDirectory_);return;}
    if(wantedFrame_&&animation_&&play_&&visible_){wantedFrame_=false;dispatchFrame();return;}
    if(!animation_&&!preloads_.isEmpty()){
        while(!preloads_.isEmpty()){
            const auto p=preloads_.takeFirst();if(p==path_||!QFileInfo::exists(p)||cache_.contains(imageview::fingerprint(p)))continue;
            // Don't let costly speculative RAW/HEIF decodes delay foreground navigation.
            const auto info=QFileInfo(p);const auto suffix=info.suffix().toLower();
            if(info.size()>24*imageview::MiB||!QStringList{"jpg","jpeg","png","webp","bmp","gif","tif","tiff","svg","psd"}.contains(suffix))continue;
            dispatchDecode(p,generation_,true);return;
        }
    }
}
void Controller::dispatchDecode(QString path,quint64 generation,bool preload){
    work_=preload?Work::Preload:Work::Decode;const auto key=imageview::fingerprint(path);
    pool_.start(QRunnable::create([this,path=std::move(path),key,generation,preload]{
        imageview::Decoded result;
        try{result=imageview::decode(path,!preload);}catch(const std::exception& e){result.error=QString::fromUtf8(e.what());}catch(...){result.error="Decoder failed.";}
        QMetaObject::invokeMethod(this,[this,path,key,generation,preload,result=std::move(result)]()mutable{
            work_=Work::None;if(closing_)return;
            const bool valid=key==imageview::fingerprint(path);
            if(!preload&&generation==generation_&&path==path_){
                if(valid)applyDecoded(std::move(result));else{wantedDecode_=true;loading_=true;}
            }else if(preload&&valid&&path==path_&&wantedDecode_&&!result.image.isNull()&&!result.metadata.value("Animated source").toBool()){
                wantedDecode_=false;applyDecoded(std::move(result));
            }else if(preload&&valid&&generation==generation_&&QFileInfo(path).absolutePath()==activeDirectory_&&!result.image.isNull()){
                const auto cost=std::max<qint64>(1,(result.image.sizeInBytes()+1023)/1024);
                // Preloaded animated files are not cached as a first-frame-only still.
                if(!result.metadata.value("Animated source").toBool()&&cost<=cache_.maxCost())cache_.insert(key,new imageview::Decoded(std::move(result)),int(cost));
            }
            schedule();
        },Qt::QueuedConnection);
    }));
}
void Controller::dispatchScan(QString directory){
    work_=Work::Scan;const auto selected=path_.isEmpty()?QDir(directory).filePath(".imageviewer-scan-placeholder"):path_;
    pool_.start(QRunnable::create([this,directory,selected]{
        auto files=imageview::neighbors(selected);
        QMetaObject::invokeMethod(this,[this,directory,files=std::move(files)]()mutable{
            work_=Work::None;if(closing_)return;
            if(directory==activeDirectory_){files_=std::move(files);if(!path_.isEmpty()&&!files_.contains(path_)&&QFileInfo::exists(path_)){files_.append(path_);files_.sort(Qt::CaseInsensitive);}emit navigationChanged();planPreload();}
            schedule();
        },Qt::QueuedConnection);
    }));
}
void Controller::requestScan(){if(activeDirectory_.isEmpty())return;wantedScan_=true;schedule();}
void Controller::planPreload(){
    preloads_.clear();if(animation_||index()<0)return;
    if(hasNext())preloads_.append(files_.at(index()+1));if(hasPrevious())preloads_.append(files_.at(index()-1));
    QStringList keep{imageview::fingerprint(path_)};for(const auto& p:preloads_)keep.append(imageview::fingerprint(p));
    for(const auto& key:cache_.keys())if(!keep.contains(key))cache_.remove(key);
}
void Controller::previous(){if(hasPrevious())select(files_.at(index()-1));}
void Controller::next(){if(hasNext())select(files_.at(index()+1));}
void Controller::reload(){if(!path_.isEmpty())select(path_,true);}
void Controller::moveToTrash(const QString& expectedPath){
    if(path_.isEmpty()||loading_||path_!=expectedPath)return;const auto deleted=path_;const auto i=index();
    if(!QFile::moveToTrash(deleted)){setStatus("Could not move image to trash. The file was not permanently deleted.");return;}
    ++generation_;wantedDecode_=false;wantedFrame_=false;animationTimer_.stop();animation_.reset();cache_.clear();current_={};preloads_.clear();
    files_.removeAll(deleted);path_.clear();original_={};metadata_.clear();reduced_=false;publishImage({});emit pathChanged();emit infoChanged();emit navigationChanged();
    if(!files_.isEmpty())select(files_.at(std::clamp(i,0,int(files_.size()-1))));else setStatus("Image moved to trash.");
    requestScan();
}
void Controller::armAnimation(int delay){if(animation_&&play_&&visible_){animationTimer_.start(std::clamp(delay,20,10000));}}
void Controller::dispatchFrame(){
    work_=Work::Frame;auto animation=animation_;const auto generation=generation_;
    pool_.start(QRunnable::create([this,animation,generation]{
        imageview::Frame frame;try{frame=imageview::nextFrame(*animation);}catch(...){frame.ended=true;frame.error="Animation decoding failed.";}
        QMetaObject::invokeMethod(this,[this,generation,frame=std::move(frame)]()mutable{
            work_=Work::None;if(closing_)return;
            if(generation==generation_&&animation_){
                if(frame.ended){animation_.reset();emit stateChanged();if(!frame.error.isEmpty())setStatus(frame.error);planPreload();}
                else{publishImage(std::move(frame.image));armAnimation(frame.delay);}
            }
            schedule();
        },Qt::QueuedConnection);
    }));
}
void Controller::togglePlayback(){if(!animation_)return;play_=!play_;if(play_&&visible_)armAnimation(20);else{animationTimer_.stop();wantedFrame_=false;}emit stateChanged();}
void Controller::setWindowVisible(bool value){visible_=value;if(!value){animationTimer_.stop();wantedFrame_=false;}else if(animation_&&play_)armAnimation(20);}
QVariantMap Controller::defaults(){return {{"open","Ctrl+O"},{"previous","Left"},{"next","Right"},{"zoomIn","+"},{"zoomOut","-"},{"fit","F"},{"actual","1"},{"rotateRight","R"},{"rotateLeft","Shift+R"},{"trash","Delete"},{"info","I"},{"fullscreen","F11"},{"play","Space"},{"reload","Ctrl+R"}};}
QVariantMap Controller::shortcuts()const{auto map=defaults();for(auto it=map.begin();it!=map.end();++it)it.value()=settings_.value("shortcuts/"+it.key(),it.value());return map;}
bool Controller::setShortcut(const QString& action,const QString& sequence){
    auto map=shortcuts();if(!map.contains(action))return false;auto trimmed=sequence.trimmed();auto key=QKeySequence::fromString(trimmed,QKeySequence::PortableText);
    if(!trimmed.isEmpty()&&(key.isEmpty()||key[0].key()==Qt::Key_unknown||key.count()!=1)){setStatus("Invalid shortcut. Enter a single key combination.");return false;}
    auto normalized=key.toString(QKeySequence::PortableText);
    if(!normalized.isEmpty())for(auto it=map.cbegin();it!=map.cend();++it)if(it.key()!=action&&it.value().toString()==normalized){setStatus("Shortcut already assigned to "+it.key()+".");return false;}
    settings_.setValue("shortcuts/"+action,normalized);settings_.sync();emit preferencesChanged();return true;
}
void Controller::resetShortcuts(){settings_.remove("shortcuts");settings_.sync();emit preferencesChanged();}
void Controller::setTheme(const QString& value){if(value!="light"&&value!="dark"&&value!="system")return;settings_.setValue("theme",value);settings_.sync();emit preferencesChanged();}
void Controller::setUpdateFeed(const QString& url){settings_.setValue("updateFeed",url.trimmed());settings_.sync();updater_.configure(updateFeed());emit preferencesChanged();}
void Controller::reveal(){if(path_.isEmpty())return;QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path_).absolutePath()));}
void Controller::defaultAppSettings(){
#if defined(Q_OS_WIN)
    QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));
#elif defined(Q_OS_MACOS)
    setStatus("In Finder, select an image, Get Info → Open with → Image Viewer → Change All.");reveal();
#else
    setStatus("Use your desktop's Open With / Default Applications settings to select Image Viewer.");reveal();
#endif
}
void Controller::enableBenchmark(qint64 startMs){benchmark_=true;startMs_=startMs;}
void Controller::imagePresented(){if(benchmark_&&!benchmarkPrinted_&&!displayed_.isNull()){benchmarkPrinted_=true;std::fprintf(stdout,"FIRST_IMAGE_MS=%lld\n",static_cast<long long>(QDateTime::currentMSecsSinceEpoch()-startMs_));std::fflush(stdout);}}
