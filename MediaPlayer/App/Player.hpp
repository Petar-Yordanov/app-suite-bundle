#pragma once
#include "../Core/VlcApi.hpp"
#include "../Core/Files.hpp"
#include "VideoSurface.hpp"
#include "Updater.hpp"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QMap>
#include <memory>

class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY stateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY timelineChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY timelineChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY timelineChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(int audioTrack READ audioTrack NOTIFY tracksChanged)
    Q_PROPERTY(int subtitleTrack READ subtitleTrack NOTIFY tracksChanged)
    Q_PROPERTY(QString aspect READ aspect WRITE setAspect NOTIFY settingsChanged)
    Q_PROPERTY(QStringList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(int playlistIndex READ playlistIndex NOTIFY playlistChanged)
    Q_PROPERTY(QString panel READ panel NOTIFY panelChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList shortcutActions READ shortcutActions NOTIFY settingsChanged)
    Q_PROPERTY(QString updateFeed READ updateFeed WRITE setUpdateFeed NOTIFY settingsChanged)
    Q_PROPERTY(Updater* updater READ updater CONSTANT)
    Q_PROPERTY(QString rendering READ rendering CONSTANT)
public:
    Player(QString profile,VideoSurface*,QWidget* dialogParent=nullptr,bool quiet=false);
    ~Player() override;
    QString title() const;
    QString status() const{return status_;}
    bool playing() const{return phase_==Phase::Playing;}
    bool busy() const{return phase_==Phase::Parsing||phase_==Phase::Starting;}
    bool hasMedia() const{return !current_.isEmpty();}
    qint64 position() const{return position_;}
    qint64 duration() const{return duration_;}
    bool seekable() const{return seekable_;}
    int volume() const{return volume_;}
    bool muted() const{return muted_;}
    QVariantList audioTracks() const{return audioTracks_;}
    QVariantList subtitleTracks() const{return subtitleTracks_;}
    int audioTrack() const{return audioTrack_;}
    int subtitleTrack() const{return subtitleTrack_;}
    QString aspect() const{return aspect_;}
    QStringList playlist() const{return playlist_;}
    int playlistIndex() const{return playlistIndex_;}
    QString panel() const{return panel_;}
    bool fullscreen() const{return fullscreen_;}
    QString theme() const{return settings_.value("theme","system").toString();}
    bool remember() const{return settings_.value("remember",true).toBool();}
    QString updateFeed() const{return settings_.value("updateFeed").toString();}
    Updater* updater();
    QString rendering() const{return surface_->software()?"Software frame output":"Native libVLC video output";}
    QVariantList shortcutActions() const;
    QMap<QString,QString> shortcuts() const{return shortcuts_;}
    QString currentPath() const{return current_;}
    QString engineVersion() const{return instance_?api_.version():QString();}
    QVariantMap stats() const;
    void openUrls(const QList<QUrl>&);
    bool checkEngine(QString& error);
    void setFullscreenState(bool);
    void dispatch(QString action);
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void addDialog();
    Q_INVOKABLE void addSubtitleDialog();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(double milliseconds);
    Q_INVOKABLE void setVolume(int);
    Q_INVOKABLE void setMuted(bool);
    Q_INVOKABLE void setAudioTrack(int);
    Q_INVOKABLE void setSubtitleTrack(int);
    Q_INVOKABLE void setAspect(QString);
    Q_INVOKABLE void playIndex(int);
    Q_INVOKABLE void removeIndex(int);
    Q_INVOKABLE void moveIndex(int,int);
    Q_INVOKABLE void clearPlaylist();
    Q_INVOKABLE void setPanel(QString);
    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void setTheme(QString);
    Q_INVOKABLE void setRemember(bool);
    Q_INVOKABLE void setUpdateFeed(QString);
    Q_INVOKABLE void setShortcut(QString,QString);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE void openDropped(QVariantList urls);
    bool addSubtitle(const QUrl&);
    void appendUrls(const QList<QUrl>&);
signals:
    void stateChanged();
    void timelineChanged();
    void volumeChanged();
    void tracksChanged();
    void playlistChanged();
    void panelChanged();
    void fullscreenChanged();
    void settingsChanged();
    void shortcutsChanged();
    void requestFullscreen();
private:
    enum class Phase {Idle,Parsing,Starting,Playing,Paused,Stopped,Ended,Error};
    VlcApi api_;
    libvlc_instance_t* instance_=nullptr;
    libvlc_media_player_t* mp_=nullptr;
    libvlc_media_t* media_=nullptr;
    VideoSurface* surface_;
    QWidget* parent_;
    QString profile_;
    QSettings settings_;
    media::ResumeStore resume_;
    std::unique_ptr<Updater> updater_;
    QTimer timer_;
    QElapsedTimer phaseTime_,refreshTime_,saveTime_;
    Phase phase_=Phase::Idle;
    QString current_,status_="Open a local video",aspect_="Original",panel_;
    QStringList playlist_,navigation_;
    int playlistIndex_=-1,volume_=80,audioTrack_=-1,subtitleTrack_=-1;
    bool muted_=false,fullscreen_=false,seekable_=false,quiet_=false,verifiedVideo_=false,transitioning_=false;
    qint64 position_=0,duration_=0,resumeAt_=0;
    bool resumePending_=false,applyAudio_=false;
    QVariantList audioTracks_,subtitleTracks_;
    QMap<QString,QString> shortcuts_;
    void setPhase(Phase,QString);
    bool load(QString,bool resume=true);
    void startPlayback();
    void tick();
    bool hasVideoTrack(unsigned* count=nullptr) const;
    void releaseMedia();
    void fail(QString,bool skipCandidate=false);
    void savePosition();
    void refreshTracks();
    void applyAudio();
    void navigate(int);
    void advanceNavigation();
    void resetTimeline();
};
