#pragma once
#include "../Core/Ffmpeg.hpp"
#include "../Platform/Capture.hpp"
#include <QProcess>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTimer>
#ifdef Q_OS_LINUX
class PortalSession;
#endif

class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent=nullptr);
    ~Recorder() override;
    rec::State state()const{return state_;}
    QString status()const{return status_;}
    QString encoder()const{return encoder_.name;}
    QString lastFile()const{return lastFile_;}
    qint64 durationMs()const{return (completedUs_+segmentUs_)/1000;}
    void start(rec::Target,QRect,rec::Config,QString systemSource
#ifdef Q_OS_LINUX
               ,std::shared_ptr<PortalSession> portal={}
#endif
               );
    void pause();
    void resume();
    void stop();
    void togglePause();
signals:
    void changed();
private:
    enum class Stage {None,Probe,Segment,Concat};
    void transition(rec::State,QString);
    void probe();
    void segment();
    void requestEnd(rec::State,QString);
    void finished(int,QProcess::ExitStatus);
    void progress();
    void finalize();
    void publish(QString source,QString note={});
    void fail(QString,bool retain=false);
    void clearCapture();
    void monitor();
    void launch(Stage,const QStringList&);
    qint64 recordedBytes()const;
    rec::State state_=rec::State::Idle;
    Stage stage_=Stage::None;
    QProcess process_;
    QTimer watchdog_;
    QElapsedTimer progressClock_;
    std::unique_ptr<QTemporaryDir> temp_;
    std::unique_ptr<rec::native::Capture> capture_;
    std::unique_ptr<rec::Feed> video_,audio_;
#ifdef Q_OS_LINUX
    std::shared_ptr<PortalSession> portal_;
#endif
    rec::Target target_;
    QRect region_;
    rec::Config config_;
    rec::Size size_;
    QList<rec::Encoder> candidates_;
    rec::Encoder encoder_;
    QString exe_,systemSource_,status_="Ready",lastFile_,current_,finalFile_,endNote_;
    QStringList parts_;
    QByteArray stdout_,stderr_;
    qint64 completedUs_=0,segmentUs_=0;
    int nextPart_=0;
    bool writtenFrame_=false;
    bool directDxgi_=false,dxgiRejected_=false;
};
