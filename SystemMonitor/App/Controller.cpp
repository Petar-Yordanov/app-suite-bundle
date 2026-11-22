#include "Controller.hpp"
#include <QtConcurrent/QtConcurrentRun>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
Controller::Controller(QString path,QObject* parent):QObject(parent),settings_(path,QSettings::IniFormat){
    options_={{"theme","System"},{"interval",1000},{"feed",""},{"refreshKey","F5"},{"searchKey","Ctrl+F"},{"settingsKey","Ctrl+,"}};
    for(auto key:options_.keys())if(settings_.contains(key))options_[key]=settings_.value(key);
    options_["interval"]=qBound(250,options_["interval"].toInt(),10000);
    proxy_.setSourceModel(&model_);proxy_.sort(0,Qt::DescendingOrder);
    collectPool_.setMaxThreadCount(1);collectPool_.setExpiryTimeout(-1);
    actionPool_.setMaxThreadCount(2);actionPool_.setExpiryTimeout(-1);
    connect(&timer_,&QTimer::timeout,this,&Controller::refresh);
    connect(&sampleWatcher_,&QFutureWatcher<sm::Snapshot>::finished,this,[this]{sampling_=false;complete(sampleWatcher_.result());});
    connect(&actionWatcher_,&QFutureWatcher<sm::Result>::finished,this,[this]{
        actionBusy_=false;auto result=actionWatcher_.result();setStatus((result.ok?"":"Failed: ")+QString::fromStdString(result.message));refresh();
        if(!entryType_.isEmpty())loadEntries(entryType_);
    });
    connect(&entriesWatcher_,&QFutureWatcher<QVariantList>::finished,this,[this]{
        entriesBusy_=false;entries_=entriesWatcher_.result();emit entriesChanged();
        if(!pendingType_.isEmpty()){auto type=pendingType_;pendingType_.clear();loadEntries(type);}
    });
    timer_.setInterval(options_["interval"].toInt());timer_.start();
    status_=sm::protectPriority()?"Safe elevated scheduling priority enabled.":"Default scheduling priority (elevation unavailable).";
    QTimer::singleShot(0,this,&Controller::refresh);
}
Controller::~Controller(){timer_.stop();collectPool_.waitForDone();actionPool_.waitForDone();settings_.sync();}
void Controller::setStatus(QString s){status_=std::move(s);emit statusChanged();}
void Controller::refresh(){
    if(sampling_)return;sampling_=true;bool details=(tick_++%5)==0;
    auto sampler=sampler_;sampleWatcher_.setFuture(QtConcurrent::run(&collectPool_,[sampler,details]{
        try{return sampler->sample(details);}
        catch(...){sm::Snapshot failed;failed.valid=false;failed.warning="Sampling failed; preserving the last snapshot. Retrying at the configured interval.";return failed;}
    }));
}
void Controller::complete(sm::Snapshot s){
    warning_=QString::fromStdString(s.warning);if(s.valid){model_.apply(s.processes);metrics_.apply(s.metrics);}
    emit selectedChanged();emit statusChanged();
}
void Controller::select(qulonglong pid,QString identity){pid_=pid;identity_=std::move(identity);emit selectedChanged();}
void Controller::setOption(QString key,QVariant value){
    if(!options_.contains(key))return;
    if(key=="interval")value=qBound(250,value.toInt(),10000);
    else if(key=="theme"&&!QStringList{"Light","Dark","System"}.contains(value.toString()))return;
    else if(key.endsWith("Key")){
        auto s=QKeySequence::fromString(value.toString(),QKeySequence::PortableText);
        if(s.isEmpty()){setStatus("Invalid shortcut");return;}value=s.toString(QKeySequence::PortableText);
        for(auto k:options_.keys())if(k!=key&&k.endsWith("Key")&&options_[k]==value){setStatus("Shortcut already assigned");return;}
    }
    if(options_[key]==value)return;options_[key]=value;settings_.setValue(key,value);settings_.sync();
    if(key=="interval")timer_.setInterval(value.toInt());
    if(key=="feed")emit feedChanged();emit optionsChanged();
}
void Controller::processAction(QString name,int priority,QString cores,bool elevated){
    if(actionBusy_)return;
    bool ok=false;auto id=identity_.toULongLong(&ok);if(!ok||!id||selected().isEmpty()){setStatus("Select an accessible process first.");return;}
    if(pid_==sm::ownPid()||pid_<=1){setStatus("The monitor and core system processes are protected.");return;}
    sm::Request r;r.pid=pid_;r.identity=id;r.priority=priority;
    if(name=="terminate")r.action=sm::Action::Terminate;
    else if(name=="force"||name=="tree")r.action=sm::Action::Force;
    else if(name=="suspend")r.action=sm::Action::Suspend;
    else if(name=="resume")r.action=sm::Action::Resume;
    else if(name=="priority")r.action=sm::Action::Priority;
    else if(name=="affinity"){
        r.action=sm::Action::Affinity;QSet<int> unique;
        for(auto part:cores.split(',',Qt::SkipEmptyParts)){bool valid;int n=part.trimmed().toInt(&valid);if(!valid||n<0||n>=coreCount()){setStatus("Invalid CPU core list");return;}if(!unique.contains(n)){unique.insert(n);r.cores.push_back(n);}}
        if(r.cores.empty()){setStatus("Enter one or more comma-separated CPU indices.");return;}
    }else return;
    auto requests=name=="tree"?sm::processTree(model_.treeSnapshot(),r):std::vector<sm::Request>{r};
    if(requests.empty()){setStatus("Process tree changed.");return;}
    actionBusy_=true;setStatus("Executing process operation…");
    actionWatcher_.setFuture(QtConcurrent::run(&actionPool_,[requests=std::move(requests),elevated,r,name]{
        if(elevated&&name=="tree")return management::elevatedTree(r);
        if(requests.size()==1)return elevated?management::elevated(requests.front()):sm::operate(requests.front());
        unsigned done=0;std::string error;
        for(auto& request:requests){auto result=sm::operate(request);if(result.ok)++done;else error=result.message;}
        return sm::Result{done==requests.size(),std::to_string(done)+"/"+std::to_string(requests.size())+" processes terminated. "+error};
    }));
}
void Controller::loadEntries(QString type){
    if(type!="startup"&&type!="services")return;
    if(entriesBusy_){pendingType_=type;return;}entryType_=type;entriesBusy_=true;entries_.clear();emit entriesChanged();
    entriesWatcher_.setFuture(QtConcurrent::run(&actionPool_,[type]{return type=="startup"?management::startup():management::services();}));
}
void Controller::entryAction(QString type,QString id,QString op){
    if(actionBusy_||type!=entryType_)return;
    bool found=false;for(auto& v:entries_){auto m=v.toMap();if(m.value("id")==id&&m.value("editable").toBool())found=true;}
    if(!found){setStatus("Entry is unavailable or protected.");return;}
    actionBusy_=true;setStatus("Executing "+type+" operation…");actionWatcher_.setFuture(QtConcurrent::run(&actionPool_,[type,id,op]{return management::change(type,id,op);}));
}
void Controller::raisePriority(){
    if(actionBusy_)return;
    actionBusy_=true;setStatus("Requesting safe scheduling priority…");
    actionWatcher_.setFuture(QtConcurrent::run(&actionPool_,[]{return management::raisePriority();}));
}
