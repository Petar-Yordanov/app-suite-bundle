#include "../App/Models.hpp"
#include <QCoreApplication>
#include <QPersistentModelIndex>
#include <iostream>
#include <chrono>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);ProcessModel source;ProcessProxy proxy;proxy.setSourceModel(&source);
    sm::Process a,b,c;a.pid=100;a.identity=10;a.name="alpha";a.cpu=10;b.pid=200;b.identity=20;b.name="beta";b.cpu=20;
    source.apply({a,b});QPersistentModelIndex index=source.index(0);int resets=0;
    QObject::connect(&source,&QAbstractItemModel::modelReset,[&]{++resets;});
    a.cpu=60;c.pid=300;c.identity=30;c.name="gamma";source.apply({a,c});
    if(resets||!index.isValid()||source.rowCount()!=2||index.data(ProcessModel::Cpu).toDouble()!=60)return 1;
    proxy.query("gamma");if(proxy.rowCount()!=1||proxy.index(0,0).data(ProcessModel::Pid).toULongLong()!=300)return 2;
    proxy.query("");proxy.order("cpu",true);if(proxy.index(0,0).data(ProcessModel::Pid).toULongLong()!=100)return 3;
    a.identity=11;source.apply({a,c});if(index.isValid()||!source.lookup(100,"10").isEmpty())return 4;
    std::vector<sm::Process> large;large.reserve(10000);
    for(int i=0;i<10000;++i){sm::Process p;p.pid=i+1000;p.identity=i+1;p.name="process-"+std::to_string(i);p.cpu=i%100;large.push_back(std::move(p));}
    source.apply(large);QPersistentModelIndex kept=source.index(5000);
    auto t=std::chrono::steady_clock::now();for(auto& p:large)p.cpu=100-*p.cpu;source.apply(large);
    auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    if(resets||!kept.isValid()||proxy.rowCount()!=10000)return 5;
    std::cout<<"10000-row incremental sorted update: "<<ms<<" ms\n";
    std::cout<<"Incremental rows, filtering, sorting, and PID reuse passed.\n";
}
