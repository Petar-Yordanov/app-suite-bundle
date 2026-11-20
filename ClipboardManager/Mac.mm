#include "Store.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace clip {
namespace {
constexpr std::uint64_t NodeCharge = 4096;
constexpr std::size_t MaxManifest = 2 * MiB;
constexpr std::size_t MaxPreview = 4096;
struct Node { fs::path source, relative; bool directory; std::uint64_t size; };
struct Hash {
    std::uint64_t a = 14695981039346656037ull, b = 0x9e3779b97f4a7c15ull;
    void add(const char* p, std::size_t n) {
        for (std::size_t i=0;i<n;++i) {
            auto c=static_cast<unsigned char>(p[i]);
            a=(a^c)*1099511628211ull;
            b=(b^c)*0x100000001b3ull; b ^= b>>29;
        }
    }
    void field(const std::string& s) {
        auto n=std::to_string(s.size()) + ":";
        add(n.data(),n.size()); add(s.data(),s.size());
    }
    std::string finish() const {
        std::ostringstream o; o<<std::hex<<std::setfill('0')<<std::setw(16)<<a<<std::setw(16)<<b; return o.str();
    }
};
std::string lower(std::string s) {
    for (auto& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string makeId(std::int64_t timestamp) {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream o; o<<timestamp<<"-"<<std::hex<<rng();return o.str();
}
bool validId(const std::string& s) {
    return !s.empty() && s.size()<64 && s.find('-')!=std::string::npos &&
        std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||c=='-';});
}
bool relativeSafe(const fs::path& p) {
    if (p.empty() || p.is_absolute() || p.has_root_path()) return false;
    for (const auto& c:p) if (c==".." || c==".") return false;
    return true;
}
bool atMost(std::uint64_t& current,std::uint64_t addition,std::uint64_t maximum) {
    if (current>maximum || addition>maximum-current) return false;
    current+=addition; return true;
}
Limits normalize(Limits l) {
    l.entries=std::clamp<std::size_t>(l.entries,1,10000);
    l.bytes=std::clamp<std::uint64_t>(l.bytes,1*MiB,8192*MiB);
    return l;
}
void check(bool b,const char* message) { if(!b) throw std::runtime_error(message); }
void privateDirectory(const fs::path& p) {
    fs::create_directories(p);
    check(!fs::is_symlink(fs::symlink_status(p)),"Unsafe storage directory");
#ifndef _WIN32
    fs::permissions(p,fs::perms::owner_all,fs::perm_options::replace);
#endif
}
std::string manifest(const Entry& e) {
    std::ostringstream out;
    out<<"CLIP2\n"<<e.timestamp<<" "<<e.bytes<<" "<<e.charge<<"\n"
       <<std::quoted(e.kind)<<" "<<std::quoted(e.preview)<<" "<<std::quoted(e.digest)<<"\n"
       <<e.formats.size()<<" "<<e.roots.size()<<"\n";
    for(auto& s:e.formats) out<<std::quoted(s)<<"\n";
    for(auto& s:e.roots) out<<std::quoted(s)<<"\n";
    return out.str();
}
std::uint64_t chargeFor(const fs::path& dir) {
    std::uint64_t total=NodeCharge; std::size_t nodes=0;
    for(const auto& n:fs::recursive_directory_iterator(dir)) {
        check(++nodes<MaxNodes+MaxFormats+20,"Too many stored files");
        auto s=n.symlink_status();
        check(!fs::is_symlink(s) && (fs::is_directory(s)||fs::is_regular_file(s)),"Unsafe stored entry");
        check(atMost(total,NodeCharge,std::numeric_limits<std::uint64_t>::max()),"Size overflow");
        if(fs::is_regular_file(s)) check(atMost(total,fs::file_size(n),std::numeric_limits<std::uint64_t>::max()),"Size overflow");
    } return total;
}
Entry readManifest(const fs::path& dir) {
    auto mp=dir/"manifest";
    check(fs::is_regular_file(fs::symlink_status(mp))&&fs::file_size(mp)<=MaxManifest,"Invalid manifest");
    std::ifstream f(mp,std::ios::binary); std::string magic; std::size_t nf=0,nr=0;
    Entry e; e.id=utf8(dir.filename());
    f>>magic>>e.timestamp>>e.bytes>>e.charge>>std::quoted(e.kind)>>std::quoted(e.preview)>>std::quoted(e.digest)>>nf>>nr;
    check(f.good()&&magic=="CLIP2"&&nf<=MaxFormats&&nr<=MaxNodes&&e.preview.size()<=MaxPreview&&e.digest.size()==32,"Invalid manifest");
    for(std::size_t i=0;i<nf;++i){ std::string s; f>>std::quoted(s); check(f.good()&&!s.empty()&&s.size()<=1024,"Invalid format");e.formats.push_back(s); }
    for(std::size_t i=0;i<nr;++i){ std::string s; f>>std::quoted(s); check(f.good()&&relativeSafe(path(s))&&s.rfind("files/",0)==0,"Invalid path");e.roots.push_back(s); }
    for(std::size_t i=0;i<nf;++i) check(fs::is_regular_file(fs::symlink_status(dir/"mime"/std::to_string(i))),"Missing MIME data");
    for(auto& s:e.roots) check(fs::exists(dir/path(s)),"Missing file snapshot");
    e.charge=chargeFor(dir);
    // The committed directory name is the authoritative recency timestamp.
    e.timestamp=std::stoll(e.id.substr(0,e.id.find('-')));
    return e;
}
void writeBlob(const fs::path& p,const std::string& b) {
    std::ofstream out(p,std::ios::binary|std::ios::trunc);
    out.write(b.data(),static_cast<std::streamsize>(b.size()));out.close();check(out.good(),"Could not write clipboard data");
}
void hashOrCopy(const Node& n,Hash& hash,const fs::path* stage=nullptr) {
    hash.field(utf8(n.relative));hash.field(n.directory?"directory":"file");hash.field(std::to_string(n.size));
    if(n.directory){ if(stage) fs::create_directories(*stage/n.relative);return; }
    check(fs::is_regular_file(fs::symlink_status(n.source)),"Source file changed");
    std::ifstream in(n.source,std::ios::binary);check(in.good(),"Cannot read copied file");
    std::ofstream out;
    if(stage){fs::create_directories((*stage/n.relative).parent_path());out.open(*stage/n.relative,std::ios::binary);check(out.good(),"Cannot store copied file");}
    std::array<char,64*1024> buffer{}; std::uint64_t copied=0;
    while(in) {
        in.read(buffer.data(),buffer.size()); auto got=in.gcount(); if(got<=0) break;
        check(atMost(copied,static_cast<std::uint64_t>(got),n.size),"Source file grew");
        hash.add(buffer.data(),static_cast<std::size_t>(got));
        if(stage){out.write(buffer.data(),got);check(out.good(),"Disk write failed");}
    }
    check(in.eof()&&!in.bad()&&copied==n.size,"Source file changed or could not be read");
    if(stage){out.close();check(out.good(),"Disk write failed");}
}
bool containsFile(const fs::path& p,const std::string& needle) {
    std::ifstream f(p,std::ios::binary); if(!f)return false;
    std::string tail;std::array<char,65536> buf{};
    while(f){f.read(buf.data(),buf.size());auto count=f.gcount();if(count<=0)break;
        std::string block=tail+std::string(buf.data(),static_cast<std::size_t>(count));
        block=lower(std::move(block)); if(block.find(needle)!=std::string::npos)return true;
        auto keep=std::min(block.size(),needle.size()-1);tail=block.substr(block.size()-keep);}
    return false;
}
}
std::string utf8(const fs::path& p) { auto s=p.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()}; }
fs::path path(const std::string& p) {
    std::u8string converted;converted.reserve(p.size());
    for(unsigned char c:p)converted.push_back(static_cast<char8_t>(c));
    return fs::path(converted);
}
bool sensitiveMarker(std::string s) {
    s=lower(std::move(s));
    for(auto* marker:{"clipboard viewer ignore","cf_clipboard_viewer_ignore","excludeclipboardcontentfrommonitorprocessing",
        "canincludeinclipboardhistory","canuploadtocloudclipboard","x-kde-passwordmanagerhint","concealedtype",
        "org.nspasteboard.transienttype","org.nspasteboard.autogeneratedtype","x-clipboard-sensitive",
        "application/x-keepass","keepassxc","application/x-sensitive"})
        if(s.find(marker)!=std::string::npos)return true;
    return false;
}
bool passwordManager(std::string s) {
    s=lower(std::move(s));return s.find("keepass")!=std::string::npos;
}
Store::Store(fs::path root,Limits limits):root_(std::move(root)),limits_(normalize(limits)) {
    privateDirectory(root_);load();check(makeRoom(0,0),"Unable to enforce history limits");
}
void Store::load() {
    for(const auto& d:fs::directory_iterator(root_)) {
        auto id=utf8(d.path().filename());
        if(fs::is_symlink(d.symlink_status())||!d.is_directory())continue;
        if(id.rfind(".stage-",0)==0 && validId(id.substr(7))) {fs::remove_all(d.path());continue;}
        if(!validId(id))continue;
        try {entries_.push_back(readManifest(d.path()));}
        catch(const std::exception&) { // Only remove corrupt directories in our private, dedicated store.
            fs::remove_all(d.path());
        }
    }
    std::sort(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b){
        return a.timestamp!=b.timestamp?a.timestamp>b.timestamp:a.id>b.id;
    });
}
std::uint64_t Store::used() const {
    std::uint64_t n=0;for(auto& e:entries_) n+=e.charge;return n;
}
bool Store::remove(const std::string& id) {
    if(id==lease_)return false;
    auto it=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==id;});
    if(it==entries_.end())return false;
    std::error_code ec;fs::remove_all(root_/id,ec);if(ec)return false;entries_.erase(it);return true;
}
bool Store::clear() {
    std::vector<std::string> ids; for(auto& e:entries_)ids.push_back(e.id);
    bool ok=true;for(auto& id:ids) if(!remove(id))ok=false;return ok;
}
bool Store::makeRoom(std::uint64_t charge,std::size_t count) {
    if(charge>limits_.bytes||count>limits_.entries)return false;
    while(used()>limits_.bytes-charge||entries_.size()>limits_.entries-count) {
        auto it=std::find_if(entries_.rbegin(),entries_.rend(),[&](auto& e){return e.id!=lease_;});
        if(it==entries_.rend())return false;
        std::string id=it->id;if(!remove(id))return false;
    } return true;
}
bool Store::setLimits(Limits l) {
    auto old=limits_;limits_=normalize(l);
    if(!lease_.empty()) {
        auto i=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==lease_;});
        if(i!=entries_.end()&&i->charge>limits_.bytes){limits_=old;return false;}
    }
    if(!makeRoom(0,0)){limits_=old;return false;}return true;
}
Result Store::put(Capture c) {
    fs::path stage;
    try {
        check(!c.formats.empty()||!c.files.empty(),"Empty clipboard");
        check(c.formats.size()<=MaxFormats&&c.files.size()<=MaxNodes,"Too many clipboard formats or files");
        std::sort(c.formats.begin(),c.formats.end(),[](auto& a,auto& b){return a.type<b.type;});
        Entry e;e.timestamp=entries_.empty()?now():std::max(now(),entries_.front().timestamp+1);
        e.id=makeId(e.timestamp);e.preview=c.preview.substr(0,MaxPreview);e.kind=c.kind.substr(0,64);
        Hash prefix;prefix.field("clip2");std::uint64_t payload=0,charge=NodeCharge*4;
        for(auto& f:c.formats) {
            check(!sensitiveMarker(f.type)&&!f.type.empty()&&f.type.size()<=1024,"Sensitive or invalid clipboard format");
            check(atMost(payload,f.bytes.size(),MaxPayload),"Clipboard payload exceeds memory limit");
            check(atMost(charge,f.bytes.size()+NodeCharge,limits_.bytes),"Clipboard exceeds storage limit");
            prefix.field(f.type);prefix.field(f.bytes);e.formats.push_back(f.type);
        }
        e.bytes=payload; std::vector<Node> nodes;
        auto addNode=[&](const fs::path& src,const fs::path& relative){
            check(nodes.size()<MaxNodes&&relativeSafe(relative),"Unsafe or excessive copied file tree");
            auto s=fs::symlink_status(src);check(!fs::is_symlink(s)&&(fs::is_directory(s)||fs::is_regular_file(s)),"Links and special files are not snapshotted");
            bool dir=fs::is_directory(s);auto size=dir?0:fs::file_size(src);
            check(atMost(e.bytes,size,limits_.bytes)&&atMost(charge,NodeCharge,limits_.bytes)&&atMost(charge,size,limits_.bytes),"Copied files exceed storage limit");
            nodes.push_back({src,relative,dir,size});
        };
        for(std::size_t i=0;i<c.files.size();++i) {
            auto src=c.files[i].lexically_normal();
            while(src.has_relative_path()&&src.filename().empty())src=src.parent_path();
            check(src.is_absolute(),"Only local absolute file paths can be snapshotted");
            auto base=src.filename();
            check(!base.empty()&&base!="."&&base!="..","Invalid file path");
            auto rel=fs::path("files")/std::to_string(i)/base;
            auto canonical=fs::weakly_canonical(src), storeRoot=fs::weakly_canonical(root_);
            auto a=storeRoot.lexically_relative(canonical);
            auto b=canonical.lexically_relative(storeRoot);
            check(canonical!=storeRoot&&!relativeSafe(a)&&!relativeSafe(b),"Cannot snapshot clipboard history's own storage tree");
            check(atMost(charge,NodeCharge,limits_.bytes),"Copied files exceed storage limit");
            e.roots.push_back(utf8(rel));addNode(src,rel);
            if(fs::is_directory(fs::symlink_status(src)))for(const auto& d:fs::recursive_directory_iterator(src)){
                auto child=d.path().lexically_relative(src);check(relativeSafe(child),"Unsafe copied path");addNode(d.path(),rel/child);
            }
        }
        std::sort(nodes.begin(),nodes.end(),[](auto& a,auto& b){return utf8(a.relative)<utf8(b.relative);});
        Hash h=prefix;for(auto& n:nodes)hashOrCopy(n,h);e.digest=h.finish();
        auto duplicate=std::find_if(entries_.begin(),entries_.end(),[&](auto& old){return old.digest==e.digest&&old.bytes==e.bytes;});
        if(duplicate!=entries_.end()){
            // Recency update by renaming the committed directory; no payload re-copy.
            if(duplicate->id==lease_)return {true,"Already in history",duplicate->id};
            Entry moved=*duplicate;auto old=moved.id; moved.id=e.id;moved.timestamp=e.timestamp;
            // Timestamp is inferred from the directory id on startup; manifest remains immutable.
            fs::rename(root_/old,root_/moved.id);entries_.erase(duplicate);entries_.insert(entries_.begin(),moved);
            return {true,"Duplicate moved to top",moved.id};
        }
        e.charge=charge; auto m=manifest(e);
        check(m.size()<=MaxManifest&&atMost(charge,m.size()+NodeCharge,limits_.bytes),"Clipboard metadata exceeds storage limit");
        e.charge=charge;m=manifest(e);
        check(charge+32<=limits_.bytes,"Clipboard exceeds storage limit");
        check(makeRoom(charge+32,1),"Storage limit reached (active clipboard snapshot may be protected)");
        stage=root_/(".stage-"+e.id);privateDirectory(stage);
        fs::create_directories(stage/"mime");fs::create_directories(stage/"files");
        for(std::size_t i=0;i<c.formats.size();++i)writeBlob(stage/"mime"/std::to_string(i),c.formats[i].bytes);
        Hash actual=prefix;for(auto& n:nodes)hashOrCopy(n,actual,&stage);
        check(actual.finish()==e.digest,"Source content changed while capturing");
        writeBlob(stage/"manifest",m);e.charge=chargeFor(stage);
        check(e.charge<=limits_.bytes-used(),"Storage limit reached");
        fs::rename(stage,root_/e.id);stage.clear();
        entries_.insert(entries_.begin(),e);return {true,"Captured",e.id};
    }catch(const std::exception& ex){
        if(!stage.empty()){std::error_code ec;fs::remove_all(stage,ec);}
        return {false,ex.what(),{}};
    }
}
std::optional<Restored> Store::restore(const std::string& id) const {
    auto it=std::find_if(entries_.begin(),entries_.end(),[&](auto& e){return e.id==id;});
    if(it==entries_.end())return {};
    try{
        Restored r;std::uint64_t total=0;auto dir=root_/id;
        for(std::size_t i=0;i<it->formats.size();++i){
            auto p=dir/"mime"/std::to_string(i);check(fs::is_regular_file(fs::symlink_status(p)),"Missing stored data");
            auto size=fs::file_size(p);check(atMost(total,size,MaxPayload),"Stored data exceeds memory limit");
            std::string b(static_cast<std::size_t>(size),'\0');std::ifstream f(p,std::ios::binary);
            check(f.is_open(),"Cannot open stored data");
            f.read(b.data(),static_cast<std::streamsize>(size));check(f.good()||f.gcount()==static_cast<std::streamsize>(size),"Read failed");
            r.formats.push_back({it->formats[i],std::move(b)});
        }
        for(auto& s:it->roots){auto p=dir/path(s);check(relativeSafe(path(s))&&fs::exists(p),"Missing snapshot");r.files.push_back(p);}
        return r;
    }catch(...){return {};}
}
std::vector<Entry> Store::search(const std::string& q,std::size_t limit) const {
    std::vector<Entry> out;auto needle=lower(q);
    if(!limit)return out;
    for(auto& e:entries_){
        bool match=needle.empty()||lower(e.preview).find(needle)!=std::string::npos||lower(e.kind).find(needle)!=std::string::npos;
        if(!match)for(auto& root:e.roots)if(lower(root).find(needle)!=std::string::npos){match=true;break;}
        if(!match)for(std::size_t i=0;i<e.formats.size();++i){
            if(lower(e.formats[i]).find(needle)!=std::string::npos){match=true;break;}
            if(e.formats[i].rfind("text/",0)==0&&containsFile(root_/e.id/"mime"/std::to_string(i),needle)){match=true;break;}
        }
        if(match)out.push_back(e);
        if(out.size()>=limit)break;
    }return out;
}
}
