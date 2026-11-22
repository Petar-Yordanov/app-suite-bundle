#include "../Core/Backend.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <functional>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(!app.arguments().contains("--run")){
        QTextStream(stdout)<<"Opt in with --run (requires an accessible test Docker engine)."<<Qt::endl;return 0;
    }
    DockerBackend backend;
    auto invoke=[&](QStringList args,int timeout=120000){
        QEventLoop loop;Result result;
        backend.run(args,[&](Result r){result=std::move(r);loop.quit();},{},{},timeout);loop.exec();return result;
    };
    auto run=[&](QStringList args){auto r=invoke(args);if(!r.ok)QTextStream(stderr)<<args.join(' ')<<": "<<r.error<<Qt::endl;return r.ok;};
    auto info=invoke({"info","--format","{{.OSType}}"});
    if(!info.ok||info.output.trimmed()!="linux"){QTextStream(stderr)<<"A running Linux-container Docker engine is required."<<Qt::endl;return 1;}
    auto id="dockergui-ci-"+QUuid::createUuid().toString(QUuid::Id128).left(12);
    auto image=id+":test",container=id+"-container",net=id+"-net",vol=id+"-vol";
    QTemporaryDir tmp; if(!tmp.isValid())return 1;
    QFile dockerfile(tmp.filePath("Dockerfile"));if(!dockerfile.open(QIODevice::WriteOnly))return 1;
    dockerfile.write("FROM alpine:3.20\nRUN echo dockergui > /test-marker\n");dockerfile.close();
    QFile compose(tmp.filePath("compose.yaml"));if(!compose.open(QIODevice::WriteOnly))return 1;
    compose.write("services:\n  test:\n    image: alpine:3.20\n    command: [sleep, '120']\n");compose.close();
    QStringList comp={"compose","-p",id,"-f",compose.fileName()};
    bool ok=true;
    auto step=[&](QStringList args){if(ok)ok=run(args);};
    step({"pull","alpine:3.20"});
    step({"build","-t",image,tmp.path()});
    step({"volume","create",vol});step({"network","create",net});
    step({"container","create","--name",container,"--network",net,"--mount","type=volume,source="+vol+",target=/data",
        "--env","TEST=ok","--restart","no",image,"sh","-c","echo marker; sleep 120"});
    step({"container","start",container});
    if(ok){
        auto r=invoke({"context","inspect","--format","{{json .Endpoints.docker.Host}}"});
        QString endpoint;
        auto doc=QJsonDocument::fromJson("["+r.output.trimmed()+"]");
        if(doc.isArray()&&!doc.array().isEmpty())endpoint=doc.array().first().toString();
        backend.setContext({},endpoint);
        QEventLoop loop;Result listed;
        backend.containers([&](Result r){listed=std::move(r);loop.quit();});loop.exec();
        ok=listed.ok&&listed.output.contains(container.toUtf8());
    }
    if(ok){auto r=invoke({"logs",container});ok=r.ok&&r.output.contains("marker");}
    step({"exec",container,"test","-f","/test-marker"});
    step({"cp",container+":/test-marker",tmp.filePath("copy")});
    if(ok){QFile f(tmp.filePath("copy"));ok=f.open(QIODevice::ReadOnly)&&f.readAll().trimmed()=="dockergui";}
    step({"container","pause",container});step({"container","unpause",container});
    step({"container","restart",container});
    step({"container","inspect","--size",container});
    step({"stats","--no-stream","--format","{{json .}}",container});
    step({"system","df","--format","{{json .}}"});
    step({"container","stop",container});step({"container","rm",container});
    step({"volume","inspect",vol});step({"network","inspect",net});
    step({"image","tag",image,id+":second"});
    step(comp+QStringList{"up","-d"});step({"compose","ls","--all","--format","json"});
    step(comp+QStringList{"stop"});step(comp+QStringList{"start"});step(comp+QStringList{"down"});
    // Always clean up only uniquely named test resources; never prune the engine.
    invoke(comp+QStringList{"down"});
    invoke({"container","rm","-f",container});
    invoke({"image","rm",id+":second"});invoke({"image","rm",image});
    invoke({"network","rm",net});invoke({"volume","rm",vol});
    QTextStream(stdout)<<(ok?"Docker integration tests passed":"Docker integration tests FAILED")<<Qt::endl;
    return ok?0:1;
}
