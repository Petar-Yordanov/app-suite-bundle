#pragma once
#include <algorithm>
#include <cstdint>

namespace media {
// No implicit looping. If current is not in the list, Next starts at its head
// and Previous at its tail.
constexpr int neighbor(int count,int current,int direction){
    if(count<=0||(direction!=1&&direction!=-1))return -1;
    if(current<0||current>=count)return direction==1?0:count-1;
    int result=current+direction;return result<0||result>=count?-1:result;
}
constexpr int movedIndex(int current,int from,int to){
    if(current==from)return to;
    if(from<to&&current>from&&current<=to)return current-1;
    if(from>to&&current>=to&&current<from)return current+1;
    return current;
}
constexpr std::int64_t resumePosition(std::int64_t time,std::int64_t duration){
    if(time<1000||duration<=0||time>=duration-2000)return 0;
    return time;
}
constexpr std::int64_t seekPosition(std::int64_t time,std::int64_t duration){
    return duration>0?std::clamp<std::int64_t>(time,0,duration):0;
}
}
