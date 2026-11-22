#include "Recorder.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUuid>
#include <algorithm>
#ifdef Q_OS_LINUX
#include "../Platform/Portal.hpp"
#endif

using namespace rec;
namespace {
constexpr qint64 Reserve=128LL*1024*1024;
QString errorTail(const QByteArray& data){return QString::fromUtf8(data.right(1600)).trimmed();}
}
Recorder::Recorder(QObject* parent):QObject(parent){
    watchdog_.setInterval(500);watchdog_.setTimerType(Qt::CoarseTimer);
    connect(&watchdog_,&QTimer::timeout,this,&Recorder::monitor);
    connect(&process_,&QProcess::readyReadStandardOutput,this,&Recorder::progress);
    connect(&process_,&QProcess::started,this,[this]{
        if(video_)video_->authorize(process_.processId());
        if(audio_)audio_->authorize(process_.processId());
    });
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{
        stderr_+=process_.readAllStandardError();if(stderr_.size()>16384)stderr_.remove(0,stderr_.size()-16384);
    });
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&Recorder::finished);
    connect(&process_,&QProcess::errorOccurred,this,[this](auto error){
        if(error==QProcess::FailedToStart&&stage_!=Stage::None){
            stderr_=process_.errorString().toUtf8();finished(1,QProcess::CrashExit);
        }
    });
}
Recorder::~Recorder(){
    if(process_.state()!=QProcess::NotRunning){
        process_.disconnect(this);process_.write("q\n");process_.waitForBytesWritten(100);
        if(!process_.waitForFinished(2000)){process_.kill();process_.waitForFinished(1000);}
    }
    clearCapture();
}
void Recorder::transition(State state,QString status){state_=state;status_=std::move(status);emit changed();}
void Recorder::start(Target target,QRect region,Config cfg,QString systemSource
#ifdef Q_OS_LINUX
    ,std::shared_ptr<PortalSession> portal
#endif
){
    if(state_!=State::Idle)return;
    exe_=ffmpegPath();
    if(exe_.isEmpty()){fail("FFmpeg was not found. Install FFmpeg or use the packaged build.");return;}
#ifdef Q_OS_LINUX
    if(native::wayland()&&!portal){fail("Wayland requires a compositor-authorized capture session.");return;}
#endif
    if(!validSettings(cfg.fps,cfg.bitrate,cfg.quality,cfg.height)){fail("Invalid recording settings.");return;}
    if(cfg.microphone&&cfg.mic.isEmpty()){fail("Choose an available microphone first.");return;}
    if(cfg.systemAudio&&systemSource.isEmpty()){fail("No system audio source is available.");return;}
    if(!native::requestPermission()){fail("Screen Recording permission denied. Enable it in System Settings and restart the app.");return;}
    if(!region.isEmpty()&&(!QRect(QPoint(),target.native.size()).contains(region)||target.window)){
        fail("The region lies outside the selected screen.");return;
    }
    const auto input=region.isEmpty()?target.native.size():region.size();
    size_=outputSize({input.width(),input.height()},cfg.height);
    if(!validFrame(size_)){fail("Invalid or oversized recording area (maximum 8192 pixels per axis / 128 MiB per source frame).");return;}
    if(cfg.output.trimmed().isEmpty()||!QDir().mkpath(cfg.output)){fail("Unable to create the recording directory.");return;}
    cfg.output=QDir(cfg.output).absolutePath();
    QStorageInfo storage(cfg.output);
    if(storage.isValid()&&storage.bytesAvailable()>=0&&storage.bytesAvailable()<Reserve){fail("Not enough disk space to record.");return;}
    temp_=std::make_unique<QTemporaryDir>(QDir(cfg.output).filePath(".screenrecorder-XXXXXX"));
    if(!temp_->isValid()){fail("Unable to create a temporary recording directory.");return;}
    target_=std::move(target);region_=region;config_=std::move(cfg);systemSource_=std::move(systemSource);
#ifdef Q_OS_LINUX
    portal_=std::move(portal);
    if(portal_){
        portal_->discardPreview();
        connect(portal_.get(),&PortalSession::failed,this,[this](QString why){
            if(state_==State::Paused){endNote_=why;finalize();}
            else if(stage_==Stage::Segment&&state_!=State::Stopping)requestEnd(State::Stopping,why);
            else if(stage_==Stage::Probe){endNote_=why;transition(State::Stopping,why);process_.kill();}
        },Qt::QueuedConnection);
    }
#endif
    finalFile_=QDir(config_.output).filePath("Recording-"+QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz")
        +"-"+QUuid::createUuid().toString(QUuid::Id128).left(6)+".mp4");
    parts_.clear();nextPart_=0;completedUs_=segmentUs_=0;endNote_.clear();lastFile_.clear();dxgiRejected_=false;
    candidates_=encoders(config_.hardware);encoder_={};
    watchdog_.start();transition(State::Preparing,"Checking available encoders…");probe();
}
void Recorder::launch(Stage stage,const QStringList& args){
    stage_=stage;stdout_.clear();stderr_.clear();progressClock_.restart();
    process_.setWorkingDirectory(temp_->path());
    process_.setProgram(exe_);process_.setArguments(args);process_.start();
}
void Recorder::probe(){
    if(state_!=State::Preparing)return;
    if(candidates_.isEmpty()){fail("No usable H.264 encoder. Install an FFmpeg build with libx264.");return;}
    encoder_=candidates_.takeFirst();
    transition(State::Preparing,"Testing "+encoder_.name+"…");
    launch(Stage::Probe,probeArgs(encoder_,config_,size_));
}
void Recorder::segment(){
    if(state_!=State::Preparing)return;
    segmentUs_=0;writtenFrame_=false;endNote_.clear();
    current_=QString("part-%1.mp4").arg(nextPart_++,6,10,QChar('0'));
    const auto inputSize=region_.isEmpty()?target_.native.size():region_.size();
    directDxgi_=!dxgiRejected_&&!target_.window&&target_.hardwareIndex>=0&&encoder_.name=="h264_nvenc"&&
                size_.w==inputSize.width()&&size_.h==inputSize.height();
    QString videoUrl,audioUrl;
    auto mailboxVideo=std::make_shared<Mailbox>();
    auto mailboxAudio=config_.systemAudio?std::make_shared<Mailbox>():nullptr;
#ifdef Q_OS_MACOS
    video_=std::make_unique<Feed>(mailboxVideo,true,config_.fps);
    if(!video_->open()){fail("Unable to initialize local video transport.");return;}
    videoUrl=video_->url();
#endif
#ifdef Q_OS_LINUX
    if(portal_){
        video_=std::make_unique<Feed>(mailboxVideo,true,config_.fps);
        if(!video_->open()){fail("Unable to initialize the local portal video transport.");return;}
        videoUrl=video_->url();
        portal_->record(mailboxVideo,region_,size_);
    }
#endif
#if defined(Q_OS_WIN)||defined(Q_OS_MACOS)
    if(mailboxAudio){
        audio_=std::make_unique<Feed>(mailboxAudio,false,config_.fps);
        if(!audio_->open()){fail("Unable to initialize local audio transport.");return;}
        audioUrl=audio_->url();
    }
#endif
    auto fault=[this](QString error){
        if(stage_==Stage::Segment&&state_!=State::Stopping)requestEnd(State::Stopping,std::move(error));
    };
    if(video_)connect(video_.get(),&Feed::fault,this,fault);
    if(audio_)connect(audio_.get(),&Feed::fault,this,fault);
#ifdef Q_OS_MACOS
    capture_=native::screenStream(target_,region_,size_,config_.fps,config_.cursor,mailboxVideo,mailboxAudio);
#elif defined(Q_OS_WIN)
    if(mailboxAudio)capture_=native::loopback(mailboxAudio);
#endif
    QList<QStringList> inputs;
    if(config_.microphone)inputs.append(microphoneInput(config_.mic));
    if(config_.systemAudio)inputs.append(systemInput(systemSource_,audioUrl));
    transition(State::Preparing,"Opening capture devices…");
    launch(Stage::Segment,recordArgs(videoInput(target_,region_,config_,size_,videoUrl,directDxgi_),
        inputs,encoder_,config_,size_,current_,directDxgi_));
}
void Recorder::progress(){
    stdout_+=process_.readAllStandardOutput();
    if(stdout_.size()>16384)stdout_.remove(0,stdout_.size()-16384);
    for(qsizetype at;(at=stdout_.indexOf('\n'))>=0;){
        auto line=stdout_.left(at).trimmed();stdout_.remove(0,at+1);
        if(stage_!=Stage::Segment)continue;
        if(line.startsWith("frame=")&&line.mid(6).toInt()>0){
            writtenFrame_=true;
            if(state_==State::Preparing)transition(State::Recording,"Recording · "+encoder_.name);
        }
        if(line.startsWith("out_time_us=")){
            const auto value=line.mid(12);
            const auto us=parseProgressUs(std::string_view(value.constData(),size_t(value.size())));
            if(us>=0)segmentUs_=std::max(segmentUs_,qint64(us));
        }
        if(line.startsWith("progress=")){
            if(writtenFrame_&&state_!=State::Pausing&&state_!=State::Stopping)progressClock_.restart();
            emit changed();
        }
    }
}
void Recorder::pause(){if(canPause(state_))requestEnd(State::Pausing,"Pausing…");}
void Recorder::resume(){
    if(!canResume(state_))return;
    // Reopen the exact source and retain the same encoder and stream geometry.
    transition(State::Preparing,"Resuming…");segment();
}
void Recorder::togglePause(){if(canPause(state_))pause();else if(canResume(state_))resume();}
void Recorder::stop(){
    if(state_==State::Paused){finalize();return;}
    if(!canStop(state_))return;
    if(stage_==Stage::Probe){transition(State::Stopping,"Cancelling…");process_.kill();return;}
    if(stage_==Stage::None){fail("Recording cancelled.");return;}
    requestEnd(State::Stopping,"Stopping…");
}
void Recorder::requestEnd(State s,QString note){
    if(s==State::Stopping&&note!="Stopping…")endNote_=note;
    transition(s,std::move(note));progressClock_.restart();
    if(process_.state()==QProcess::Running){process_.write("q\n");process_.closeWriteChannel();}
    else if(process_.state()==QProcess::Starting){process_.kill();}
}
void Recorder::clearCapture(){
    capture_.reset();video_.reset();audio_.reset();
#ifdef Q_OS_LINUX
    if(portal_)portal_->pause();
#endif
}
void Recorder::finished(int code,QProcess::ExitStatus exit){
    progress();stderr_+=process_.readAllStandardError();
    const auto stage=stage_;stage_=Stage::None;
    const bool ok=code==0&&exit==QProcess::NormalExit;
    if(stage==Stage::Probe){
        if(state_==State::Stopping){fail(endNote_.isEmpty()?"Recording cancelled.":endNote_);return;}
        QTimer::singleShot(0,this,[this,ok]{if(ok)segment();else probe();});return;
    }
    if(stage==Stage::Segment){
        clearCapture();
        const bool usable=ok&&writtenFrame_&&QFileInfo(QDir(temp_->path()).filePath(current_)).size()>0;
        if(usable){parts_.append(current_);completedUs_+=segmentUs_;}
        else QFile::remove(QDir(temp_->path()).filePath(current_));
        segmentUs_=0;
        if(!usable&&state_==State::Preparing&&parts_.isEmpty()&&directDxgi_){
            dxgiRejected_=true;
            QTimer::singleShot(0,this,&Recorder::segment);return;
        }
        if(!usable&&state_==State::Preparing&&parts_.isEmpty()&&encoder_.name!="libx264"){
            candidates_.clear();candidates_.append(rec::Encoder{"libx264",{}});
            QTimer::singleShot(0,this,&Recorder::probe);return;
        }
        if(!ok&&endNote_.isEmpty())endNote_="Capture ended: "+errorTail(stderr_);
        if(state_==State::Pausing&&usable){transition(State::Paused,"Paused");return;}
        if(!usable&&parts_.isEmpty()){fail(endNote_.isEmpty()?"No video frames were captured.":endNote_);return;}
        QTimer::singleShot(0,this,&Recorder::finalize);return;
    }
    if(stage==Stage::Concat){
        if(ok)publish(QDir(temp_->path()).filePath("joined.mp4"),endNote_);
        else fail("Unable to finalize MP4: "+errorTail(stderr_),true);
    }
}
qint64 Recorder::recordedBytes()const{
    qint64 count=0;
    if(temp_)for(const auto& name:parts_)count+=QFileInfo(QDir(temp_->path()).filePath(name)).size();
    if(temp_&&stage_==Stage::Segment)count+=QFileInfo(QDir(temp_->path()).filePath(current_)).size();
    return count;
}
void Recorder::monitor(){
    if(stage_==Stage::Probe&&progressClock_.elapsed()>6000){process_.kill();return;}
    if(stage_==Stage::Segment){
        if(state_==State::Pausing||state_==State::Stopping){
            if(progressClock_.elapsed()>12000)process_.kill();
            return;
        }
        if(progressClock_.elapsed()>20000){requestEnd(State::Stopping,"Capture timed out or stopped delivering frames.");return;}
        QStorageInfo info(config_.output);
        if(info.isValid()&&info.bytesAvailable()>=0&&info.bytesAvailable()<Reserve+(parts_.isEmpty()?0:recordedBytes()))
            requestEnd(State::Stopping,"Recording stopped to preserve free disk space.");
    }
}
void Recorder::finalize(){
    if(parts_.isEmpty()){fail("No completed recording.");return;}
    transition(State::Finalizing,"Finalizing MP4…");
#ifdef Q_OS_LINUX
    portal_.reset();
#endif
    if(parts_.size()==1){publish(QDir(temp_->path()).filePath(parts_.first()),endNote_);return;}
    QStorageInfo info(config_.output);
    if(info.isValid()&&info.bytesAvailable()>=0&&info.bytesAvailable()<recordedBytes()+1024*1024){fail("Not enough free space to join recording segments.",true);return;}
    QSaveFile manifest(QDir(temp_->path()).filePath("segments.txt"));
    if(!manifest.open(QIODevice::WriteOnly)){fail("Cannot write segment manifest.",true);return;}
    for(const auto& name:parts_){
        auto line=("file '"+name+"'\n").toUtf8();
        if(manifest.write(line)!=line.size()){manifest.cancelWriting();fail("Cannot write segment manifest.",true);return;}
    }
    if(!manifest.commit()){fail("Cannot save segment manifest.",true);return;}
    launch(Stage::Concat,concatArgs("joined.mp4"));
}
void Recorder::publish(QString source,QString note){
    // Temp directory is on the same filesystem; QFile refuses overwrite.
    if(!QFile::rename(source,finalFile_)){fail("Cannot publish the completed MP4.",true);return;}
    lastFile_=finalFile_;temp_.reset();parts_.clear();watchdog_.stop();
    transition(State::Idle,(note.isEmpty()?QString("Saved recording."):note+" Saved completed recording.")+" "+lastFile_);
}
void Recorder::fail(QString message,bool retain){
    clearCapture();watchdog_.stop();stage_=Stage::None;
#ifdef Q_OS_LINUX
    portal_.reset();
#endif
    if(retain&&temp_&&!parts_.isEmpty()){
        temp_->setAutoRemove(false);message+=" Completed MP4 segments retained in "+temp_->path();
    }
    temp_.reset();parts_.clear();segmentUs_=0;
    transition(State::Idle,std::move(message));
}
