#include "../Core/Geometry.hpp"
#include <iostream>
#include <limits>
int main(){
    int failed=0;
    auto check=[&](shot::PixelRect r,int x,int y,int w,int h){if(r.x!=x||r.y!=y||r.width!=w||r.height!=h){++failed;std::cerr<<"Unexpected crop: "<<r.x<<","<<r.y<<","<<r.width<<","<<r.height<<"\n";}};
    check(shot::mapRegion(10,20,100,60,1000,500,2000,1000),20,40,200,120);
    check(shot::mapRegion(110,80,-100,-60,1000,500,2000,1000),20,40,200,120);
    check(shot::mapRegion(-10,-20,50,60,100,100,150,150),0,0,60,60);
    check(shot::mapRegion(99,99,9,9,100,100,150,150),148,148,2,2);
    check(shot::mapRegion(5,5,0,1,100,100,150,150),0,0,0,0);
    check(shot::mapRegion(5,5,1,1,0,100,150,150),0,0,0,0);
    check(shot::mapRegion(0,0,1,1,1,1,50000,50000),0,0,0,0);
    check(shot::mapRegion(std::numeric_limits<double>::infinity(),5,1,1,100,100,150,150),0,0,0,0);
    check(shot::mapRegion(0,0,100,100,100,100,3840,2160),0,0,3840,2160);
    if(failed)return 1;
    std::cout<<"Geometry tests passed (9 cases).\n";return 0;
}
