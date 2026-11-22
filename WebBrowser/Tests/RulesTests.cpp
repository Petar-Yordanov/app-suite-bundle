#include "../Core/Rules.hpp"
#include <iostream>
#include <cstdlib>
void check(bool b,const char* m){if(!b){std::cerr<<"FAIL: "<<m<<"\n";std::exit(1);}}
int main(){
    auto r=web::Rules::make(true,"0.0.0.0 extra.example # comment\nEXTRA.example\ninvalid\n","allow.example");
    check(r.blocks("ad.doubleclick.net","example.org"),"third party filter");
    check(!r.blocks("evil-doubleclick.net","example.org"),"hostname boundary");
    check(!r.blocks("doubleclick.net","doubleclick.net"),"first party");
    check(!r.blocks("doubleclick.net","allow.example"),"allowlist");
    check(!r.blocks("doubleclick.net","sub.allow.example"),"subdomain allowlist");
    check(!r.blocks("doubleclick.net","example.org",true),"top-level navigation");
    check(r.blocks("extra.example","example.org"),"custom rule");
    check(!web::Rules::make(false,"","").blocks("doubleclick.net","example.org"),"switch");
    check(web::parseHosts("0.0.0.0 FOO.Example\nfoo.example\n# a.example\n127.0.0.1 localhost").size()==1,"hosts parsing");
    auto abp=web::Rules::make(true,"! ignore comment.example\n||tracker.example^$third-party\n||cdn.example/ads/\n||site.example^$script\n@@||exception.example^\nexample.org##.banner\n","trusted.example");
    check(abp.blocks("sub.tracker.example","other.example"),"ABP domain");
    check(abp.blocks("cdn.example","other.example",false,"/ads/image.png"),"ABP path");
    check(!abp.blocks("cdn.example","other.example",false,"/images/ad.png"),"nonmatching path");
    check(!abp.blocks("cdn.example","trusted.example",false,"/ads/image.png"),"path allowlist");
    check(!abp.blocks("site.example","other.example")&&!abp.blocks("comment.example","other.example")&&!abp.blocks("exception.example","other.example"),"unsupported filters skipped");
    check(web::safeFilename("../../evil.exe")=="evil.exe","path traversal");
    check(web::safeFilename("CON.txt")=="_CON.txt","reserved names");
    check(web::safeFilename("..")=="download","empty filename");
    check(web::safeFilename("file?.png")=="file_.png","characters");
    std::cout<<"Rules tests PASS\n";
}
