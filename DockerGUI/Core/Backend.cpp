#include "Backend.hpp"
#include "Http.hpp"
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QStringDecoder>
#include <QVariant>
#include <memory>

QString DockerBackend::executable() const {
    auto exe=QStandardPaths::findExecutable("docker");
    if(!exe.isEmpty()) return exe;
#ifdef Q_OS_MACOS
    for(auto p:{QString("/opt/homebrew/bin/docker"),QString("/usr/local/bin/docker"),QDir::homePath()+"/.docker/bin/docker",QString("/Applications/Docker.app/Contents/Resources/bin/docker")})
        if(QFileInfo(p).isExecutable()) return p;
#elif defined(Q_OS_WIN)
    auto p=qEnvironmentVariable("ProgramFiles")+"/Docker/Docker/resources/bin/docker.exe";
    if(QFileInfo(p).isFile()) return p;
#endif
    return {};
}
QStringList DockerBackend::arguments(QStringList args) const {
    if(!context_.isEmpty()) args.prepend(context_),args.prepend("--context");
    return args;
}
QProcess* DockerBackend::process(QObject* owner,QString program,QStringList args,Done done,Stream stream,QByteArray input,int timeout) {
    auto* p=new QProcess(owner);
    auto* timer=new QTimer(p); timer->setSingleShot(true);
    struct State { QByteArray out,err,input; QStringDecoder stdoutDecoder{QStringDecoder::Utf8},stderrDecoder{QStringDecoder::Utf8}; bool limit=false,timeout=false,finished=false; };
    auto state=std::make_shared<State>(); state->input=std::move(input);
    auto drain=[p,state,stream]{
        auto out=p->readAllStandardOutput(),err=p->readAllStandardError();
        if(stream) {
            if(!out.isEmpty())stream(state->stdoutDecoder(out));
            if(!err.isEmpty())stream(state->stderrDecoder(err));
        } else if(out.size() > qsizetype(dock::MaxBody)-state->out.size()) {
            state->limit=true; p->kill();
        } else state->out+=out;
        state->err+=err; if(state->err.size()>65536)state->err=state->err.right(65536);
    };
    auto finish=[p,timer,state,done,drain](bool ok,QString error){
        if(state->finished)return;
        state->finished=true;timer->stop();drain();
        state->input.fill('\0'); state->input.clear();
        if(state->limit)error="Docker output exceeded 16 MiB; narrow the request.";
        else if(state->timeout)error="Docker operation timed out.";
        else if(!ok&&!state->err.isEmpty())error=QString::fromUtf8(state->err).trimmed();
        done({ok&&!state->limit&&!state->timeout,std::move(state->out),error});
        p->deleteLater();
    };
    QObject::connect(p,&QProcess::readyReadStandardOutput,p,drain);
    QObject::connect(p,&QProcess::readyReadStandardError,p,drain);
    QObject::connect(p,&QProcess::started,p,[p,state]{
        if(!state->input.isEmpty())p->write(state->input);
        state->input.fill('\0');state->input.clear();p->closeWriteChannel();
    });
    QObject::connect(p,&QProcess::errorOccurred,p,[p,finish](QProcess::ProcessError e){
        if(e==QProcess::FailedToStart)finish(false,p->errorString());
    });
    QObject::connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),p,[finish](int code,QProcess::ExitStatus status){
        finish(code==0&&status==QProcess::NormalExit,QString("Docker exited (%1)").arg(code));
    });
    QObject::connect(timer,&QTimer::timeout,p,[p,state]{state->timeout=true;p->kill();});
    if(timeout>0)timer->start(timeout);
    // Start on the next event loop turn so callers can retain the handle before callbacks.
    QTimer::singleShot(0,p,[p,program,args,finish]{
        if(p->property("cancelRequested").toBool()){finish(false,"Cancelled.");return;}
        p->start(program.isEmpty()?QString("docker-not-installed"):program,args);
    });
    return p;
}
QProcess* DockerBackend::run(QStringList args,Done done,Stream stream,QByteArray input,int timeout){
    return process(this,executable(),arguments(std::move(args)),std::move(done),std::move(stream),std::move(input),timeout);
}
QJsonArray DockerBackend::jsonRows(const QByteArray& data){
    auto doc=QJsonDocument::fromJson(data);
    if(doc.isArray())return doc.array();
    QJsonArray result;
    if(doc.isObject()){result.append(doc.object());return result;}
    for(auto line:data.split('\n')){
        auto item=QJsonDocument::fromJson(line);
        if(item.isObject())result.append(item.object());
    }
    return result;
}
void DockerBackend::containers(Done done){
    localGet("/containers/json?all=1",{"container","ls","-a","--no-trunc","--format","{{json .}}"},done);
}
void DockerBackend::resources(QString kind,Done done){
    if(kind=="Images")localGet("/images/json",{"image","ls","--no-trunc","--format","{{json .}}"},done);
    else if(kind=="Volumes")localGet("/volumes",{"volume","ls","--format","{{json .}}"},done,true);
    else if(kind=="Networks")localGet("/networks",{"network","ls","--no-trunc","--format","{{json .}}"},done);
    else done({false,{},"Unknown resource type"});
}
void DockerBackend::localGet(QString target,QStringList fallback,Done done,bool volumeObject){
    auto cli=[this,done,fallback]{run(fallback,done);};
#ifndef Q_OS_WIN
    // Only use a context's explicit local Unix socket. SSH/TLS and environment
    // overrides are delegated to Docker CLI's own authenticated transport.
    if(!endpoint_.startsWith("unix:///") || qEnvironmentVariableIsSet("DOCKER_API_VERSION") ||
       qEnvironmentVariableIsSet("DOCKER_HOST") || qEnvironmentVariableIsSet("DOCKER_TLS_VERIFY")){cli();return;}
    auto path=endpoint_.mid(7);
    auto* watcher=new QFutureWatcher<Result>(this);
    const auto requestedContext=context_;
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,done,cli,requestedContext]{
        auto result=watcher->result();watcher->deleteLater();
        if(context_!=requestedContext){done({false,{},"Context changed"});return;}
        if(result.ok)done(std::move(result));else cli();
    });
    watcher->setFuture(QtConcurrent::run([path,target,volumeObject]()->Result{
        QLocalSocket socket; socket.setReadBufferSize(64*1024); socket.connectToServer(path);
        if(!socket.waitForConnected(1500))return {};
        socket.write("GET "+target.toLatin1()+" HTTP/1.1\r\nHost: docker\r\nConnection: close\r\n\r\n");
        if(socket.bytesToWrite()&&!socket.waitForBytesWritten(1000))return {};
        QByteArray wire; QElapsedTimer clock;clock.start();
        while(clock.elapsed()<8000){
            if(!socket.bytesAvailable()&&socket.state()==QLocalSocket::ConnectedState)socket.waitForReadyRead(250);
            wire+=socket.readAll();
            if(wire.size()>qsizetype(dock::MaxBody+65536))return {};
            dock::Response response;
            auto parsed=dock::decode({wire.constData(),size_t(wire.size())},response,socket.state()==QLocalSocket::UnconnectedState);
            if(parsed==dock::Parse::invalid)return {};
            if(parsed==dock::Parse::complete){
                if(response.status!=200)return {};
                QByteArray body(response.body.data(),qsizetype(response.body.size()));
                auto doc=QJsonDocument::fromJson(body);
                if(volumeObject&&doc.isObject())body=QJsonDocument(doc.object().value("Volumes").toArray()).toJson(QJsonDocument::Compact);
                else if(!doc.isArray())return {};
                return {true,body,{}};
            }
        }
        return {};
    }));
#else
    cli();
#endif
}
