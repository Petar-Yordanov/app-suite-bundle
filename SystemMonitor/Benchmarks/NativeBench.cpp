#include "../Core/Monitor.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>
int main(int argc,char** argv){
    int iterations=20,interval=100;
    if(argc>1)iterations=std::clamp(std::atoi(argv[1]),2,1000);
    if(argc>2)interval=std::clamp(std::atoi(argv[2]),50,10000);
    sm::Sampler sample;std::vector<double> times;size_t count=0;
    for(int i=0;i<iterations;++i){
        auto start=std::chrono::steady_clock::now();auto snapshot=sample.sample(i%5==0);
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        count=snapshot.processes.size();std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
    double mean=0;for(auto t:times)mean+=t;mean/=times.size();std::sort(times.begin(),times.end());
    std::cout<<"{\"samples\":"<<iterations<<",\"interval_ms\":"<<interval<<",\"processes\":"<<count<<",\"mean_ms\":"<<mean<<",\"p95_ms\":"<<times[(times.size()*95+99)/100-1]<<",\"max_ms\":"<<times.back()<<"}\n";
}
