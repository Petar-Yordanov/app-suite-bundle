#pragma once
#include "../Core/Store.hpp"
#include "../Platform/Native.hpp"
#include "Updater.hpp"
#include <QObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QClipboard>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <memory>

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString storageSummary READ storageSummary NOTIFY entriesChanged)
    Q_PROPERTY(QString limitations READ limitations CONSTANT)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool settingsBusy READ settingsBusy NOTIFY settingsBusyChanged)
    Q_PROPERTY(Updater* updater READ updater CONSTANT)
public:
    explicit Controller(QString dataDirectory,QObject* parent=nullptr);
    ~Controller() override;
    QVariantList entries() const {return entries_;}
    QVariantMap preferences() const;
    QString status() const {return status_;}
    QString storageSummary() const {return summary_;}
    QString limitations() const {return native::platformLimitations();}
    bool paused() const {return paused_;}
    bool settingsBusy() const {return settingsBusy_;}
    Updater* updater() {return &updater_;}
    bool hasTray() const;
    Q_INVOKABLE void showUi();
    Q_INVOKABLE void hideUi();
    Q_INVOKABLE void search(const QString&);
    Q_INVOKABLE void restore(const QString&);
    Q_INVOKABLE void remove(const QString&);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setPaused(bool);
    Q_INVOKABLE void applySettings(int count,int maxMiB,bool persistent,QString theme,QString shortcut,bool autostart,QString feed);
    Q_INVOKABLE void quit();
signals:
    void entriesChanged();
    void preferencesChanged();
    void statusChanged();
    void pausedChanged();
    void settingsBusyChanged();
private:
    void clipboardChanged();
    void refresh();
    void setStatus(QString);
    void applyShortcut(const QString&);
    void finishRefresh(std::vector<clip::Entry>,std::size_t,std::uint64_t);
    clip::fs::path storePath(bool persistent) const;
    QString directory_;
    QSettings settings_;
    QThread thread_;
    QObject* worker_=nullptr;
    std::unique_ptr<clip::Store> store_; // worker thread exclusively
    native::Hotkey hotkey_;
    Updater updater_;
    QClipboard* clipboard_=nullptr;
    std::unique_ptr<QSystemTrayIcon> tray_;
    std::unique_ptr<QMenu> menu_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QTimer macPoll_,queryTimer_;
    QVariantList entries_;
    QString query_,status_,summary_;
    bool paused_=false,ready_=false,refreshing_=false,refreshDirty_=false,settingsBusy_=false,restoring_=false,writingClipboard_=false,closing_=false,capturing_=false,retryCapture_=false;
    int pendingCaptures_=0;
    quint64 lastSequence_=0,epoch_=0;
};
