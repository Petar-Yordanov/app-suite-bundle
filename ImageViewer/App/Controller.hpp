#pragma once
#include "../Core/Decoder.hpp"
#include "Updater.hpp"
#include <QCache>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <memory>

struct ImageBuffer {
    QMutex mutex;
    QImage image;
};
class ImageProvider final: public QQuickImageProvider {
public:
    explicit ImageProvider(std::shared_ptr<ImageBuffer> buffer):QQuickImageProvider(QQuickImageProvider::Image),buffer_(std::move(buffer)){}
    QImage requestImage(const QString&,QSize* size,const QSize&) override;
private:
    std::shared_ptr<ImageBuffer> buffer_;
};

class Controller final: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY pathChanged)
    Q_PROPERTY(QString imageUrl READ imageUrl NOTIFY imageChanged)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(int originalWidth READ originalWidth NOTIFY infoChanged)
    Q_PROPERTY(int originalHeight READ originalHeight NOTIFY infoChanged)
    Q_PROPERTY(bool reduced READ reduced NOTIFY infoChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool animated READ animated NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int index READ index NOTIFY navigationChanged)
    Q_PROPERTY(int count READ count NOTIFY navigationChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY navigationChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY navigationChanged)
    Q_PROPERTY(QVariantList metadata READ metadata NOTIFY infoChanged)
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY preferencesChanged)
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY preferencesChanged)
    Q_PROPERTY(QString updateFeed READ updateFeed NOTIFY preferencesChanged)
    Q_PROPERTY(Updater* updater READ updater CONSTANT)
public:
    explicit Controller(QString dataDirectory,QObject* parent=nullptr);
    ~Controller() override;
    std::shared_ptr<ImageBuffer> buffer() const {return buffer_;}
    QString path()const{return path_;}
    QString fileName()const;
    QString imageUrl()const;
    int imageWidth()const{return displayed_.width();}
    int imageHeight()const{return displayed_.height();}
    int originalWidth()const{return original_.width();}
    int originalHeight()const{return original_.height();}
    bool reduced()const{return reduced_;}
    bool busy()const{return loading_;}
    bool animated()const{return bool(animation_);}
    bool playing()const{return animated()&&play_;}
    QString status()const{return status_;}
    int index()const{return files_.indexOf(path_);}
    int count()const{return int(files_.size());}
    bool hasPrevious()const{return index()>0;}
    bool hasNext()const{return index()>=0&&index()+1<count();}
    QVariantList metadata()const{return metadata_;}
    QStringList nameFilters()const;
    QString theme()const{return settings_.value("theme","system").toString();}
    bool systemDark() const;
    QVariantMap shortcuts()const;
    QString updateFeed()const{return settings_.value("updateFeed").toString();}
    Updater* updater(){return &updater_;}
    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void previous();
    Q_INVOKABLE void next();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void moveToTrash(const QString& expectedPath);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void setWindowVisible(bool value);
    Q_INVOKABLE void setTheme(const QString& value);
    Q_INVOKABLE bool setShortcut(const QString& action,const QString& sequence);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE void setUpdateFeed(const QString& url);
    Q_INVOKABLE void reveal();
    Q_INVOKABLE void defaultAppSettings();
    Q_INVOKABLE void imagePresented();
    void enableBenchmark(qint64 processStartMs);
signals:
    void pathChanged();void imageChanged();void infoChanged();void stateChanged();void navigationChanged();void preferencesChanged();
private:
    enum class Work{None,Decode,Scan,Preload,Frame};
    void select(QString path,bool force=false);
    void schedule();
    void dispatchDecode(QString path,quint64 generation,bool preload);
    void dispatchScan(QString directory);
    void dispatchFrame();
    void applyDecoded(imageview::Decoded result);
    void publishImage(QImage image);
    void setStatus(QString text);
    void requestScan();
    void armAnimation(int delay);
    void planPreload();
    static QVariantMap defaults();
    QString directory_,path_,activeDirectory_,status_,currentFingerprint_;
    QSettings settings_;
    Updater updater_;
    std::shared_ptr<ImageBuffer> buffer_=std::make_shared<ImageBuffer>();
    QThreadPool pool_;
    QCache<QString,imageview::Decoded> cache_{int(imageview::CacheBytes/1024)};
    QFileSystemWatcher watcher_;
    QTimer animationTimer_,scanTimer_;
    QStringList files_,preloads_;
    QImage displayed_;
    imageview::Decoded current_;
    QSize original_;
    QVariantList metadata_;
    std::shared_ptr<imageview::Animation> animation_;
    Work work_=Work::None;
    quint64 generation_=0,revision_=0;
    bool wantedDecode_=false,wantedScan_=false,wantedFrame_=false,loading_=false,closing_=false,play_=true,visible_=true,reduced_=false;
    bool benchmark_=false,benchmarkPrinted_=false;
    qint64 startMs_=0;
    QElapsedTimer loadTimer_;
};
