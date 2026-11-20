#include "Metadata.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <limits>

namespace imageview {
namespace {
using Bytes=std::span<const std::uint8_t>;
constexpr std::size_t MaxMetadata=4*1024*1024;
bool has(Bytes b,std::size_t p,std::size_t n){return p<=b.size()&&n<=b.size()-p;}
bool magic(Bytes b,std::size_t p,const char* s,std::size_t n){return has(b,p,n)&&std::memcmp(b.data()+p,s,n)==0;}
std::uint32_t be(Bytes b,std::size_t p,int n){std::uint32_t v=0;if(!has(b,p,n))return 0;for(int i=0;i<n;++i)v=(v<<8)|b[p+i];return v;}
std::uint32_t le(Bytes b,std::size_t p,int n){std::uint32_t v=0;if(!has(b,p,n))return 0;for(int i=n-1;i>=0;--i)v=(v<<8)|b[p+i];return v;}
void add(Metadata& m,std::string key,std::string value){
    if(value.empty()||m.fields.size()>=64)return;
    if(std::none_of(m.fields.begin(),m.fields.end(),[&](auto& f){return f.first==key;}))m.fields.emplace_back(std::move(key),std::move(value));
}
void merge(Metadata& a,Metadata b){
    if(b.orientation!=1)a.orientation=b.orientation;
    for(auto& [k,v]:b.fields)add(a,std::move(k),std::move(v));
    if(a.icc.empty())a.icc=std::move(b.icc);
}
class Tiff {
    Bytes b; bool little; Metadata result; std::array<std::uint32_t,8> visited{};std::size_t seen=0;
    std::uint32_t number(std::size_t p,int n)const{return little?le(b,p,n):be(b,p,n);}
    std::string value(std::uint16_t type,std::uint32_t count,std::size_t p)const{
        if(!count)return {};
        if(type==2){std::string s;for(std::size_t i=0;i<std::min<std::uint32_t>(count,512)&&b[p+i];++i){auto c=b[p+i];if(c>=32&&c!=127)s+=char(c);}return s;}
        if(type==3)return std::to_string(number(p,2));
        if(type==4)return std::to_string(number(p,4));
        if(type==5||type==10){
            double a=type==10?double(static_cast<std::int32_t>(number(p,4))):double(number(p,4));
            double d=type==10?double(static_cast<std::int32_t>(number(p+4,4))):double(number(p+4,4));
            if(!d)return {};
            std::ostringstream out;out<<std::setprecision(6)<<a/d;return out.str();
        }
        return {};
    }
    void directory(std::uint32_t offset){
        if(seen==visited.size()||!has(b,offset,2)||std::find(visited.begin(),visited.begin()+seen,offset)!=visited.begin()+seen)return;
        visited[seen++]=offset;auto n=number(offset,2);if(n>2048||!has(b,std::size_t(offset)+2,std::size_t(n)*12))return;
        std::uint32_t exif=0;
        for(std::size_t i=0;i<n;++i){
            std::size_t e=std::size_t(offset)+2+i*12;auto tag=number(e,2),type=number(e+2,2),count=number(e+4,4);
            int unit=type==1||type==2||type==7?1:type==3?2:type==4||type==9?4:type==5||type==10?8:0;
            if(!unit||!count||count>MaxMetadata/std::size_t(unit))continue;
            std::size_t len=std::size_t(count)*unit,p=len<=4?e+8:number(e+8,4);if(!has(b,p,len))continue;
            if(tag==0x8769&&type==4){exif=number(p,4);continue;}
            if(tag==0x0112&&type==3){auto o=number(p,2);if(o>=1&&o<=8)result.orientation=int(o);add(result,"EXIF orientation",std::to_string(o));continue;}
            if(tag==0x8773&&len<=MaxMetadata){result.icc.assign(b.begin()+p,b.begin()+p+len);continue;}
            const char* name=nullptr;
            switch(tag){case 0x010f:name="Camera make";break;case 0x0110:name="Camera model";break;case 0x0131:name="Software";break;case 0x0132:name="Modified date";break;case 0x013b:name="Artist";break;case 0x010e:name="Description";break;case 0x8298:name="Copyright";break;case 0x829a:name="Exposure (s)";break;case 0x829d:name="F-number";break;case 0x8827:name="ISO";break;case 0x9003:name="Date taken";break;case 0x9004:name="Date digitized";break;case 0x9204:name="Exposure bias (EV)";break;case 0x9209:name="Flash";break;case 0x920a:name="Focal length (mm)";break;case 0xa434:name="Lens model";break;case 0xa405:name="35mm focal length";break;default:break;}
            if(name)add(result,name,value(type,count,p));
        }
        if(exif)directory(exif);
    }
public:
    explicit Tiff(Bytes data):b(data),little(magic(b,0,"II",2)){}
    Metadata parse(){if(!has(b,0,8)||(!little&&!magic(b,0,"MM",2))||number(2,2)!=42)return {};directory(number(4,4));return std::move(result);}
};
}
Metadata readExif(Bytes b){if(magic(b,0,"Exif\0\0",6))b=b.subspan(6);return Tiff(b).parse();}
Metadata readMetadata(Bytes b){
    Metadata m;
    if(magic(b,0,"II",2)||magic(b,0,"MM",2))return readExif(b);
    if(has(b,0,2)&&b[0]==0xff&&b[1]==0xd8){
        for(std::size_t p=2;has(b,p,4)&&p<MaxMetadata;){
            if(b[p++]!=0xff)break;
            while(has(b,p,1)&&b[p]==0xff)++p;
            if(!has(b,p,1))break;
            auto marker=b[p++];if(marker==0xda||marker==0xd9)break;
            if(marker==0x01||(marker>=0xd0&&marker<=0xd7))continue;
            if(!has(b,p,2))break;
            auto n=be(b,p,2);if(n<2||!has(b,p,n))break;
            if(marker==0xe1&&magic(b,p+2,"Exif\0\0",6))merge(m,readExif(b.subspan(p+2,n-2)));
            p+=n;
        }return m;
    }
    if(magic(b,0,"\x89PNG\r\n\x1a\n",8)){
        for(std::size_t p=8,records=0;has(b,p,12)&&records++<4096;){auto len=be(b,p,4);if(!has(b,p+8,std::size_t(len)+4))break;
            if(magic(b,p+4,"eXIf",4)&&len<=MaxMetadata)merge(m,readExif(b.subspan(p+8,len)));
            if(magic(b,p+4,"IEND",4))break;
            p+=std::size_t(len)+12;}
        return m;
    }
    if(magic(b,0,"RIFF",4)&&magic(b,8,"WEBP",4)){
        for(std::size_t p=12,records=0;has(b,p,8)&&records++<4096;){auto len=le(b,p+4,4);if(!has(b,p+8,len))break;
            if(magic(b,p,"EXIF",4)&&len<=MaxMetadata)merge(m,readExif(b.subspan(p+8,len)));
            if(magic(b,p,"ICCP",4)&&len<=MaxMetadata)m.icc.assign(b.begin()+p+8,b.begin()+p+8+len);
            p+=8+std::size_t(len)+(len&1);}
        return m;
    }
    if(magic(b,0,"8BPS",4)&&has(b,26,4)){
        auto p=std::size_t(30)+be(b,26,4);if(!has(b,p,4))return m;auto n=be(b,p,4);p+=4;if(!has(b,p,n))return m;auto end=p+n;
        std::size_t records=0;while(p<end&&end-p>=12&&magic(b,p,"8BIM",4)&&records++<4096){
            auto id=be(b,p+4,2);p+=6;auto name=std::size_t(b[p])+1;p+=name+(name&1);if(p>end||end-p<4)break;
            auto len=be(b,p,4);p+=4;if(p>end||len>end-p)break;
            if(id==1039&&len<=MaxMetadata)m.icc.assign(b.begin()+p,b.begin()+p+len);
            if((id==1058||id==1059)&&len<=MaxMetadata)merge(m,readExif(b.subspan(p,len)));
            p+=std::size_t(len)+(len&1);
        }
    }
    return m;
}
}
