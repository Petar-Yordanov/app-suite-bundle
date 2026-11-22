#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace rec {
struct Size { int w=0,h=0; bool operator==(const Size&) const = default; };
inline constexpr int MaxDimension=8192;
inline constexpr std::int64_t MaxFrameBytes=128LL*1024*1024;
inline bool validFrame(Size s) {
    return s.w>=2 && s.h>=2 && s.w<=MaxDimension && s.h<=MaxDimension &&
        std::int64_t(s.w)*s.h*4<=MaxFrameBytes;
}
// Preserve aspect ratio. Native resolution rounds down to even dimensions for H.264.
inline Size outputSize(Size input,int heightLimit) {
    if(!validFrame(input))return {};
    double scale=heightLimit>0?std::min(1.0,double(heightLimit)/input.h):1.0;
    Size out{int(input.w*scale)&~1,int(input.h*scale)&~1};
    return validFrame(out)?out:Size{};
}
inline bool validSettings(int fps,int bitrate,int quality,int heightLimit) {
    return fps>=10&&fps<=120&&bitrate>=500&&bitrate<=100000&&quality>=0&&quality<=2&&
        (heightLimit==0||heightLimit==720||heightLimit==1080||heightLimit==1440||heightLimit==2160);
}
enum class State {Idle, Preparing, Recording, Pausing, Paused, Stopping, Finalizing};
inline bool canPause(State state){return state==State::Recording;}
inline bool canResume(State state){return state==State::Paused;}
inline bool canStop(State state) {
    return state==State::Preparing||state==State::Recording||state==State::Pausing||state==State::Paused;
}
inline std::int64_t parseProgressUs(std::string_view value) {
    if(value.empty())return -1;
    std::int64_t n=0;
    for(auto c:value){if(c<'0'||c>'9'||n>864000000000LL)return -1;n=n*10+c-'0';}
    return n;
}
}
