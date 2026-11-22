#include "Downloads.hpp"
#include "../Core/Rules.hpp"
#include <QUuid>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#ifndef Q_OS_IOS
#include <QFileDialog>
#endif
Downloads::Downloads(QObject* p):QAbstractListModel(p){timer_.setInterval(250);connect(&timer_,&QTimer::timeout,this,&Downloads::poll);}
int Downloads::index(QString id)const{for(int i=0;i<rows_.size();++i)if(rows_[i].fields["id"].toString()==id)return i;return -1;}
QVariant Downloads::data(const QModelIndex& i,int r)const{return i.isValid()&&i.row()>=0&&i.row()<rows_.size()&&r==Qt::UserRole+1?rows_[i.row()].fields:QVariant{};}
QString Downloads::add(QString n,QString path,bool p){
    if(rows_.size()>=200)clearFinished();if(rows_.size()>=200)return {};
    auto id=QUuid::createUuid().toString();beginInsertRows({},rows_.size(),rows_.size());
    rows_.append(Row{{{"id",id},{"name",n},{"path",path},{"privateMode",p},{"state","Waiting"},{"received",0},{"total",-1},{"canPause",false},{"canResume",false},{"canCancel",true},{"finished",false}}, {},{}});endInsertRows();return id;
}
void Downloads::update(QString id,QVariantMap data){int i=index(id);if(i<0)return;auto& m=rows_[i].fields;bool changed=false;for(auto it=data.begin();it!=data.end();++it)if(m.value(it.key())!=it.value()){m[it.key()]=it.value();changed=true;}if(changed)emit dataChanged(this->index(i,0),this->index(i,0));}
void Downloads::handler(QString id,std::function<void(QString)> f){int i=index(id);if(i>=0)rows_[i].act=std::move(f);}
void Downloads::action(QString id,QString a){int i=index(id);if(i<0)return;if(a=="folder"){if(rows_[i].fields["state"]=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(rows_[i].fields["path"].toString()).absolutePath()));return;}auto f=rows_[i].act;if(f)f(a);}
void Downloads::clearFinished(){for(int i=rows_.size()-1;i>=0;--i)if(rows_[i].fields["finished"].toBool()){beginRemoveRows({},i,i);rows_.removeAt(i);endRemoveRows();}}
void Downloads::clearPrivate(){for(int i=rows_.size()-1;i>=0;--i)if(rows_[i].fields["privateMode"].toBool()){if(auto f=rows_[i].act)f("cancel");beginRemoveRows({},i,i);rows_.removeAt(i);endRemoveRows();}}
void Downloads::acceptDesktop(QObject* d,bool p){
#ifndef Q_OS_IOS
    if(!d)return;QPointer<QObject> guard(d);
    auto n=QString::fromStdString(web::safeFilename(d->property("suggestedFileName").toString().toStdString()));
    auto path=QFileDialog::getSaveFileName(nullptr,"Save download",QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(n));
    if(!guard)return;if(path.isEmpty()){QMetaObject::invokeMethod(d,"cancel");return;}
    auto id=add(QFileInfo(path).fileName(),path,p);if(id.isEmpty()){QMetaObject::invokeMethod(d,"cancel");return;}
    d->setProperty("downloadDirectory",QFileInfo(path).absolutePath());d->setProperty("downloadFileName",QFileInfo(path).fileName());
    rows_[index(id)].object=d;
    rows_[index(id)].desktop=true;
    handler(id,[guard](QString a){if(guard&&(a=="pause"||a=="resume"||a=="cancel"))QMetaObject::invokeMethod(guard,a.toLatin1().constData());});
    QMetaObject::invokeMethod(d,"accept");timer_.start();poll();
#else
    Q_UNUSED(d);Q_UNUSED(p);
#endif
}
void Downloads::poll(){
    bool active=false;
    for(auto& row:rows_){auto d=row.object;auto id=row.fields["id"].toString();if(!d){if(row.desktop&&!row.fields["finished"].toBool()){update(id,{{"state","Download request ended"},{"finished",true},{"canCancel",false},{"canPause",false},{"canResume",false}});row.act={};}continue;}int state=d->property("state").toInt();bool paused=d->property("isPaused").toBool();bool finished=d->property("isFinished").toBool();
        QString status=state==2?"Completed":state==3?"Cancelled":state==4?d->property("interruptReasonString").toString():paused?"Paused":state==1?"Downloading":"Waiting";
        bool resumable=state==4&&!finished;active|=!finished;
        update(id,{{"received",d->property("receivedBytes")},{"total",d->property("totalBytes")},{"state",status},{"finished",finished},{"canCancel",!finished},{"canPause",state==1&&!paused},{"canResume",paused||resumable}});
        if(finished){row.act={};row.object=nullptr;}
    }
    if(!active)timer_.stop();
}
