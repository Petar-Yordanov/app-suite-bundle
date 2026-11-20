#include "Store.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
using namespace clip;
using Clock=std::chrono::steady_clock;
namespace {
double ms(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
struct Temp {
    fs::path root=fs::temp_directory_path()/("clipdeck-bench-"+std::to_string(Clock::now().time_since_epoch().count()));
    Temp(){fs::create_directories(root);}
    ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
};
}
int main(){
    Temp t;auto start=Clock::now();Store s(t.root/"history",{1000,512*MiB});auto open=ms(start);
    start=Clock::now();
    for(int i=0;i<1000;++i){
        auto item=std::to_string(i)+" "+std::string(512,'x');
        auto r=s.put({{{"text/plain",item}},{},item,"Text"});
        if(!r.ok){std::cerr<<r.message<<"\n";return 1;}
    }
    auto insert=ms(start);start=Clock::now();auto found=s.search("needle-not-present");auto search=ms(start);
    start=Clock::now();auto restored=s.restore(s.entries().front().id);auto restore=ms(start);
    auto src=t.root/"snapshot.bin";{std::ofstream f(src,std::ios::binary);std::string block(64*1024,'a');for(int i=0;i<1024;++i)f.write(block.data(),block.size());}
    start=Clock::now();auto r=s.put({{}, {src},"snapshot.bin","Files"});auto snapshot=ms(start);
    if(!r.ok||!restored||!found.empty())return 1;
    start=Clock::now();Store reopened(t.root/"history",{1000,512*MiB});auto reopen=ms(start);
    std::cout<<"Empty store initialization (ms): "<<open<<"\n";
    std::cout<<"1,000 text captures (ms): "<<insert<<"\n";
    std::cout<<"1,000-entry full-text miss (ms): "<<search<<"\n";
    std::cout<<"Restore latest text (ms): "<<restore<<"\n";
    std::cout<<"64 MiB file snapshot (ms): "<<snapshot<<"\n";
    std::cout<<"Reopen full store (ms): "<<reopen<<"\n";
    std::cout<<"Retained entries: "<<reopened.entries().size()<<"\n";
    std::cout<<"Accounted storage bytes: "<<reopened.used()<<"\n";
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};getrusage(RUSAGE_SELF,&usage);
#ifdef __APPLE__
    std::cout<<"Peak RSS bytes: "<<usage.ru_maxrss<<"\n";
#else
    std::cout<<"Peak RSS KiB: "<<usage.ru_maxrss<<"\n";
#endif
#endif
}
