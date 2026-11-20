#include "Store.hpp"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace clip;
namespace {
int assertions=0;
void expect(bool value,const char* detail){++assertions;if(!value)throw std::runtime_error(detail);}
struct Temp {
    fs::path root;
    Temp(){
        auto seed=std::chrono::steady_clock::now().time_since_epoch().count();
        root=fs::temp_directory_path()/("clipdeck-tests-"+std::to_string(seed));fs::create_directories(root);
    }
    ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
};
void write(const fs::path& p,const std::string& data){
    fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(data.data(),data.size());f.close();
    expect(f.good(),"test fixture could not be written");
}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
Capture text(std::string data,std::string preview={}){
    if(preview.empty())preview=data.substr(0,30);
    return {{{"text/plain",std::move(data)}},{},std::move(preview),"Text"};
}
void testRoundtrip(){
    Temp t;std::string binary("a\0b\xffz",5);std::string html="<b>Hi</b>";std::string rtf="{\\rtf1 Hi}";
    std::string id;
    {
        Store s(t.root/"history");
        auto result=s.put({{{"text/plain","Hello"},{"text/html",html},{"text/rtf",rtf},{"application/octet-stream",binary}},{},"Hello","Text"});
        expect(result.ok,"rich clipboard capture failed");id=result.id;
        expect(s.entries().size()==1,"entry missing");expect(s.used()<=s.limits().bytes,"quota exceeded");
        auto restored=s.restore(id);expect(restored.has_value(),"restore failed");
        expect(restored->formats.size()==4,"format count changed");
        for(auto& f:restored->formats){
            if(f.type=="application/octet-stream")expect(f.bytes==binary,"binary bytes changed");
            if(f.type=="text/html")expect(f.bytes==html,"HTML changed");
            if(f.type=="text/rtf")expect(f.bytes==rtf,"RTF changed");
        }
        expect(s.search("HELLO").size()==1,"case-insensitive search failed");
        expect(s.search("nope").empty(),"unrelated search returned results");
    }
    Store again(t.root/"history");
    expect(again.entries().size()==1&&again.entries()[0].id==id,"restart did not retain history");
    expect(again.restore(id).has_value(),"restore after reload failed");
}
void testDedupeAndCount(){
    Temp t;std::string latest;
    {
        Store s(t.root/"history",{2,16*MiB});
        auto a=s.put(text("first"));auto b=s.put(text("second"));expect(a.ok&&b.ok,"capture failed");
        auto duplicate=s.put(text("first"));expect(duplicate.ok,"duplicate failed");
        expect(s.entries().size()==2&&s.entries().front().preview=="first","duplicate did not move to top");
        latest=duplicate.id;
    }
    Store s(t.root/"history",{2,16*MiB});
    expect(s.entries()[0].id==latest,"duplicate recency did not survive restart");
    auto c=s.put(text("third"));expect(c.ok&&s.entries().size()==2,"count retention failed");
    expect(s.search("second").empty()&&s.search("first").size()==1,"wrong entry evicted");
    expect(s.setLimits({1,16*MiB})&&s.entries().size()==1,"limit reduction failed");
    expect(s.clear()&&s.entries().empty(),"clear failed");
}
void testFileSnapshot(){
    Temp t;auto source=t.root/"original"/"folder";write(source/"sub"/"note.txt","original content");write(source/"empty","");fs::create_directories(source/"empty-directory");
    Store s(t.root/"history",{10,16*MiB});
    auto result=s.put({{}, {source},"folder","Files"});expect(result.ok,"directory snapshot failed");
    write(source/"sub"/"note.txt","changed");fs::remove_all(source);
    auto restored=s.restore(result.id);expect(restored&&restored->files.size()==1,"file restore failed");
    auto snapshot=restored->files[0];expect(snapshot!=source,"restore pointed at original file");
    expect(read(snapshot/"sub"/"note.txt")=="original content","stored file not independent");
    expect(fs::is_directory(snapshot/"empty-directory"),"empty directory missing");
    expect(read(snapshot/"empty").empty(),"empty file not retained");
    s.protect(result.id);expect(!s.remove(result.id),"active clipboard file was removed");
    expect(!s.clear()&&fs::exists(snapshot),"clear invalidated active file clipboard");
    s.protect({});expect(s.remove(result.id)&&!fs::exists(snapshot),"snapshot removal failed");
}
void testLimits(){
    Temp t;Store s(t.root/"history",{5,MiB});
    auto a=s.put(text(std::string(420000,'a')));auto b=s.put(text(std::string(420000,'b')));
    expect(a.ok&&b.ok,"quota setup failed");
    auto c=s.put(text(std::string(420000,'c')));expect(c.ok,"quota insertion failed");
    expect(s.used()<=s.limits().bytes&&s.entries().size()==2&&!s.restore(a.id),"byte retention did not evict oldest");
    auto rejected=s.put(text(std::string(2*MiB,'z')));
    expect(!rejected.ok&&s.entries().size()==2,"oversized capture deleted valid history");
    auto source=t.root/"large.bin";write(source,std::string(2*MiB,'x'));
    expect(!s.put({{}, {source},"large","Files"}).ok,"oversized file was captured");
    expect(s.used()<=MiB,"quota exceeded");
}
void testPrivacyAndSearch(){
    Temp t;Store s(t.root/"history");
    for(const std::string marker:{"CF_CLIPBOARD_VIEWER_IGNORE","application/x-qt-windows-mime;value=\"Clipboard Viewer Ignore\"",
        "ExcludeClipboardContentFromMonitorProcessing","x-kde-passwordManagerHint","org.nspasteboard.ConcealedType"}){
        expect(sensitiveMarker(marker),"privacy marker not recognized");
        auto r=s.put({{{marker,"secret"},{"text/plain","secret"}},{},"secret","Text"});
        expect(!r.ok&&s.entries().empty(),"secret entered history");
    }
    expect(passwordManager("C:/Program Files/KeePassXC/keepassxc.exe"),"KeePassXC detection failed");
    expect(passwordManager("org.keepassxc.keepassxc"),"KeePass detection failed");
    expect(!passwordManager("notepad.exe"),"normal app excluded");
    auto r=s.put(text(std::string(65530,'x')+"cross-boundary-needle","preview"));
    expect(r.ok&&s.search("CROSS-BOUNDARY-NEEDLE").size()==1,"full-text streaming search failed");
    expect(s.search("cross-boundary-needle",0).empty(),"zero limit failed");
}
void testRecoveryAndSafety(){
    Temp t;auto root=t.root/"history";
    fs::create_directories(root/".stage-123-aabb");
    write(root/".stage-123-aabb"/"partial","incomplete");
    fs::create_directories(root/"123-abcd");
    write(root/"123-abcd"/"manifest","corrupt");
    write(t.root/"outside.txt","outside");
    Store s(root);
    expect(s.entries().empty()&&!fs::exists(root/".stage-123-aabb")&&!fs::exists(root/"123-abcd"),"recovery did not clean incomplete entries");
    expect(read(t.root/"outside.txt")=="outside","unrelated file affected");
    expect(!s.put({{}, {t.root},"self","Files"}).ok,"store recursively snapshotted itself");
#ifndef _WIN32
    fs::create_symlink(t.root/"outside.txt",t.root/"link");
    expect(!s.put({{}, {t.root/"link"},"link","Files"}).ok,"followed symbolic link");
#endif
}
}
int main(){
    try{
        testRoundtrip();testDedupeAndCount();testFileSnapshot();testLimits();testPrivacyAndSearch();testRecoveryAndSafety();
        std::cout<<"PASS: "<<assertions<<" assertions across 6 suites\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
