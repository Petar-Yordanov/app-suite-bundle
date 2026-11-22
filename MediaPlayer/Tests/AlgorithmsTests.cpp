#include "../Core/Algorithms.hpp"
#include <iostream>

int main(){
    int checks=0,failed=0;
    auto require=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::cerr<<"FAIL: "<<name<<"\n";}};
    require(media::neighbor(0,-1,1)==-1,"empty list");
    require(media::neighbor(3,-1,1)==0,"start list");
    require(media::neighbor(3,-1,-1)==2,"previous from outside");
    require(media::neighbor(3,0,-1)==-1,"no wrap backward");
    require(media::neighbor(3,2,1)==-1,"no wrap forward");
    require(media::neighbor(3,1,1)==2,"next");
    require(media::neighbor(3,1,-1)==0,"previous");
    require(media::neighbor(3,1,2)==-1,"invalid direction");
    require(media::movedIndex(2,2,0)==0,"move current");
    require(media::movedIndex(2,0,3)==1,"shift down");
    require(media::movedIndex(1,3,0)==2,"shift up");
    require(media::movedIndex(3,0,2)==3,"unaffected");
    require(media::resumePosition(0,12000)==0,"beginning");
    require(media::resumePosition(500,12000)==0,"near beginning");
    require(media::resumePosition(4000,12000)==4000,"remember");
    require(media::resumePosition(10000,12000)==0,"near end");
    require(media::resumePosition(15000,12000)==0,"past end");
    require(media::resumePosition(4000,0)==0,"unknown length");
    require(media::seekPosition(-100,12000)==0,"negative seek");
    require(media::seekPosition(20000,12000)==12000,"seek past end");
    require(media::seekPosition(3000,12000)==3000,"seek normal");
    require(media::seekPosition(3000,0)==0,"seek unknown duration");
    std::cout<<checks-failed<<"/"<<checks<<" core tests passed\n";return failed?1:0;
}
