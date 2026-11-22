#include "Feed.hpp"
#include <QHostAddress>
#include <QMutexLocker>
#include <algorithm>
#include <cstring>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#endif

namespace rec {
#ifdef Q_OS_WIN
namespace {
bool ownedByEncoder(QTcpSocket* socket,qint64 pid){
    if(pid<=0)return false;
    DWORD bytes=0;
    GetExtendedTcpTable(nullptr,&bytes,FALSE,AF_INET,TCP_TABLE_OWNER_PID_ALL,0);
    if(bytes==0||bytes>8*1024*1024)return false;
    QByteArray storage(bytes,Qt::Uninitialized);
    if(GetExtendedTcpTable(storage.data(),&bytes,FALSE,AF_INET,TCP_TABLE_OWNER_PID_ALL,0)!=NO_ERROR)return false;
    const auto* table=reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(storage.constData());
    for(DWORD i=0;i<table->dwNumEntries;++i){
        const auto& row=table->table[i];
        if(row.dwOwningPid==DWORD(pid)&&ntohs(u_short(row.dwLocalPort))==socket->peerPort()&&
           ntohs(u_short(row.dwRemotePort))==socket->localPort()&&row.dwState==MIB_TCP_STATE_ESTAB)return true;
    }
    return false;
}
}
#endif
void Mailbox::video(QByteArray data){QMutexLocker lock(&mutex_);video_=std::move(data);}
void Mailbox::audio(const char* data,qsizetype n){
    constexpr qsizetype cap=48000*8/2;
    if(n<=0)return;
    n-=n%8;
    QMutexLocker lock(&mutex_);
    if(n>cap){data+=n-cap;n=cap;}
    if(audio_.size()+n>cap)audio_.remove(0,audio_.size()+n-cap);
    audio_.append(data,n);
}
void Mailbox::fail(QString message){QMutexLocker lock(&mutex_);if(error_.isEmpty())error_=std::move(message);}
QByteArray Mailbox::frame(){QMutexLocker lock(&mutex_);return video_;}
QByteArray Mailbox::samples(qsizetype n){
    QMutexLocker lock(&mutex_);
    QByteArray out(n,'\0');
    auto count=std::min(n,audio_.size());
    if(count){std::memcpy(out.data(),audio_.constData(),size_t(count));audio_.remove(0,count);}
    return out;
}
QString Mailbox::error(){QMutexLocker lock(&mutex_);return error_;}
Feed::Feed(std::shared_ptr<Mailbox> mailbox,bool video,int fps,QObject* parent)
    :QObject(parent),mailbox_(std::move(mailbox)),video_(video),fps_(fps){
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(video_?std::max(1,1000/fps_):10);
    connect(&timer_,&QTimer::timeout,this,&Feed::pump);
    connect(&server_,
#ifdef Q_OS_WIN
        &QTcpServer::newConnection,
#else
        &QLocalServer::newConnection,
#endif
        this,[this]{
        while(auto* incoming=server_.nextPendingConnection()){
            if(socket_
#ifdef Q_OS_WIN
                ||!ownedByEncoder(incoming,authorizedPid_)
#endif
            ){incoming->abort();incoming->deleteLater();continue;}
            socket_=incoming;
#ifdef Q_OS_WIN
            socket_->setSocketOption(QAbstractSocket::LowDelayOption,1);
            connect(socket_,&QTcpSocket::disconnected,this,[this]{timer_.stop();});
#else
            connect(socket_,&QLocalSocket::disconnected,this,[this]{timer_.stop();});
#endif
            // Remove stale audio collected before FFmpeg attached.
            if(!video_)mailbox_->samples(48000*8/2);
            clock_.start();sentSamples_=0;timer_.start();pump();
        }
    });
}
bool Feed::open(){
#ifdef Q_OS_WIN
    return server_.listen(QHostAddress::LocalHost);
#else
    if(!localDir_.isValid())return false;
    // User-only socket inside a private temporary directory, not a network port.
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    return server_.listen(localDir_.filePath("stream"));
#endif
}
QString Feed::url()const{
#ifdef Q_OS_WIN
    return QString("tcp://127.0.0.1:%1").arg(server_.serverPort());
#else
    return "unix:"+server_.fullServerName();
#endif
}
void Feed::pump(){
    const auto error=mailbox_->error();
    if(!error.isEmpty()){timer_.stop();emit fault(error);return;}
    if(!socket_||socket_->state()!=
#ifdef Q_OS_WIN
        QAbstractSocket::ConnectedState
#else
        QLocalSocket::ConnectedState
#endif
    )return;
    if(video_){
        auto data=mailbox_->frame();
        if(data.isEmpty()||socket_->bytesToWrite()>data.size())return;
        if(socket_->write(data)!=data.size()){timer_.stop();emit fault("Video pipe write failed.");}
    }else{
        qint64 due=clock_.elapsed()*48-sentSamples_;
        if(due<=0)return;
        if(due>12000){sentSamples_+=due-12000;due=12000;}
        if(socket_->bytesToWrite()>48000*8/2)return;
        const auto data=mailbox_->samples(due*8);
        if(socket_->write(data)!=data.size()){timer_.stop();emit fault("Audio pipe write failed.");return;}
        sentSamples_+=due;
    }
}
}
