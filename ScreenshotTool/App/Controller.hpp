#pragma once
#include "../Platform/Native.hpp"
#include "Updater.hpp"
#include <QObject>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QSystemTrayIcon>
#include <QMenu>
#include <array>
#ifdef Q_OS_LINUX
#include "../Platform/Portal.hpp"
#endif

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList screens READ screenNames NOTIFY changed)
    Q_PROPERTY(QStringList windows READ windowNames NOTIFY changed)
    Q_PROPERTY(QString status MEMBER status_ NOTIFY changed)
    Q_PROPERTY(QString limitations READ limitations CONSTANT)
    Q_PROPERTY(bool portal READ isPortal CONSTANT)
    Q_PROPERTY(bool busy MEMBER busy_ NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(bool cursor READ cursor WRITE setCursor NOTIFY changed)
    Q_PROPERTY(int delay READ delay WRITE setDelay NOTIFY changed)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY changed)
    Q_PROPERTY(QStringList shortcuts READ shortcuts NOTIFY changed)
    Q_PROPERTY(QString updateFeed READ updateFeed WRITE setUpdateFeed NOTIFY changed)
    Q_PROPERTY(Updater* updater READ updater CONSTANT)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY changed)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY changed)
public:
    explicit Controller(QString data,bool testing=false);
    ~Controller() override;
    QStringList screenNames() const;
    QStringList windowNames() const;
    QString limitations() const{return shot::native::limits();}
    bool isPortal() const{return shot::native::wayland();}
    QString theme() const{return settings_.value("theme","system").toString();}
    bool cursor() const{return settings_.value("cursor",false).toBool();}
    int delay() const{return settings_.value("delay",0).toInt();}
    bool autostart() const{return shot::native::autostartEnabled();}
    QStringList shortcuts() const{return keys_;}
    QString updateFeed() const{return settings_.value("updateFeed").toString();}
    Updater* updater();
    int imageWidth() const{return image_.width();}
    int imageHeight() const{return image_.height();}
    void setTheme(QString);
    void setCursor(bool);
    void setDelay(int);
    void setAutostart(bool);
    void setUpdateFeed(QString);
    Q_INVOKABLE void setShortcuts(QString region,QString screen,QString window);
    Q_INVOKABLE void refreshTargets();
    Q_INVOKABLE void showSettings();
    Q_INVOKABLE void capture(int mode,int index=-1);
    Q_INVOKABLE void finishRegion(double x,double y,double w,double h,double viewW,double viewH);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void save(QUrl);
    Q_INVOKABLE void uiClosed();
    void command(const QString&);
    bool testView(const QString&,const QImage&);
signals:
    void changed();
private:
    QString data_;
    QSettings settings_;
    QString status_;
    QStringList keys_;
    QList<shot::Target> screens_,windows_;
    std::array<std::unique_ptr<shot::native::Hotkey>,3> hotkeys_;
    std::unique_ptr<Updater> updater_;
    std::unique_ptr<QSystemTrayIcon> tray_;
    std::unique_ptr<QMenu> menu_;
    QQmlApplicationEngine* engine_=nullptr;
    QImage image_;
    QThreadPool pool_;
    quint64 generation_=0;
    bool busy_=false,inFlight_=false,testing_=false;
    QString view_;
#ifdef Q_OS_LINUX
    std::unique_ptr<Portal> portal_;
#endif
    void status(QString);
    bool bind(QStringList);
    void closeUi();
    bool showUi(QString,QRect screenGeometry={});
    void captured(shot::Result,int,const shot::Target&,quint64);
    void present();
    int activeScreen() const;
};
