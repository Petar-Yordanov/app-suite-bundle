#include "Downloads.hpp"
#include "../Core/Rules.hpp"
#include <QUuid>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#ifndef Q_OS_IOS
#include <QFileDialog>
#endif
namespace {
// Only retain ordinary HTTP(S) GET-like destinations. Query strings can
// contain session credentials or signed tokens; never persist them here.
QUrl retryable(QUrl u){
    if(!u.isValid()||u.host().isEmpty()||u.hasQuery()||u.hasFragment()||!u.userInfo().isEmpty()||(u.scheme()!="http"&&u.scheme()!="https")||u.toEncoded().size()>4096)return {};
    return u;
}
}
Downloads::Downloads(QString directory,QObject* p):QAbstractListModel(p){
    timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,&Downloads::poll);
    if(directory.isEmpty())return;
    historyPath_=QDir(directory).filePath("downloads.json");QFile file(historyPath_);
    if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)return;
    auto entries=QJsonDocument::fromJson(file.readAll()).toVariant().toList();
    for(const auto& value:entries){auto m=value.toMap();if(rows_.size()>=200)break;
        if(m["id"].toString().isEmpty()||m["privateMode"].toBool())continue;
        if(!m["finished"].toBool())m["state"]="Interrupted (browser closed)";
        m["finished"]=true;m["canCancel"]=false;m["canPause"]=false;m["canResume"]=false;
        m["canRetry"]=m["state"]!="Completed"&&m["state"]!="Cancelled"&&!retryable(QUrl(m["retryUrl"].toString())).isEmpty();
        rows_.append(Row{m,{},{}});
    }
}
Downloads::~Downloads(){save();}
void Downloads::save()const{
    if(historyPath_.isEmpty())return;QVariantList history;
    for(const auto& row:rows_)if(!row.fields["privateMode"].toBool())history.append(row.fields);
    const auto data=QJsonDocument::fromVariant(history).toJson(QJsonDocument::Compact);
    if(data.size()>1024*1024)return;QSaveFile file(historyPath_);if(file.open(QIODevice::WriteOnly)&&file.write(data)==data.size())file.commit();
}
int Downloads::index(QString id)const{for(int i=0;i<rows_.size();++i)if(rows_[i].fields["id"].toString()==id)return i;return -1;}
QVariant Downloads::data(const QModelIndex& i,int r)const{return i.isValid()&&i.row()>=0&&i.row()<rows_.size()&&r==Qt::UserRole+1?rows_[i.row()].fields:QVariant{};}
QString Downloads::add(QString n,QString path,bool p){
    if(rows_.size()>=200)clearFinished();if(rows_.size()>=200)return {};
    auto id=QUuid::createUuid().toString();beginInsertRows({},rows_.size(),rows_.size());
    rows_.append(Row{{{"id",id},{"name",n},{"path",path},{"privateMode",p},{"state","Waiting"},{"received",0},{"total",-1},{"canPause",false},{"canResume",false},{"canCancel",true},{"finished",false}}, {},{}});endInsertRows();if(!p)save();return id;
}
void Downloads::update(QString id,QVariantMap data){int i=index(id);if(i<0)return;auto& m=rows_[i].fields;bool changed=false,wasFinished=m["finished"].toBool();for(auto it=data.begin();it!=data.end();++it)if(m.value(it.key())!=it.value()){m[it.key()]=it.value();changed=true;}if(changed)emit dataChanged(this->index(i,0),this->index(i,0));if(!wasFinished&&m["finished"].toBool()&&!m["privateMode"].toBool())save();}
void Downloads::handler(QString id,std::function<void(QString)> f){int i=index(id);if(i>=0)rows_[i].act=std::move(f);}
void Downloads::action(QString id,QString a){int i=index(id);if(i<0)return;if(a=="retry"){
    const auto& fields=rows_[i].fields;if(!fields["canRetry"].toBool()||fields["privateMode"].toBool())return;
    auto u=retryable(QUrl(fields["retryUrl"].toString()));if(!u.isEmpty())emit retryRequested(u);return;
}if(a=="folder"){if(rows_[i].fields["state"]=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(rows_[i].fields["path"].toString()).absolutePath()));return;}auto f=rows_[i].act;if(f)f(a);}
void Downloads::clearFinished(){for(int i=rows_.size()-1;i>=0;--i)if(rows_[i].fields["finished"].toBool()){beginRemoveRows({},i,i);rows_.removeAt(i);endRemoveRows();}save();}
void Downloads::clearPrivate(){for(int i=rows_.size()-1;i>=0;--i)if(rows_[i].fields["privateMode"].toBool()){if(auto f=rows_[i].act)f("cancel");beginRemoveRows({},i,i);rows_.removeAt(i);endRemoveRows();}}
void Downloads::acceptDesktop(QObject* d,bool p,QString directory){
#ifndef Q_OS_IOS
    if(!d)return;QPointer<QObject> guard(d);
    auto n=QString::fromStdString(web::safeFilename(d->property("suggestedFileName").toString().toStdString()));
    if(directory.isEmpty()||!QDir(directory).exists())directory=QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    auto path=QFileDialog::getSaveFileName(nullptr,"Save download",QDir(directory).filePath(n));
    if(!guard)return;if(path.isEmpty()){QMetaObject::invokeMethod(d,"cancel");return;}
    auto id=add(QFileInfo(path).fileName(),path,p);if(id.isEmpty()){QMetaObject::invokeMethod(d,"cancel");return;}
    d->setProperty("downloadDirectory",QFileInfo(path).absolutePath());d->setProperty("downloadFileName",QFileInfo(path).fileName());
    rows_[index(id)].object=d;
    rows_[index(id)].desktop=true;
    if(!p){auto u=retryable(d->property("url").toUrl());if(!u.isEmpty()){update(id,{{"retryUrl",u.toString(QUrl::FullyEncoded)}});save();}}
    handler(id,[guard](QString a){if(guard&&(a=="pause"||a=="resume"||a=="cancel"))QMetaObject::invokeMethod(guard,a.toLatin1().constData());});
    QMetaObject::invokeMethod(d,"accept");timer_.start();poll();
#else
    Q_UNUSED(d);Q_UNUSED(p);Q_UNUSED(directory);
#endif
}
void Downloads::poll(){
    bool active=false;
    for(auto& row:rows_){auto d=row.object;auto id=row.fields["id"].toString();if(!d){if(row.desktop&&!row.fields["finished"].toBool()){update(id,{{"state","Download request ended"},{"finished",true},{"canCancel",false},{"canPause",false},{"canResume",false}});row.act={};}continue;}int state=d->property("state").toInt();bool paused=d->property("isPaused").toBool();bool finished=d->property("isFinished").toBool();
        QString status=state==2?"Completed":state==3?"Cancelled":state==4?d->property("interruptReasonString").toString():paused?"Paused":state==1?"Downloading":"Waiting";
        bool resumable=state==4&&!finished;active|=!finished;
        update(id,{{"received",d->property("receivedBytes")},{"total",d->property("totalBytes")},{"state",status},{"finished",finished},{"canCancel",!finished},{"canPause",state==1&&!paused},{"canResume",paused||resumable},{"canRetry",finished&&state==4&&!retryable(QUrl(row.fields["retryUrl"].toString())).isEmpty()}});
        if(finished){row.act={};row.object=nullptr;}
    }
    if(!active)timer_.stop();
}
