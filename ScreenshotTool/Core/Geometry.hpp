#pragma once
#include <algorithm>
#include <cmath>

namespace shot {
struct PixelRect {int x=0,y=0,width=0,height=0;};
inline constexpr long long MaxPixels=128ll*1024*1024;
inline bool validPixels(int w,int h){return w>0&&h>0&&static_cast<long long>(w)*h<=MaxPixels;}
inline PixelRect mapRegion(double x,double y,double w,double h,double logicalW,double logicalH,int pixelW,int pixelH){
    if(!validPixels(pixelW,pixelH)||logicalW<=0||logicalH<=0||!std::isfinite(logicalW)||!std::isfinite(logicalH)||
       !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(x+w)||!std::isfinite(y+h))return {};
    double l=std::clamp(std::min(x,x+w),0.0,logicalW),r=std::clamp(std::max(x,x+w),0.0,logicalW);
    double t=std::clamp(std::min(y,y+h),0.0,logicalH),b=std::clamp(std::max(y,y+h),0.0,logicalH);
    if(l>=r||t>=b)return {};
    int left=int(std::floor(l/logicalW*pixelW)),right=int(std::ceil(r/logicalW*pixelW));
    int top=int(std::floor(t/logicalH*pixelH)),bottom=int(std::ceil(b/logicalH*pixelH));
    return {left,top,right-left,bottom-top};
}
}
