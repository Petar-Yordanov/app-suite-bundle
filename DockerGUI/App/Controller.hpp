#pragma once
#include "../Core/Backend.hpp"
#include "ResourceModel.hpp"
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QSet>
#include <memory>

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(ResourceModel* resources READ resources CONSTANT)
    Q_PROPERTY(QString page READ page NOTIFY changed)
    Q_PROPERTY(QString context READ context NOTIFY changed)
    Q_PROPERTY(QVariantList contexts READ contexts NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY changed)
    Q_PROPERTY(QString refreshKey READ refreshKey WRITE setRefreshKey NOTIFY changed)
    Q_PROPERTY(QString settingsKey READ settingsKey WRITE setSettingsKey NOTIFY changed)
    Q_PROPERTY(QString feed READ feed WRITE setFeed NOTIFY changed)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY changed)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filesChanged)
    Q_PROPERTY(QString fileStatus READ fileStatus NOTIFY filesChanged)
public:
    explicit Controller(QString settingsPath,QObject* parent=nullptr,std::unique_ptr<Backend> backend={});
    ~Controller() override;
    ResourceModel* resources(){return &model_;}
    QString page()const{return page_;} QString context()const{return context_;}
    QVariantList contexts()const{return contexts_;} QString status()const{return status_;}
    QString output()const{return output_;} bool busy()const{return job_!=nullptr;}
    bool online()const{return online_;} QString theme()const{return settings_.value("theme","System").toString();}
    int interval()const{return settings_.value("interval",5).toInt();}
    QString refreshKey()const{return settings_.value("refreshKey","Ctrl+R").toString();}
    QString settingsKey()const{return settings_.value("settingsKey","Ctrl+,").toString();}
    QString feed()const{return settings_.value("feed").toString();}
    bool autoStart()const{return settings_.value("autoStart",true).toBool();}
    QVariantList files()const{return files_;}
    QString filePath()const{return filePath_;}
    QString fileStatus()const{return fileStatus_;}
    void setTheme(QString);void setInterval(int);void setRefreshKey(QString);void setSettingsKey(QString);void setFeed(QString);void setAutoStart(bool);
    Q_INVOKABLE void discover();
    Q_INVOKABLE void switchContext(QString);
    Q_INVOKABLE void selectPage(QString);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setVisible(bool);
    Q_INVOKABLE void stopOutput();
    Q_INVOKABLE void startEngine(QString wsl);
    Q_INVOKABLE void action(QString verb,QVariantMap row,QVariantMap fields);
    Q_INVOKABLE void terminal(QString id,QString shell);
    Q_INVOKABLE void browse(QString id,QString path);
    Q_INVOKABLE void closeBrowser();
signals:
    void changed();void outputChanged();void feedChanged();void filesChanged();
private:
    void say(QString);void append(QString);void clearOutput();
    void execute(QStringList,bool stream=false,QByteArray input={},bool secret=false);
    void updateRows(QJsonArray);
    void engineFailed(QString);
    QString endpoint() const;
    QStringList composeArgs(const QVariantMap&,const QVariantMap&)const;
    bool credentialHelper(QString registry)const;
    QSettings settings_;
    std::unique_ptr<Backend> backend_;
    Backend& docker_;
    ResourceModel model_;
    QTimer poll_,outputTimer_;
    QString page_="Containers",context_,status_="Discovering Docker...",output_,pending_;
    QVariantList contexts_;
    QPointer<QProcess> job_,logs_,browser_;
    bool loading_=false,discovering_=false,visible_=true,online_=false,starting_=false,closing_=false;
    quint64 generation_=0,logGeneration_=0;
    QSet<QString> attempted_;
    QVariantList files_;
    QString filePath_="/",fileStatus_;
    quint64 browseGeneration_=0;
};
