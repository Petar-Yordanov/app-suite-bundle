#include "../Core/Rules.hpp"
#include <chrono>
#include <iostream>
int main(){
    std::string hosts;
    for(int i=0;i<20000;++i)hosts+="ads"+std::to_string(i)+".example\n";
    auto start=std::chrono::steady_clock::now();auto rules=web::Rules::make(true,hosts,"");
    auto compiled=std::chrono::steady_clock::now();int blocked=0;constexpr int n=200000;
    for(int i=0;i<n;++i)blocked+=rules.blocks(i%2?"cdn.ads19999.example":"content.example.org","first.example");
    auto end=std::chrono::steady_clock::now();
    std::cout<<"rules="<<rules.blocked.size()<<" compile_ms="<<std::chrono::duration<double,std::milli>(compiled-start).count()<<" checks="<<n<<" checks_ms="<<std::chrono::duration<double,std::milli>(end-compiled).count()<<" blocked="<<blocked<<"\n";
    return blocked==n/2?0:1;
}
