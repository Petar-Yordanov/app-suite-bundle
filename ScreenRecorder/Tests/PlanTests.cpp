#include "../Core/Geometry.hpp"
#include "../Core/RecordingPlan.hpp"
#include <iostream>
#include <stdexcept>

using namespace rec;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
int main(){
    try{
        check(outputSize({3840,2160},1080)==Size{1920,1080},"4K downscale");
        check(outputSize({1200,800},1080)==Size{1200,800},"no upscale");
        check(outputSize({1279,719},0)==Size{1278,718},"even H.264 size");
        check(outputSize({5120,2880},1440)==Size{2560,1440},"Retina scale");
        check(!validFrame({8192,8192}),"frame memory bound");
        check(!validFrame({-1,1080}),"negative dimensions");
        check(validSettings(60,20000,1,2160),"valid settings");
        check(!validSettings(240,10000,1,720),"fps bound");
        check(!validSettings(60,1000000,1,720),"bitrate bound");
        check(!validSettings(30,10000,4,720),"quality bound");
        check(!validSettings(30,10000,1,999),"resolution bound");
        auto r=mapRegion(10,20,200,100,1920,1080,3840,2160);
        check(r.x==20&&r.y==40&&r.width==400&&r.height==200,"HiDPI selection");
        r=mapRegion(-5,-20,100,80,1000,500,2000,1000);
        check(r.x==0&&r.y==0&&r.width==190&&r.height==120,"clipping");
        check(!canPause(State::Idle)&&canPause(State::Recording),"pause transition");
        check(canResume(State::Paused)&&!canResume(State::Pausing),"resume transition");
        check(canStop(State::Preparing)&&!canStop(State::Finalizing),"stop transition");
        check(parseProgressUs("1234567")==1234567,"progress parsing");
        check(parseProgressUs("N/A")==-1&&parseProgressUs("-1")==-1,"invalid progress");
        check(parseProgressUs("999999999999999999999999999999999")==-1,"overflow");
        std::cout<<"PASS: geometry, frame limits, settings, states and progress\n";
    }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
