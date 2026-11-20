#include "../Core/Metadata.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using B=std::vector<std::uint8_t>;
int assertions=0;
void require(bool condition,const char* message){++assertions;if(!condition)throw std::runtime_error(message);}
void append(B& destination,const B& bytes){destination.insert(destination.end(),bytes.begin(),bytes.end());}
void be32(B& b,std::uint32_t v){for(int s=24;s>=0;s-=8)b.push_back(std::uint8_t(v>>s));}
B littleTiff(){
    return {'I','I',42,0,8,0,0,0,2,0,
        0x12,0x01,3,0,1,0,0,0,6,0,0,0,
        0x0f,0x01,2,0,5,0,0,0,38,0,0,0,
        0,0,0,0,'T','e','s','t',0};
}
int main(){
    try{
        const auto tiff=littleTiff();auto m=imageview::readExif(tiff);
        require(m.orientation==6,"little-endian orientation");require(m.fields.size()==2,"EXIF field count");
        require(std::find(m.fields.begin(),m.fields.end(),std::pair<std::string,std::string>{"Camera make","Test"})!=m.fields.end(),"EXIF camera name");
        B big={'M','M',0,42,0,0,0,8,0,1,1,18,0,3,0,0,0,1,0,8,0,0,0,0,0,0};require(imageview::readExif(big).orientation==8,"big-endian orientation");
        B jpeg={0xff,0xd8,0xff,0xe1,0,std::uint8_t(tiff.size()+8),'E','x','i','f',0,0};append(jpeg,tiff);append(jpeg,{0xff,0xd9});require(imageview::readMetadata(jpeg).orientation==6,"JPEG APP1 orientation");
        B png={137,'P','N','G',13,10,26,10};be32(png,tiff.size());append(png,{'e','X','I','f'});append(png,tiff);be32(png,0);require(imageview::readMetadata(png).orientation==6,"PNG eXIf orientation");
        B webp={'R','I','F','F',0,0,0,0,'W','E','B','P','E','X','I','F',std::uint8_t(tiff.size()),0,0,0};append(webp,tiff);require(imageview::readMetadata(webp).orientation==6,"WebP EXIF orientation");
        B psd(26);psd[0]='8';psd[1]='B';psd[2]='P';psd[3]='S';be32(psd,0);be32(psd,16);append(psd,{'8','B','I','M',4,15,0,0,0,0,0,3,1,2,3,0});require(imageview::readMetadata(psd).icc==B({1,2,3}),"PSD ICC resource");
        for(std::size_t n=0;n<tiff.size();++n){auto partial=imageview::readExif(std::span<const std::uint8_t>(tiff).first(n));require(partial.orientation>=1&&partial.orientation<=8,"truncated EXIF");}
        auto invalid=tiff;invalid[4]=0xff;invalid[5]=0xff;invalid[6]=0xff;invalid[7]=0xff;require(imageview::readExif(invalid).fields.empty(),"invalid offset");
        std::mt19937 rng(2026);for(int i=0;i<20000;++i){B bytes(std::size_t(rng()%256));for(auto& b:bytes)b=std::uint8_t(rng());if(bytes.size()>8&&(i%2)==0){bytes[0]='I';bytes[1]='I';bytes[2]=42;bytes[3]=0;}auto data=imageview::readMetadata(bytes);require(data.fields.size()<=64,"fuzz metadata bound");}
        std::cout<<"PASS: "<<assertions<<" metadata assertions (JPEG, PNG, WebP, TIFF, PSD, truncation and randomized input)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
