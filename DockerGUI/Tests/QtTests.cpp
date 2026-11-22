#include "../Core/Backend.hpp"
#include "../App/ResourceModel.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QFile>
#include <QElapsedTimer>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--helper")){
        QFile input;input.open(stdin,QIODevice::ReadOnly);
        auto data=input.readAll();
        QTextStream(stdout)<<QString::fromUtf8(data)<<Qt::endl;
        return 0;
    }
    int fails=0;
    auto check=[&](bool ok,const char* text){if(!ok){std::cerr<<text<<"\n";++fails;}};
    auto rows=DockerBackend::jsonRows("{\"ID\":\"a\"}\n{\"ID\":\"b\"}\n");
    check(rows.size()==2,"CLI NDJSON parser");
    check(DockerBackend::jsonRows("[{\"ID\":\"a\"}]").size()==1,"API array parser");
    ResourceModel model;int resets=0,updates=0,inserted=0,removed=0;
    QObject::connect(&model,&QAbstractItemModel::modelReset,&app,[&]{++resets;});
    QObject::connect(&model,&QAbstractItemModel::dataChanged,&app,[&]{++updates;});
    QObject::connect(&model,&QAbstractItemModel::rowsInserted,&app,[&]{++inserted;});
    QObject::connect(&model,&QAbstractItemModel::rowsRemoved,&app,[&]{++removed;});
    model.update({{{"key","a"},{"value",1}},{{"key","b"},{"value",2}}});
    model.update({{{"key","b"},{"value",2}},{{"key","a"},{"value",1}}});
    check(model.rowCount()==2&&model.at(0).value("key")=="a"&&updates==0&&resets==0&&inserted==1,"stable rows");
    model.update({{{"key","a"},{"value",3}},{{"key","c"},{"value",4}}});
    check(model.rowCount()==2&&updates==1&&removed==1&&inserted==2&&resets==0,"incremental updates");
    QVector<QVariantMap> large;
    for(int i=0;i<50000;++i)large.append({{"key",QString::number(i)},{"title",QString("container-%1").arg(i)},{"status","Running"}});
    QElapsedTimer clock;clock.start();model.update(large);
    auto initial=clock.elapsed();clock.restart();model.update(large);
    QTextStream(stdout)<<"50k rows initial: "<<initial<<" ms; unchanged update: "<<clock.elapsed()<<" ms"<<Qt::endl;
    check(model.rowCount()==50000&&resets==0,"large model update");
    QEventLoop loop;Result result;
    DockerBackend::process(&app,app.applicationFilePath(),{"--helper"},[&](Result r){result=std::move(r);loop.quit();},{},
        "password is passed only through stdin",5000);
    loop.exec();
    check(result.ok&&result.output.trimmed()=="password is passed only through stdin","asynchronous subprocess stdin");
    DockerBackend::process(&app,app.applicationFilePath()+".notfound",{},[&](Result r){result=std::move(r);loop.quit();});
    loop.exec();check(!result.ok,"failed process start");
    std::cout<<(fails?"Qt tests FAILED":"Qt tests passed")<<"\n";return fails?1:0;
}
