#include "Player.hpp"
#include "../Core/Algorithms.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStyleHints>
#include <QKeySequence>
#include <QSet>
#include <QEventLoop>
#include <QThread>
#include <thread>
#include <atomic>
#include <iterator>
#include <cmath>

namespace {
struct Action{const char* id;const char* label;const char* key;};
constexpr Action actions[]={
    {"play","Play / pause","Space"},{"stop","Stop","S"},{"previous","Previous video","P"},{"next","Next video","N"},
    {"back","Seek backward 10 s","Left"},{"forward","Seek forward 10 s","Right"},{"volumeUp","Volume up","Up"},
    {"volumeDown","Volume down","Down"},{"mute","Mute","M"},{"fullscreen","Fullscreen","F"},
    {"open","Open file","Ctrl+O"},{"playlist","Playlist","Ctrl+L"},{"subtitles","Add subtitles","Ctrl+T"},{"settings","Settings","Ctrl+,"}
};
QVariantList trackList(libvlc_track_description_t* list,VlcApi& api){
    QVariantList out{QVariantMap{{"id",-1},{"label","Disabled"}}};
    for(auto* p=list;p;p=p->p_next)if(p->i_id!=-1)out.append(QVariantMap{{"id",p->i_id},{"label",p->psz_name?QString::fromUtf8(p->psz_name):QString("Track %1").arg(p->i_id)}});
    if(list)api.libvlc_track_description_list_release(list);return out;
}
QList<QUrl> urlsFor(const QStringList& paths){QList<QUrl> urls;for(const auto& path:paths)urls.push_back(QUrl::fromLocalFile(path));return urls;}
}
Player::Player(QString profile,VideoSurface* surface,QWidget* parent,bool quiet):surface_(surface),parent_(parent),profile_(std::move(profile)),settings_(profile_+"/settings.ini",QSettings::IniFormat),resume_(settings_),quiet_(quiet){
    volume_=std::clamp(settings_.value("volume",80).toInt(),0,100);muted_=settings_.value("muted",false).toBool();
    for(const auto& a:actions)shortcuts_[a.id]=settings_.value(QString("shortcut/")+a.id,a.key).toString();
    connect(&timer_,&QTimer::timeout,this,&Player::tick);timer_.setTimerType(Qt::CoarseTimer);
    setTheme(theme());if(!updateFeed().isEmpty()&&!quiet_)updater();
}
Player::~Player(){savePosition();releaseMedia();if(mp_)api_.libvlc_media_player_release(mp_);if(instance_)api_.libvlc_release(instance_);}
QString Player::title() const{return current_.isEmpty()?"Media Player":QFileInfo(current_).fileName();}
Updater* Player::updater(){if(!updater_){updater_=std::make_unique<Updater>(profile_+"/updates");if(!quiet_)updater_->configure(updateFeed());}return updater_.get();}
bool Player::checkEngine(QString& error){
    if(instance_)return true;
    if(!api_.load(error))return false;
    const char* args[]={"--no-video-title-show","--no-osd","--no-metadata-network-access","--no-lua","--avcodec-hw=any","--quiet","--intf=dummy","--stats"};
    const char* testArgs[]={"--no-video-title-show","--no-osd","--no-metadata-network-access","--no-lua","--avcodec-hw=none","--aout=dummy","--quiet","--intf=dummy","--stats"};
    instance_=quiet_?api_.libvlc_new(int(std::size(testArgs)),testArgs):api_.libvlc_new(int(std::size(args)),args);
    if(!instance_){error="libVLC initialization failed: "+api_.lastError();return false;}
    mp_=api_.libvlc_media_player_new(instance_);
    if(!mp_){error="Cannot allocate libVLC player.";api_.libvlc_release(instance_);instance_=nullptr;return false;}
    surface_->attach(api_,mp_);return true;
}
void Player::setPhase(Phase p,QString s){phase_=p;status_=std::move(s);emit stateChanged();}
void Player::resetTimeline(){position_=duration_=0;seekable_=false;emit timelineChanged();audioTracks_.clear();subtitleTracks_.clear();audioTrack_=subtitleTrack_=-1;emit tracksChanged();}
void Player::releaseMedia(){
    timer_.stop();
    if(media_){
        const bool parsing=phase_==Phase::Parsing;
        auto* old=media_;media_=nullptr;transitioning_=true;verifiedVideo_=false;
        // libVLC 3 stop is synchronous. On macOS its video output may need the
        // AppKit main thread to detach its NSView; keep that dispatcher alive.
        // Suppress user input and guard against reentrant loads during teardown.
        std::atomic_bool done{false};
        std::thread worker([this,old,parsing,&done]{
            if(parsing)api_.libvlc_media_parse_stop(old);
            api_.libvlc_media_player_stop(mp_);
            api_.libvlc_media_player_set_media(mp_,nullptr);
            api_.libvlc_media_release(old);done.store(true,std::memory_order_release);
        });
        while(!done.load(std::memory_order_acquire)){
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents,5);
            QThread::msleep(1);
        }
        worker.join();transitioning_=false;
    }
    surface_->reset();verifiedVideo_=false;resumePending_=false;applyAudio_=false;
}
void Player::fail(QString error,bool skipCandidate){
    releaseMedia();resetTimeline();setPhase(Phase::Error,std::move(error));
    if(!skipCandidate)navigation_.clear();
    if(skipCandidate&&!navigation_.isEmpty())QTimer::singleShot(0,this,[this]{if(phase_==Phase::Error&&!navigation_.isEmpty())advanceNavigation();});
}
void Player::savePosition(){
    if(!current_.isEmpty()&&remember()&&verifiedVideo_&&!resumePending_){
        if(phase_==Phase::Playing||phase_==Phase::Paused){auto t=api_.libvlc_media_player_get_time(mp_);if(t>=0)position_=t;resume_.save(current_,position_,duration_);}
    }
}
bool Player::load(QString path,bool restore){
    if(transitioning_)return false;
    const auto valid=media::localVideo(QUrl::fromLocalFile(path));
    if(valid.isEmpty()){status_="File unavailable or not a local video.";emit stateChanged();return false;}
    savePosition();releaseMedia();resetTimeline();
    current_=valid;playlistIndex_=playlist_.indexOf(current_);emit playlistChanged();
    QString error;if(!checkEngine(error)){fail(error);return false;}
    auto utf8=QDir::toNativeSeparators(current_).toUtf8();media_=api_.libvlc_media_new_path(instance_,utf8.constData());
    if(!media_){fail("Cannot open video: "+api_.lastError());return false;}
    api_.libvlc_media_add_option(media_,":no-video-title-show");
    resumeAt_=restore&&remember()?resume_.load(current_,0):0;resumePending_=resumeAt_>0;
    phaseTime_.restart();setPhase(Phase::Parsing,"Reading video tracks...");
    if(api_.libvlc_media_parse_with_options(media_,libvlc_media_parse_local,5000)!=0){startPlayback();return phase_!=Phase::Error;}
    timer_.start(50);return true;
}
bool Player::hasVideoTrack(unsigned* count) const{
    if(!media_)return false;libvlc_media_track_t** tracks=nullptr;
    auto n=api_.libvlc_media_tracks_get(media_,&tracks);bool found=false;
    for(unsigned i=0;i<n;++i)if(tracks[i]&&tracks[i]->i_type==libvlc_track_video){found=true;break;}
    if(tracks)api_.libvlc_media_tracks_release(tracks,n);if(count)*count=n;return found;
}
void Player::startPlayback(){
    auto type=api_.libvlc_media_get_type(media_);
    if(type==libvlc_media_type_playlist||type==libvlc_media_type_stream||type==libvlc_media_type_directory){fail("Only local video files are supported.",true);return;}
    unsigned count=0;const bool video=hasVideoTrack(&count);
    if(count>0&&!video){fail("Audio-only files are not supported.",true);return;}
    // Unrecognized containers are allowed to try playback, but remain muted until
    // a real video track/output is available.
    verifiedVideo_=video;duration_=std::max<qint64>(0,api_.libvlc_media_get_duration(media_));
    emit timelineChanged();api_.libvlc_media_player_set_media(mp_,media_);
    applyAudio_=true;
    api_.libvlc_audio_set_mute(mp_,muted_||!verifiedVideo_);
    if(!verifiedVideo_){api_.libvlc_audio_set_volume(mp_,0);}
    if(api_.libvlc_media_player_play(mp_)!=0){fail("Playback failed: "+api_.lastError(),true);return;}
    phaseTime_.restart();refreshTime_.invalidate();saveTime_.restart();setPhase(Phase::Starting,"Starting playback...");timer_.start(100);
}
void Player::tick(){
    if(!media_||!mp_)return;
    if(phase_==Phase::Parsing){
        auto parsed=api_.libvlc_media_get_parsed_status(media_);
        if(parsed==libvlc_media_parsed_status_done||parsed==libvlc_media_parsed_status_failed||parsed==libvlc_media_parsed_status_timeout||parsed==libvlc_media_parsed_status_skipped||phaseTime_.elapsed()>6000){
            if(parsed!=libvlc_media_parsed_status_done)api_.libvlc_media_parse_stop(media_);
            startPlayback();
        }return;
    }
    auto state=api_.libvlc_media_player_get_state(mp_);
    if(state==libvlc_Error){fail("libVLC could not play this video.",true);return;}
    if(state==libvlc_Ended||(state==libvlc_Stopped&&phase_==Phase::Playing&&duration_>0&&position_>=duration_-1000)){
        if(!verifiedVideo_){fail("No video track found.",true);return;}
        resume_.forget(current_);position_=duration_;seekable_=false;emit timelineChanged();
        timer_.stop();setPhase(Phase::Ended,"Finished");
        if(!playlist_.isEmpty()&&playlistIndex_>=0&&playlistIndex_+1<playlist_.size()){
            auto current=current_;QTimer::singleShot(0,this,[this,current]{if(phase_==Phase::Ended&&current_==current)next();});
        }
        return;
    }
    if(state==libvlc_Stopped&&(phase_==Phase::Playing||phase_==Phase::Paused)){
        if(remember())resume_.save(current_,position_,duration_);
        timer_.stop();seekable_=false;emit timelineChanged();setPhase(Phase::Stopped,"Stopped");return;
    }
    if(!verifiedVideo_){
        if(api_.libvlc_media_player_has_vout(mp_)>0||hasVideoTrack()){verifiedVideo_=true;applyAudio_=true;}
        else if(phaseTime_.elapsed()>15000){fail("No playable video track found.",true);return;}
    }
    if(phase_==Phase::Starting&&phaseTime_.elapsed()>20000){fail("Timed out starting playback.",true);return;}
    if(state!=libvlc_Playing&&state!=libvlc_Paused&&state!=libvlc_Buffering)return;
    if(!verifiedVideo_){
        api_.libvlc_audio_set_mute(mp_,true);api_.libvlc_audio_set_volume(mp_,0);return;
    }
    auto t=api_.libvlc_media_player_get_time(mp_);auto len=api_.libvlc_media_player_get_length(mp_);
    bool seek=api_.libvlc_media_player_is_seekable(mp_)!=0;
    if(len>0)duration_=len;
    if(resumePending_&&(seek&&duration_>0)){
        auto target=media::resumePosition(resumeAt_,duration_);
        if(target>0){api_.libvlc_media_player_set_time(mp_,target);t=target;}
        resumePending_=false;
    }else if(resumePending_&&phaseTime_.elapsed()>6000)resumePending_=false;
    if(t>=0)position_=std::max<qint64>(0,t);
    seekable_=seek&&duration_>0;emit timelineChanged();
    if(state==libvlc_Playing&&phase_!=Phase::Playing){navigation_.clear();applyAudio_=true;setPhase(Phase::Playing,"Playing");timer_.setInterval(200);}
    if(state==libvlc_Paused&&phase_!=Phase::Paused){setPhase(Phase::Paused,"Paused");savePosition();timer_.setInterval(500);}
    if(applyAudio_){applyAudio();if(api_.libvlc_media_player_has_vout(mp_)>0){setAspect(aspect_);applyAudio_=false;}}
    if(!refreshTime_.isValid()||refreshTime_.elapsed()>1000){refreshTracks();refreshTime_.restart();}
    if(phase_==Phase::Playing&&saveTime_.elapsed()>5000){savePosition();saveTime_.restart();}
}
void Player::applyAudio(){if(!mp_||!verifiedVideo_)return;api_.libvlc_audio_set_volume(mp_,volume_);api_.libvlc_audio_set_mute(mp_,muted_);}
void Player::refreshTracks(){
    if(!mp_||!verifiedVideo_)return;
    auto audio=trackList(api_.libvlc_audio_get_track_description(mp_),api_);
    auto subs=trackList(api_.libvlc_video_get_spu_description(mp_),api_);
    auto a=api_.libvlc_audio_get_track(mp_),s=api_.libvlc_video_get_spu(mp_);
    if(audio!=audioTracks_||subs!=subtitleTracks_||a!=audioTrack_||s!=subtitleTrack_){
        audioTracks_=std::move(audio);subtitleTracks_=std::move(subs);audioTrack_=a;subtitleTrack_=s;emit tracksChanged();
    }
}
void Player::openUrls(const QList<QUrl>& urls){
    if(transitioning_)return;
    navigation_.clear();QStringList paths;QSet<QString> seen;
    for(const auto& url:urls){auto p=media::localVideo(url);if(!p.isEmpty()&&!seen.contains(p)&&paths.size()<10000){seen.insert(p);paths.append(p);}}
    if(paths.isEmpty()){status_="Choose a readable local video file.";emit stateChanged();return;}
    playlist_=paths.size()>1?paths:QStringList{};playlistIndex_=-1;emit playlistChanged();load(paths.first());
}
void Player::appendUrls(const QList<QUrl>& urls){
    if(transitioning_)return;
    if(playlist_.isEmpty()&&!current_.isEmpty())playlist_.append(current_);
    for(const auto& url:urls){auto p=media::localVideo(url);if(!p.isEmpty()&&!playlist_.contains(p)&&playlist_.size()<10000)playlist_.append(p);}
    playlistIndex_=playlist_.indexOf(current_);emit playlistChanged();
    if(current_.isEmpty()&&!playlist_.isEmpty())playIndex(0);
}
void Player::openDialog(){auto files=QFileDialog::getOpenFileNames(parent_,"Open video",current_.isEmpty()?QString():QFileInfo(current_).absolutePath(),media::videoFilter());if(!files.isEmpty())openUrls(urlsFor(files));}
void Player::addDialog(){auto files=QFileDialog::getOpenFileNames(parent_,"Add videos",current_.isEmpty()?QString():QFileInfo(current_).absolutePath(),media::videoFilter());if(!files.isEmpty())appendUrls(urlsFor(files));}
void Player::openDropped(QVariantList values){QList<QUrl> urls;for(const auto& value:values)urls.append(value.toUrl());openUrls(urls);}
void Player::togglePlay(){
    if(transitioning_)return;
    navigation_.clear();
    if(phase_==Phase::Parsing||phase_==Phase::Starting){stop();return;}
    if(phase_==Phase::Playing){api_.libvlc_media_player_set_pause(mp_,1);setPhase(Phase::Paused,"Paused");savePosition();timer_.start(500);return;}
    if(phase_==Phase::Paused){api_.libvlc_media_player_set_pause(mp_,0);setPhase(Phase::Playing,"Playing");timer_.start(200);return;}
    if(current_.isEmpty()){openDialog();return;}load(current_,phase_==Phase::Error);
}
void Player::stop(){
    if(transitioning_||current_.isEmpty())return;navigation_.clear();savePosition();auto length=duration_;releaseMedia();resetTimeline();duration_=length;emit timelineChanged();setPhase(Phase::Stopped,"Stopped");
}
void Player::navigate(int direction){
    if(transitioning_)return;
    navigation_.clear();auto files=playlist_.isEmpty()?media::neighbors(current_):playlist_;
    int i=media::neighbor(int(files.size()),int(files.indexOf(current_)),direction);
    while(i>=0){navigation_.append(files[i]);i=media::neighbor(int(files.size()),i,direction);}
    if(navigation_.isEmpty()){status_=direction>0?"No next video":"No previous video";emit stateChanged();return;}
    advanceNavigation();
}
void Player::advanceNavigation(){
    if(transitioning_)return;
    while(!navigation_.isEmpty()){
        auto path=navigation_.takeFirst();
        if(!media::localVideo(QUrl::fromLocalFile(path)).isEmpty()){load(path);return;}
    }
    if(!playing()&&!busy()){status_="No further playable video.";emit stateChanged();}
}
void Player::next(){navigate(1);}
void Player::previous(){navigate(-1);}
void Player::seek(double ms){
    if(transitioning_||!mp_||!seekable_||!std::isfinite(ms))return;
    auto target=media::seekPosition(qint64(std::clamp(ms,0.0,double(duration_))),duration_);
    api_.libvlc_media_player_set_time(mp_,target);position_=target;resumePending_=false;emit timelineChanged();
}
void Player::setVolume(int value){value=std::clamp(value,0,100);if(volume_==value)return;volume_=value;settings_.setValue("volume",value);applyAudio();emit volumeChanged();}
void Player::setMuted(bool value){if(muted_==value)return;muted_=value;settings_.setValue("muted",value);applyAudio();emit volumeChanged();}
void Player::setAudioTrack(int id){if(mp_&&verifiedVideo_&&api_.libvlc_audio_set_track(mp_,id)==0){refreshTracks();applyAudio();}}
void Player::setSubtitleTrack(int id){if(mp_&&verifiedVideo_&&api_.libvlc_video_set_spu(mp_,id)==0)refreshTracks();}
bool Player::addSubtitle(const QUrl& url){
    auto path=media::localSubtitle(url);
    if(path.isEmpty()||!media_||!verifiedVideo_)return false;
    auto uri=QUrl::fromLocalFile(path).toEncoded();
    bool ok=api_.libvlc_media_player_add_slave(mp_,libvlc_media_slave_type_subtitle,uri.constData(),true)==0;
    status_=ok?"Subtitle loaded":"Could not add subtitle.";refreshTracks();emit stateChanged();return ok;
}
void Player::addSubtitleDialog(){if(!media_||!verifiedVideo_)return;auto file=QFileDialog::getOpenFileName(parent_,"Load subtitle",QFileInfo(current_).absolutePath(),"Subtitles (*.srt *.ass *.ssa *.sub *.idx *.vtt *.smi *.sami *.ttml *.sup *.txt);;All files (*)");if(!file.isEmpty())addSubtitle(QUrl::fromLocalFile(file));}
void Player::setAspect(QString ratio){
    static const QSet<QString> choices={"Original","4:3","16:9","16:10","21:9","239:100","1:1"};
    if(!choices.contains(ratio))return;
    bool changed=aspect_!=ratio;aspect_=ratio;surface_->setAspect(ratio=="Original"?QString():ratio);
    if(mp_&&media_&&!surface_->software()){auto val=ratio.toLatin1();api_.libvlc_video_set_aspect_ratio(mp_,ratio=="Original"?nullptr:val.constData());}
    if(changed)emit settingsChanged();
}
void Player::playIndex(int i){navigation_.clear();if(i>=0&&i<playlist_.size())load(playlist_[i]);}
void Player::removeIndex(int i){if(i<0||i>=playlist_.size())return;playlist_.removeAt(i);playlistIndex_=playlist_.indexOf(current_);emit playlistChanged();}
void Player::moveIndex(int from,int to){if(from<0||from>=playlist_.size()||to<0||to>=playlist_.size()||from==to)return;playlist_.move(from,to);playlistIndex_=playlist_.indexOf(current_);emit playlistChanged();}
void Player::clearPlaylist(){navigation_.clear();playlist_.clear();playlistIndex_=-1;emit playlistChanged();}
void Player::setPanel(QString page){if(page!="playlist"&&page!="tracks"&&page!="settings"&&page!="")return;panel_=panel_==page?QString():page;emit panelChanged();}
void Player::toggleFullscreen(){emit requestFullscreen();}
void Player::setFullscreenState(bool value){if(fullscreen_!=value){fullscreen_=value;emit fullscreenChanged();}}
void Player::setTheme(QString theme){if(theme!="system"&&theme!="dark"&&theme!="light")return;settings_.setValue("theme",theme);QGuiApplication::styleHints()->setColorScheme(theme=="dark"?Qt::ColorScheme::Dark:theme=="light"?Qt::ColorScheme::Light:Qt::ColorScheme::Unknown);emit settingsChanged();}
void Player::setRemember(bool value){settings_.setValue("remember",value);if(!value){resumePending_=false;settings_.remove("resume");}emit settingsChanged();}
void Player::setUpdateFeed(QString feed){settings_.setValue("updateFeed",feed.trimmed());updater()->configure(feed);emit settingsChanged();}
QVariantList Player::shortcutActions() const{QVariantList out;for(const auto& a:actions)out.append(QVariantMap{{"id",a.id},{"label",a.label},{"key",shortcuts_.value(a.id)}});return out;}
void Player::setShortcut(QString id,QString text){
    if(!shortcuts_.contains(id))return;
    auto sequence=QKeySequence::fromString(text.trimmed(),QKeySequence::PortableText);
    if(sequence.isEmpty()||sequence.count()!=1||sequence[0].key()==Qt::Key_unknown){status_="Enter a single valid key combination.";emit stateChanged();return;}
    auto key=sequence.toString(QKeySequence::PortableText);
    if(key=="Esc"){status_="Escape is reserved for leaving fullscreen.";emit stateChanged();return;}
    for(auto i=shortcuts_.cbegin();i!=shortcuts_.cend();++i)if(i.key()!=id&&QKeySequence::fromString(i.value(),QKeySequence::PortableText)==sequence){status_="Shortcut already in use.";emit stateChanged();return;}
    shortcuts_[id]=key;settings_.setValue("shortcut/"+id,key);emit shortcutsChanged();emit settingsChanged();
}
void Player::resetShortcuts(){for(const auto& a:actions){shortcuts_[a.id]=a.key;settings_.remove(QString("shortcut/")+a.id);}emit shortcutsChanged();emit settingsChanged();}
void Player::dispatch(QString action){
    if(action=="play")togglePlay();else if(action=="stop")stop();else if(action=="previous")previous();else if(action=="next")next();
    else if(action=="back")seek(position_-10000);else if(action=="forward")seek(position_+10000);
    else if(action=="volumeUp")setVolume(volume_+5);else if(action=="volumeDown")setVolume(volume_-5);else if(action=="mute")setMuted(!muted_);
    else if(action=="fullscreen")toggleFullscreen();else if(action=="open")openDialog();else if(action=="playlist")setPanel("playlist");else if(action=="subtitles")addSubtitleDialog();else if(action=="settings")setPanel("settings");
}
QVariantMap Player::stats() const{
    libvlc_media_stats_t s{};if(media_)api_.libvlc_media_get_stats(media_,&s);
    return {{"displayed",s.i_displayed_pictures},{"dropped",s.i_lost_pictures},{"decoded",s.i_decoded_video},{"callbackFrames",qulonglong(surface_->frames())},{"position",position_},{"duration",duration_}};
}
