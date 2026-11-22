#include "../Core/Monitor.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sched.h>
#include <unistd.h>

namespace sm {
namespace fs=std::filesystem;
namespace {
std::string read(const fs::path& p,size_t limit=65536){
    std::ifstream in(p,std::ios::binary);if(!in)return {};
    std::string out;char buffer[4096];while(out.size()<limit&&in){in.read(buffer,std::min(sizeof(buffer),limit-out.size()));out.append(buffer,size_t(in.gcount()));}return out;
}
std::string trimmed(std::string s){while(!s.empty()&&(s.back()=='\n'||s.back()=='\r'||s.back()==' '))s.pop_back();return s;}
Number number(const fs::path& p){auto s=read(p,128);try{return std::stod(s);}catch(...){return {};}}
U integer(const std::string& s){try{return std::stoull(s);}catch(...){return 0;}}
std::vector<fs::directory_entry> entries(const fs::path& p){
    std::vector<fs::directory_entry> out;std::error_code ec;
    for(fs::directory_iterator i(p,fs::directory_options::skip_permission_denied,ec),e;i!=e&&!ec;i.increment(ec))out.push_back(*i);
    return out;
}
std::string link(const fs::path& p){std::error_code ec;auto v=fs::read_symlink(p,ec);return ec?std::string{}:v.string();}
std::string username(uid_t uid){
    static thread_local std::unordered_map<uid_t,std::string> cache;
    if(auto i=cache.find(uid);i!=cache.end())return i->second;
    char buf[16384];passwd pass{},*result=nullptr;std::string name=std::to_string(uid);
    if(!getpwuid_r(uid,&pass,buf,sizeof(buf),&result)&&result)name=pass.pw_name;
    if(cache.size()>4096)cache.clear();
    return cache.emplace(uid,name).first->second;
}
struct Stat {std::string name;std::vector<std::string> v;};
std::optional<Stat> statFor(U pid){
    auto s=read("/proc/"+std::to_string(pid)+"/stat",16384);auto a=s.find('('),b=s.rfind(')');
    if(a==std::string::npos||b==std::string::npos||b<=a)return {};
    Stat st;st.name=s.substr(a+1,b-a-1);std::istringstream in(s.substr(b+1));
    for(std::string w;in>>w;)st.v.push_back(w);
    if(st.v.size()<22)return {};
    return st;
}
void metric(Raw& r,std::string name,Number value,std::string unit,std::string detail={}){
    r.metrics.push_back({std::move(name),value,std::move(unit),std::move(detail)});
}
void counter(Raw& r,std::string name,U value,std::string unit,bool valid=true){
    metric(r,name,{},unit);r.counters[name]={value,valid};
}
Result err(){return {false,std::strerror(errno),errno==EACCES||errno==EPERM};}
}
U ownPid(){return U(getpid());}
unsigned cpuCount(){static auto n=std::max(1l,sysconf(_SC_NPROCESSORS_ONLN));return unsigned(n);}
std::optional<U> processIdentity(U pid){if(auto s=statFor(pid))return integer(s->v[19]);return {};}
bool protectPriority(){return setpriority(PRIO_PROCESS,0,-5)==0;}
Raw collectNative(bool details){
    Raw r;std::ifstream cpu("/proc/stat");std::string line;
    while(std::getline(cpu,line)){
        std::istringstream in(line);std::string n;in>>n;if(!n.starts_with("cpu"))break;
        U user=0,nice=0,system=0,idle=0,wait=0,irq=0,soft=0,steal=0;
        if(!(in>>user>>nice>>system>>idle>>wait>>irq>>soft>>steal))continue;
        auto label=n=="cpu"?"CPU total":"CPU core "+n.substr(3);
        U busy=user+nice+system+irq+soft+steal;
        metric(r,label,{},"%");r.counters[label+"/busy"]={busy,true};r.counters[label+"/total"]={busy+idle+wait,true};
    }
    std::unordered_map<std::string,U> mem;std::ifstream memory("/proc/meminfo");
    while(std::getline(memory,line)){std::istringstream in(line);std::string key;U n;if(in>>key>>n)mem[key]=n*1024;}
    bool haveAvail=mem.contains("MemAvailable:"),haveSwap=mem.contains("SwapTotal:")&&mem.contains("SwapFree:");
    auto total=mem["MemTotal:"],avail=mem["MemAvailable:"],swap=mem["SwapTotal:"],freeSwap=mem["SwapFree:"];
    metric(r,"Physical memory used",haveAvail&&total&&avail<=total?Number(total-avail):Number{},"B");
    metric(r,"Physical memory total",total?Number(total):Number{},"B");
    metric(r,"Swap used",haveSwap&&swap>=freeSwap?Number(swap-freeSwap):Number{},"B");
    metric(r,"Swap total",haveSwap?Number(swap):Number{},"B");
    metric(r,"Committed virtual memory",mem.contains("Committed_AS:")?Number(mem["Committed_AS:"]):Number{},"B","Linux committed address space");
    metric(r,"Commit limit",mem.contains("CommitLimit:")?Number(mem["CommitLimit:"]):Number{},"B");
    U diskRead=0,diskWrite=0;bool diskValid=false;std::ifstream disks("/proc/diskstats");
    while(std::getline(disks,line)){
        std::istringstream in(line);U major,minor,reads,merged,sectors,ms,writes,wmerged,wsectors;std::string name;
        if(!(in>>major>>minor>>name>>reads>>merged>>sectors>>ms>>writes>>wmerged>>wsectors))continue;
        std::error_code ec;if(!fs::exists("/sys/block/"+name,ec)||name.starts_with("loop")||name.starts_with("ram")||name.starts_with("dm-")||name.starts_with("md"))continue;
        diskRead+=sectors*512;diskWrite+=wsectors*512;diskValid=true;
    }
    counter(r,"Disk reads",diskRead,"B/s",diskValid);counter(r,"Disk writes",diskWrite,"B/s",diskValid);
    U rx=0,tx=0;bool netValid=false;std::ifstream net("/proc/net/dev");
    while(std::getline(net,line)){
        auto p=line.find(':');if(p==std::string::npos)continue;auto name=trimmed(line.substr(0,p));
        if(name.find_first_not_of(" \t")!=std::string::npos&&name.substr(name.find_first_not_of(" \t"))=="lo")continue;
        std::istringstream in(line.substr(p+1));U receive=0,send=0,tmp=0;if(!(in>>receive))continue;
        for(int i=0;i<7;++i)in>>tmp;
        if(!(in>>send))continue;
        rx+=receive;tx+=send;netValid=true;
    }
    counter(r,"Network download",rx,"B/s",netValid);counter(r,"Network upload",tx,"B/s",netValid);
    std::set<std::string> deviceIds;std::ifstream mounts("/proc/self/mountinfo");
    while(std::getline(mounts,line)){
        std::istringstream in(line);std::string id,parent,dev,root,mount;
        if(!(in>>id>>parent>>dev>>root>>mount))continue;
        // Never query remote filesystems, pseudo devices or arbitrary mount helpers.
        if(!dev.starts_with("0:")&&deviceIds.insert(dev).second){
            for(size_t i=0;i+3<mount.size();++i)if(mount[i]=='\\'&&mount[i+1]>='0'&&mount[i+1]<='7'&&mount[i+2]>='0'&&mount[i+2]<='7'&&mount[i+3]>='0'&&mount[i+3]<='7')
                mount.replace(i,4,1,char((mount[i+1]-'0')*64+(mount[i+2]-'0')*8+mount[i+3]-'0'));
            struct statvfs st{};if(!statvfs(mount.c_str(),&st)){
                metric(r,"Storage "+mount+" used",double(st.f_blocks-st.f_bfree)*st.f_frsize,"B");
                metric(r,"Storage "+mount+" total",double(st.f_blocks)*st.f_frsize,"B");
            }
        }
    }
    if(deviceIds.empty()){struct statvfs st{};if(!statvfs("/",&st)){metric(r,"Storage / used",double(st.f_blocks-st.f_bfree)*st.f_frsize,"B");metric(r,"Storage / total",double(st.f_blocks)*st.f_frsize,"B");}}
    bool gpu=false,temp=false;
    for(auto& e:entries("/sys/class/drm")){
        auto n=e.path().filename().string();if(!n.starts_with("card")||n.find('-')!=std::string::npos)continue;
        auto p=e.path()/"device";auto busy=number(p/"gpu_busy_percent"),used=number(p/"mem_info_vram_used"),capacity=number(p/"mem_info_vram_total");
        if(busy||used||capacity){gpu=true;metric(r,"GPU "+n+" utilization",busy,"%");metric(r,"GPU "+n+" VRAM used",used,"B");metric(r,"GPU "+n+" VRAM total",capacity,"B");}
    }
    for(auto& e:entries("/sys/class/hwmon")){
        auto name=trimmed(read(e.path()/"name"));
        for(auto& sensor:entries(e.path())){
            auto n=sensor.path().filename().string();
            if(!n.starts_with("temp")||!n.ends_with("_input"))continue;
            if(auto v=number(sensor.path())){temp=true;auto label=trimmed(read(e.path()/(n.substr(0,n.size()-6)+"_label")));if(label.empty())label=n;metric(r,"Temperature "+name+" "+label+" ("+e.path().filename().string()+")",*v/1000,"°C");}
        }
    }
    if(!gpu){metric(r,"GPU utilization",{},"%","Driver does not expose DRM utilization");metric(r,"VRAM used",{},"B");}
    if(!temp)metric(r,"Hardware temperatures",{},"°C","No readable hwmon sensors");
    double up=0;std::ifstream("/proc/uptime")>>up;
    const auto hz=std::max(1l,sysconf(_SC_CLK_TCK)),page=std::max(1l,sysconf(_SC_PAGESIZE));
    for(auto& e:entries("/proc")){
        auto name=e.path().filename().string();if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
        U pid=integer(name);auto s=statFor(pid);if(!s)continue;auto& v=s->v;
        RawProcess p;p.p.pid=pid;p.p.name=s->name;p.p.parent=integer(v[1]);p.p.identity=integer(v[19]);p.p.state=v[0];
        p.cpu={U((static_cast<long double>(integer(v[11]))+integer(v[12]))*1e9L/hz),true};
        p.p.threads=integer(v[17]);p.p.virtualMemory=integer(v[20]);p.p.memory=double(integer(v[21]))*page;p.p.uptime=std::max(0.,up-double(p.p.identity)/hz);
        std::ifstream io(e.path()/"io");while(std::getline(io,line)){
            std::istringstream in(line);std::string key;U value;if(in>>key>>value){if(key=="read_bytes:")p.read={value,true};if(key=="write_bytes:")p.write={value,true};}
        }
        if(details){struct stat status{};if(!::stat(e.path().c_str(),&status))p.p.user=username(status.st_uid);p.p.path=link(e.path()/"exe");p.p.command=read(e.path()/"cmdline",8192);std::replace(p.p.command.begin(),p.p.command.end(),'\0',' ');p.p.command=trimmed(p.p.command);}
        r.processes.push_back(std::move(p));
    }
    r.warning="Per-process network usage is unavailable without traffic attribution (eBPF/kernel privileges).";
    return r;
}
Result operateNative(const Request& r){
    auto pid=pid_t(r.pid);auto s=statFor(r.pid);if(!s||s->v[0]=="Z")return {false,"Process exited or is a zombie."};
    // Linux kernel threads have no userspace executable and cannot be managed safely.
    if(integer(s->v[6])&0x00200000u)return {false,"Kernel thread is protected."};
    if(r.action==Action::Priority){if(r.priority<-2||r.priority>2)return {false,"Priority out of range."};if(setpriority(PRIO_PROCESS,pid,-r.priority*5))return err();return {true,"Priority changed."};}
    if(r.action==Action::Affinity){
        cpu_set_t mask;CPU_ZERO(&mask);for(int c:r.cores){if(c<0||c>=CPU_SETSIZE)return {false,"Invalid CPU index."};CPU_SET(c,&mask);}
        if(r.cores.empty())return {false,"Select at least one core."};
        if(sched_setaffinity(pid,sizeof(mask),&mask))return err();
        return {true,"Affinity changed."};
    }
    int sig=r.action==Action::Terminate?SIGTERM:r.action==Action::Force?SIGKILL:r.action==Action::Suspend?SIGSTOP:SIGCONT;
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    int fd=int(syscall(SYS_pidfd_open,pid,0));
    if(fd>=0){auto id=processIdentity(r.pid);if(!id||*id!=r.identity){close(fd);return {false,"Process identity changed."};}int rc=int(syscall(SYS_pidfd_send_signal,fd,sig,nullptr,0));auto error=errno;close(fd);errno=error;if(rc)return err();return {true,"Signal sent."};}
    if(errno!=ENOSYS&&errno!=EINVAL)return err();
#endif
    if(kill(pid,sig))return err();
    return {true,"Signal sent."};
}
}
