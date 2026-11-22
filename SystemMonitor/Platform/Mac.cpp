#include "../Core/Monitor.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <ctime>
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/mount.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/processor_info.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pwd.h>
#include <unistd.h>
#include <set>

namespace sm {
namespace {
void metric(Raw& s,std::string n,Number v,std::string u,std::string d={}){s.metrics.push_back({std::move(n),v,std::move(u),std::move(d)});}
void counter(Raw& r,std::string n,U v,bool valid=true){metric(r,n,{},"B/s");r.counters[n]={v,valid};}
template<class T> std::optional<T> sys(const char* name){T v{};size_t n=sizeof(v);if(sysctlbyname(name,&v,&n,nullptr,0)==0&&n==sizeof(v))return v;return {};}
std::optional<proc_bsdinfo> bsd(U pid){proc_bsdinfo b{};if(proc_pidinfo(int(pid),PROC_PIDTBSDINFO,0,&b,sizeof(b))==sizeof(b))return b;return {};}
U id(const proc_bsdinfo& b){return U(b.pbi_start_tvsec)*1000000+b.pbi_start_tvusec;}
Result err(){return {false,std::strerror(errno),errno==EPERM||errno==EACCES};}
std::string username(uid_t uid){
    static thread_local std::unordered_map<uid_t,std::string> cache;if(auto i=cache.find(uid);i!=cache.end())return i->second;
    char buf[16384];passwd p{},*out=nullptr;std::string name=std::to_string(uid);
    if(getpwuid_r(uid,&p,buf,sizeof(buf),&out)==0&&out)name=p.pw_name;
    if(cache.size()>4096)cache.clear();return cache.emplace(uid,name).first->second;
}
std::string args(int pid){
    int mib[]={CTL_KERN,KERN_PROCARGS2,pid};size_t n=0;if(sysctl(mib,3,nullptr,&n,nullptr,0)||n<sizeof(int)||n>1024*1024)return {};
    std::vector<char> buf(n);if(sysctl(mib,3,buf.data(),&n,nullptr,0)||n<sizeof(int))return {};
    int argc;std::memcpy(&argc,buf.data(),sizeof(int));size_t i=sizeof(int);
    while(i<n&&buf[i])++i;while(i<n&&!buf[i])++i;
    std::string result;
    for(int j=0;j<argc&&i<n&&result.size()<8192;++j){size_t b=i;while(i<n&&buf[i])++i;if(j)result+=' ';result.append(buf.data()+b,std::min(i-b,8192-result.size()));++i;}
    return result;
}
Number cfnum(CFDictionaryRef dict,CFStringRef key){
    if(!dict)return {};auto v=CFDictionaryGetValue(dict,key);if(!v||CFGetTypeID(v)!=CFNumberGetTypeID())return {};
    double val=0;if(CFNumberGetValue(static_cast<CFNumberRef>(v),kCFNumberDoubleType,&val))return val;return {};
}
CFDictionaryRef properties(io_service_t service){
    CFMutableDictionaryRef d=nullptr;if(IORegistryEntryCreateCFProperties(service,&d,kCFAllocatorDefault,0)!=KERN_SUCCESS)return nullptr;return d;
}
CFDictionaryRef sub(CFDictionaryRef d,CFStringRef key){
    if(!d)return nullptr;auto v=CFDictionaryGetValue(d,key);return v&&CFGetTypeID(v)==CFDictionaryGetTypeID()?static_cast<CFDictionaryRef>(v):nullptr;
}
}
U ownPid(){return getpid();}
unsigned cpuCount(){static auto n=std::max(1l,sysconf(_SC_NPROCESSORS_ONLN));return unsigned(n);}
std::optional<U> processIdentity(U pid){if(auto b=bsd(pid))return id(*b);return {};}
bool protectPriority(){return setpriority(PRIO_PROCESS,0,-5)==0;}
Raw collectNative(bool details){
    Raw r;natural_t count=0;processor_info_array_t data=nullptr;mach_msg_type_number_t words=0;
    mach_port_t host=mach_host_self();
    if(host_processor_info(host,PROCESSOR_CPU_LOAD_INFO,&count,&data,&words)==KERN_SUCCESS){
        U totalBusy=0,totalTicks=0;
        for(unsigned i=0;i<count;++i){auto* c=data+i*CPU_STATE_MAX;
            U busy=U(c[CPU_STATE_USER])+c[CPU_STATE_SYSTEM]+c[CPU_STATE_NICE],ticks=busy+c[CPU_STATE_IDLE];
            auto label="CPU core "+std::to_string(i);metric(r,label,{},"%");r.counters[label+"/busy"]={busy,true};r.counters[label+"/total"]={ticks,true};totalBusy+=busy;totalTicks+=ticks;}
        metric(r,"CPU total",{},"%");r.counters["CPU total/busy"]={totalBusy,true};r.counters["CPU total/total"]={totalTicks,true};
        vm_deallocate(mach_task_self(),vm_address_t(data),size_t(words)*sizeof(integer_t));
    }else metric(r,"CPU total",{},"%");
    auto total=sys<U>("hw.memsize");vm_statistics64_data_t vm{};mach_msg_type_number_t n=HOST_VM_INFO64_COUNT;vm_size_t page=0;
    host_page_size(host,&page);
    if(host_statistics64(host,HOST_VM_INFO64,reinterpret_cast<host_info64_t>(&vm),&n)==KERN_SUCCESS){
        // App memory approximation includes active, wired and compressed memory.
        metric(r,"Physical memory used",double(U(vm.active_count)+vm.wire_count+vm.compressor_page_count)*page,"B","Active + wired + compressed; not memory pressure");
        metric(r,"Compressed memory",double(vm.compressor_page_count)*page,"B");
    }else metric(r,"Physical memory used",{},"B");
    metric(r,"Physical memory total",total?Number(*total):Number{},"B");
    if(auto s=sys<xsw_usage>("vm.swapusage")){metric(r,"Swap used",double(s->xsu_used),"B");metric(r,"Swap total",double(s->xsu_total),"B");}
    else metric(r,"Swap used",{},"B");
    metric(r,"Committed virtual memory",{},"B","No system-wide commit counter on macOS");
    mach_port_deallocate(mach_task_self(),host);
    U diskRead=0,diskWrite=0;bool disks=false;io_iterator_t iter=0;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOBlockStorageDriver"),&iter)==KERN_SUCCESS){
        for(io_service_t dev;(dev=IOIteratorNext(iter));){auto d=properties(dev);auto stats=sub(d,CFSTR("Statistics"));
            auto a=cfnum(stats,CFSTR("Bytes (Read)")),b=cfnum(stats,CFSTR("Bytes (Write)"));
            if(a&&b){disks=true;diskRead+=U(*a);diskWrite+=U(*b);}if(d)CFRelease(d);IOObjectRelease(dev);}
        IOObjectRelease(iter);
    }
    counter(r,"Disk reads",diskRead,disks);counter(r,"Disk writes",diskWrite,disks);
    U rx=0,tx=0;bool net=false;
    int mib[]={CTL_NET,PF_ROUTE,0,0,NET_RT_IFLIST2,0};size_t size=0;
    if(sysctl(mib,6,nullptr,&size,nullptr,0)==0&&size<=16*1024*1024){
        std::vector<unsigned char> buffer(size);
        if(sysctl(mib,6,buffer.data(),&size,nullptr,0)==0){
            for(size_t offset=0;offset+sizeof(if_msghdr)<=size;){
                auto* h=reinterpret_cast<const if_msghdr*>(buffer.data()+offset);
                if(!h->ifm_msglen||h->ifm_msglen>size-offset)break;
                if(h->ifm_type==RTM_IFINFO2&&h->ifm_msglen>=sizeof(if_msghdr2)){
                    auto* d=reinterpret_cast<const if_msghdr2*>(h);
                    if(!(d->ifm_flags&IFF_LOOPBACK)){rx+=d->ifm_data.ifi_ibytes;tx+=d->ifm_data.ifi_obytes;net=true;}
                }
                offset+=h->ifm_msglen;
            }
        }
    }
    counter(r,"Network download",rx,net);counter(r,"Network upload",tx,net);
    struct statfs* mounts=nullptr;int num=getmntinfo(&mounts,MNT_NOWAIT);
    std::set<std::string> devices;
    for(int i=0;i<num;++i){auto& m=mounts[i];if(!(m.f_flags&MNT_LOCAL)||!std::string(m.f_mntfromname).starts_with("/dev/")||!devices.insert(m.f_mntfromname).second)continue;
        auto name=std::string("Storage ")+m.f_mntonname;metric(r,name+" used",double(m.f_blocks-m.f_bfree)*m.f_bsize,"B");metric(r,name+" total",double(m.f_blocks)*m.f_bsize,"B","APFS shared volumes overlap; do not sum");
    }
    bool gpu=false;iter=0;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOAccelerator"),&iter)==KERN_SUCCESS){
        int index=0;
        for(io_service_t dev;(dev=IOIteratorNext(iter));){auto d=properties(dev);auto stats=sub(d,CFSTR("PerformanceStatistics"));
            auto busy=cfnum(stats,CFSTR("Device Utilization %"));auto used=cfnum(stats,CFSTR("vramUsedBytes"));auto allocated=cfnum(stats,CFSTR("Alloc system memory"));
            auto label="GPU "+std::to_string(index++);
            if(busy||used||allocated){gpu=true;metric(r,label+" utilization",busy,"%","Driver-reported");metric(r,label+" VRAM used",used,"B");if(allocated)metric(r,label+" shared allocation",allocated,"B");}
            if(d)CFRelease(d);IOObjectRelease(dev);
        }IOObjectRelease(iter);
    }
    if(!gpu){metric(r,"GPU utilization",{},"%","Driver does not expose IOAccelerator statistics");metric(r,"VRAM used",{},"B","Apple Silicon uses unified memory");}
    metric(r,"Hardware temperatures",{},"°C","No public generic sensor interface; thermal pressure is not temperature");
    int capacity=proc_listallpids(nullptr,0);
    if(capacity>0&&capacity<1000000){
        std::vector<pid_t> pids(size_t(capacity)+512);
        int result=proc_listallpids(pids.data(),int(pids.size()*sizeof(pid_t)));
        auto now=double(std::time(nullptr));
        for(int i=0;i<std::min(result,int(pids.size()));++i){auto pid=pids[i];if(pid<=0)continue;
            proc_taskallinfo a{};if(proc_pidinfo(pid,PROC_PIDTASKALLINFO,0,&a,sizeof(a))!=sizeof(a)){
                char name[PROC_PIDPATHINFO_MAXSIZE]{};if(proc_name(pid,name,sizeof(name))>0){RawProcess p;p.p.pid=pid;p.p.name=name;r.processes.push_back(std::move(p));}continue;}
            auto& b=a.pbsd;auto& t=a.ptinfo;RawProcess p;p.p.pid=pid;p.p.parent=b.pbi_ppid;p.p.identity=id(b);p.p.name=b.pbi_name[0]?b.pbi_name:b.pbi_comm;
            p.p.memory=double(t.pti_resident_size);p.p.virtualMemory=double(t.pti_virtual_size);p.p.threads=t.pti_threadnum;p.p.uptime=std::max(0.,now-double(b.pbi_start_tvsec));
            p.cpu={t.pti_total_user+t.pti_total_system,true};
            rusage_info_v2 ru{};if(proc_pid_rusage(pid,RUSAGE_INFO_V2,reinterpret_cast<rusage_info_t*>(&ru))==0){p.read={ru.ri_diskio_bytesread,true};p.write={ru.ri_diskio_byteswritten,true};p.cpu={ru.ri_user_time+ru.ri_system_time,true};}
            if(details){char path[PROC_PIDPATHINFO_MAXSIZE]{};if(proc_pidpath(pid,path,sizeof(path))>0)p.p.path=path;p.p.command=args(pid);p.p.user=username(b.pbi_uid);}
            r.processes.push_back(std::move(p));
        }
    }
    r.warning="Per-process network traffic and CPU affinity are not available through public process APIs on macOS.";
    return r;
}
Result operateNative(const Request& r){
    if(r.pid<=1)return {false,"System process is protected."};
    if(r.action==Action::Affinity)return {false,"macOS does not expose per-process CPU affinity."};
    int pid=int(r.pid);
    if(r.action==Action::Priority){if(r.priority<-2||r.priority>2)return {false,"Priority out of range."};if(setpriority(PRIO_PROCESS,pid,-5*r.priority))return err();return {true,"Priority changed."};}
    auto s=r.action==Action::Terminate?SIGTERM:r.action==Action::Force?SIGKILL:r.action==Action::Suspend?SIGSTOP:SIGCONT;
    if(kill(pid,s))return err();return {true,"Signal sent."};
}
}
