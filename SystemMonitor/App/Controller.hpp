#pragma once
#include "Models.hpp"
#include "Management.hpp"
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QFutureWatcher>
#include <QThreadPool>
#include <memory>

class Controller:public QObject {
    Q_OBJECT
    Q_PROPERTY(ProcessProxy* processes READ processes CONSTANT)
    Q_PROPERTY(MetricsModel* metrics READ metrics CONSTANT)
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(int processCount READ processCount NOTIFY statusChanged)
    Q_PROPERTY(int coreCount READ coreCount CONSTANT)
public:
    explicit Controller(QString path,QObject* parent=nullptr);
    ~Controller()override;
    ProcessProxy* processes(){return &proxy_;}
    MetricsModel* metrics(){return &metrics_;}
    QVariantMap options()const{return options_;}
    QVariantMap selected()const{return model_.lookup(pid_,identity_);}
    QVariantList entries()const{return entries_;}
    QString status()const{return status_;}
    QString warning()const{return warning_;}
    bool busy()const{return actionBusy_;}
    int processCount()const{return model_.rowCount();}
    int coreCount()const{return int(sm::cpuCount());}
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(qulonglong pid,QString identity);
    Q_INVOKABLE void setOption(QString key,QVariant value);
    Q_INVOKABLE void processAction(QString action,int priority,QString cores,bool elevated);
    Q_INVOKABLE void loadEntries(QString type);
    Q_INVOKABLE void entryAction(QString type,QString id,QString op);
    Q_INVOKABLE void raisePriority();
signals:
    void optionsChanged();void selectedChanged();void entriesChanged();void statusChanged();void feedChanged();
private:
    void setStatus(QString s);void complete(sm::Snapshot);
    QSettings settings_;QVariantMap options_;
    ProcessModel model_;ProcessProxy proxy_;MetricsModel metrics_;
    std::shared_ptr<sm::Sampler> sampler_=std::make_shared<sm::Sampler>();
    QThreadPool collectPool_,actionPool_;
    QFutureWatcher<sm::Snapshot> sampleWatcher_;
    QFutureWatcher<sm::Result> actionWatcher_;
    QFutureWatcher<QVariantList> entriesWatcher_;
    QTimer timer_;
    int tick_=0;bool sampling_=false,actionBusy_=false,entriesBusy_=false;
    sm::U pid_=0;QString identity_,status_,warning_,entryType_,pendingType_;
    QVariantList entries_;
};
