#pragma once
#include "Recorder.hpp"
#include "Updater.hpp"
#include <QSettings>
#include <QFutureWatcher>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QPointer>
#include <QQuickWindow>

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList screens READ screens NOTIFY targetsChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY targetsChanged)
    Q_PROPERTY(QVariantList microphones READ microphones NOTIFY devicesChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY devicesChanged)
    Q_PROPERTY(bool systemAvailable READ systemAvailable NOTIFY devicesChanged)
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString duration READ duration NOTIFY changed)
    Q_PROPERTY(QString lastFile READ lastFile NOTIFY changed)
    Q_PROPERTY(QString platformLimits READ platformLimits CONSTANT)
    Q_PROPERTY(QString hotkeyStatus READ hotkeyStatus NOTIFY settingsChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY settingsChanged)
public:
    explicit Controller(const QString& data);
    ~Controller() override;
    QVariantMap settings()const;
    QVariantList screens()const;
    QVariantList windows()const;
    QVariantList microphones()const;
    bool discovering()const{return discovery_.isRunning();}
    bool systemAvailable()const{return !devices_.systemSource.isEmpty();}
    int state()const{return int(recorder_.state());}
    QString status()const{return uiError_.isEmpty()?recorder_.status():uiError_;}
    QString duration()const;
    QString lastFile()const{return recorder_.lastFile();}
    QString platformLimits()const;
    QString hotkeyStatus()const{return hotkeyStatus_;}
    bool dark()const;
    void command(const QString&);
    Q_INVOKABLE void show();
    Q_INVOKABLE void closeUi();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setSetting(const QString&,const QVariant&);
    Q_INVOKABLE void begin(int mode,int index);
    Q_INVOKABLE void finishRegion(double x,double y,double w,double h,double logicalW,double logicalH);
    Q_INVOKABLE void cancelRegion();
    Q_INVOKABLE void pauseResume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openOutput();
    Q_INVOKABLE void openLast();
    Q_INVOKABLE void quit();
    Q_INVOKABLE void setOutputUrl(const QUrl&);
signals:
    void changed();
    void settingsChanged();
    void targetsChanged();
    void devicesChanged();
private:
    rec::Config config()const;
    void start(rec::Target,QRect);
    void bindShortcuts();
    void updateTray();
    void error(QString);
    void showOverlay(const rec::Target&);
#ifdef Q_OS_LINUX
    void choosePortal(int mode);
    void showPortalRegion(const rec::Target&);
    std::shared_ptr<PortalSession> portal_;
#endif
    void destroyOverlay();
    void loadWindow();
    QIcon icon(bool recording)const;
    QSettings prefs_;
    Updater updater_;
    Recorder recorder_;
    rec::native::Hotkey toggleKey_,pauseKey_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction *recordAction_=nullptr,*pauseAction_=nullptr,*durationAction_=nullptr,*quitAction_=nullptr;
    std::unique_ptr<QQmlApplicationEngine> engine_,overlayEngine_;
    QPointer<QQuickWindow> main_,overlay_;
    QFutureWatcher<rec::Devices> discovery_;
    rec::Devices devices_;
    QList<rec::Target> screens_,windows_;
    rec::Target selecting_;
    QString uiError_,hotkeyStatus_;
    bool quitPending_=false,selectionPending_=false,starting_=false;
    int previousState_=0;
};
