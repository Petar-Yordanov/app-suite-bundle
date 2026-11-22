#include "../Core/Monitor.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <winternl.h>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace sm {
namespace {
struct Handle {HANDLE h=nullptr;explicit Handle(HANDLE v=nullptr):h(v){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}operator bool()const{return h&&h!=INVALID_HANDLE_VALUE;}};
std::string utf(const wchar_t* text,int len=-1){
    if(!text||!len)return {};
    int n=WideCharToMultiByte(CP_UTF8,0,text,len,nullptr,0,nullptr,nullptr);if(n<=0)return {};
    std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,text,len,s.data(),n,nullptr,nullptr);if(len<0)s.pop_back();return s;
}
U ft(FILETIME t){return (U(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
Result error(DWORD e=GetLastError()){return {false,"Windows error "+std::to_string(e),e==ERROR_ACCESS_DENIED||e==ERROR_PRIVILEGE_NOT_HELD};}
std::optional<U> identity(HANDLE h){FILETIME c,e,k,u;if(GetProcessTimes(h,&c,&e,&k,&u))return ft(c);return {};}
void metric(Raw& s,std::string n,Number v,std::string u,std::string d={}){s.metrics.push_back({std::move(n),v,std::move(u),std::move(d)});}
using NtQuery=NTSTATUS(NTAPI*)(HANDLE,PROCESSINFOCLASS,PVOID,ULONG,PULONG);
using NtAction=NTSTATUS(NTAPI*)(HANDLE);
auto ntdll(){return GetModuleHandleW(L"ntdll.dll");}
std::string command(HANDLE p){
    static auto query=reinterpret_cast<NtQuery>(GetProcAddress(ntdll(),"NtQueryInformationProcess"));
    if(!query)return {};ULONG size=0;
    query(p,static_cast<PROCESSINFOCLASS>(60),nullptr,0,&size);
    if(size<sizeof(UNICODE_STRING)||size>65536)return {};
    std::vector<unsigned char> buf(size);
    if(query(p,static_cast<PROCESSINFOCLASS>(60),buf.data(),size,&size)<0)return {};
    auto* s=reinterpret_cast<UNICODE_STRING*>(buf.data());
    auto start=reinterpret_cast<uintptr_t>(buf.data()),ptr=reinterpret_cast<uintptr_t>(s->Buffer);
    if(ptr<start||ptr-start>buf.size()||s->Length>buf.size()-(ptr-start)||s->Length%2)return {};
    return utf(s->Buffer,std::min<int>(s->Length/2,4096));
}
std::string user(HANDLE h){
    HANDLE raw=nullptr;if(!OpenProcessToken(h,TOKEN_QUERY,&raw))return {};
    Handle token(raw);DWORD n=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&n);
    if(!n||n>65536)return {};std::vector<unsigned char> buf(n);
    if(!GetTokenInformation(token.h,TokenUser,buf.data(),n,&n))return {};
    auto sid=reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;
    wchar_t name[256],domain[256];DWORD a=256,b=256;SID_NAME_USE type;
    if(!LookupAccountSidW(nullptr,sid,name,&a,domain,&b,&type))return {};
    return utf(domain)+"\\"+utf(name);
}
class Pdh {
    PDH_HQUERY query_=nullptr;
    std::map<std::wstring,PDH_HCOUNTER> counters_;
public:
    Pdh(){
        if(PdhOpenQueryW(nullptr,0,&query_)!=ERROR_SUCCESS)return;
        for(auto path:{L"\\Processor Information(*)\\% Processor Time",L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",L"\\Network Interface(*)\\Bytes Received/sec",L"\\Network Interface(*)\\Bytes Sent/sec",L"\\GPU Engine(*)\\Utilization Percentage",L"\\GPU Adapter Memory(*)\\Dedicated Usage"}){
            PDH_HCOUNTER c=nullptr;if(PdhAddEnglishCounterW(query_,path,0,&c)==ERROR_SUCCESS)counters_[path]=c;
        }
    }
    ~Pdh(){if(query_)PdhCloseQuery(query_);}
    void sample(){if(query_)PdhCollectQueryData(query_);}
    Number single(const wchar_t* path){
        auto i=counters_.find(path);if(i==counters_.end())return {};
        PDH_FMT_COUNTERVALUE v{};if(PdhGetFormattedCounterValue(i->second,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,nullptr,&v)!=ERROR_SUCCESS||v.CStatus>1)return {};
        return v.doubleValue;
    }
    std::vector<std::pair<std::string,double>> array(const wchar_t* path){
        auto i=counters_.find(path);if(i==counters_.end())return {};
        DWORD bytes=0,n=0;if(PdhGetFormattedCounterArrayW(i->second,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&n,nullptr)!=PDH_MORE_DATA||bytes>32*1024*1024)return {};
        std::vector<unsigned char> data(bytes);
        auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(data.data());
        if(PdhGetFormattedCounterArrayW(i->second,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&n,items)!=ERROR_SUCCESS)return {};
        std::vector<std::pair<std::string,double>> out;
        for(DWORD j=0;j<n;++j)if(items[j].FmtValue.CStatus<=1)out.emplace_back(utf(items[j].szName),items[j].FmtValue.doubleValue);
        return out;
    }
    Number sum(const wchar_t* path){auto a=array(path);if(a.empty())return {};double s=0;for(auto& [n,v]:a)s+=v;return s;}
};
}
U ownPid(){return GetCurrentProcessId();}
unsigned cpuCount(){static auto n=std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));return n;}
std::optional<U> processIdentity(U pid){Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid)));return p?identity(p.h):std::nullopt;}
bool protectPriority(){return SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS)!=0;}
Raw collectNative(bool details){
    Raw r;static thread_local Pdh pdh;pdh.sample();
    FILETIME idle,kernel,userTime;if(GetSystemTimes(&idle,&kernel,&userTime)){
        U total=ft(kernel)+ft(userTime),busy=total-ft(idle);
        metric(r,"CPU total",{},"%");r.counters["CPU total/busy"]={busy,true};r.counters["CPU total/total"]={total,true};
    }else metric(r,"CPU total",{},"%");
    auto cores=pdh.array(L"\\Processor Information(*)\\% Processor Time");
    if(cores.empty())metric(r,"Individual CPU cores",{},"%","PDH counter warming up or unavailable");
    for(auto& [n,v]:cores)if(n.find("_Total")==std::string::npos)metric(r,"CPU core "+n,std::clamp(v,0.,100.),"%");
    MEMORYSTATUSEX m{};m.dwLength=sizeof(m);
    if(GlobalMemoryStatusEx(&m)){
        metric(r,"Physical memory used",double(m.ullTotalPhys-m.ullAvailPhys),"B");metric(r,"Physical memory total",double(m.ullTotalPhys),"B");
        metric(r,"Committed virtual memory",double(m.ullTotalPageFile-m.ullAvailPageFile),"B");metric(r,"Commit limit",double(m.ullTotalPageFile),"B");
    }
    PERFORMANCE_INFORMATION perf{};perf.cb=sizeof(perf);
    if(GetPerformanceInfo(&perf,sizeof(perf)))metric(r,"Commit peak",double(perf.CommitPeak)*perf.PageSize,"B");
    struct PageFile {U total=0,used=0;bool valid=false;} pagefiles;
    SYSTEM_INFO info{};GetSystemInfo(&info);
    if(EnumPageFilesW([](LPVOID ctx,PENUM_PAGE_FILE_INFORMATION e,LPCWSTR)->BOOL{
        auto& s=*static_cast<PageFile*>(ctx);s.total+=e->TotalSize;s.used+=e->TotalInUse;s.valid=true;return TRUE;
    },&pagefiles)&&pagefiles.valid){
        metric(r,"Swap used",double(pagefiles.used)*info.dwPageSize,"B");metric(r,"Swap total",double(pagefiles.total)*info.dwPageSize,"B");
    }else metric(r,"Swap used",{},"B");
    // Native pagefile performance counter reflects actual use, not commit pressure.
    static PDH_HQUERY pageQuery=nullptr;static PDH_HCOUNTER pageCounter=nullptr;
    if(!pageQuery&&PdhOpenQueryW(nullptr,0,&pageQuery)==ERROR_SUCCESS)PdhAddEnglishCounterW(pageQuery,L"\\Paging File(_Total)\\% Usage",0,&pageCounter);
    PDH_FMT_COUNTERVALUE pv{};Number page;
    if(pageQuery&&pageCounter&&PdhCollectQueryData(pageQuery)==ERROR_SUCCESS&&PdhGetFormattedCounterValue(pageCounter,PDH_FMT_DOUBLE,nullptr,&pv)==ERROR_SUCCESS&&pv.CStatus<=1)page=pv.doubleValue;
    metric(r,"Pagefile utilization",page,"%");
    metric(r,"Disk reads",pdh.single(L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec"),"B/s");
    metric(r,"Disk writes",pdh.single(L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec"),"B/s");
    metric(r,"Network download",pdh.sum(L"\\Network Interface(*)\\Bytes Received/sec"),"B/s");
    metric(r,"Network upload",pdh.sum(L"\\Network Interface(*)\\Bytes Sent/sec"),"B/s");
    for(DWORD mask=GetLogicalDrives(),i=0;i<26;++i)if(mask&(1u<<i)){
        wchar_t root[]={wchar_t(L'A'+i),L':',L'\\',0};if(GetDriveTypeW(root)!=DRIVE_FIXED)continue;
        ULARGE_INTEGER total{},free{};if(GetDiskFreeSpaceExW(root,nullptr,&total,&free)){
            auto label="Storage "+utf(root);metric(r,label+" used",double(total.QuadPart-free.QuadPart),"B");metric(r,label+" total",double(total.QuadPart),"B");
        }
    }
    auto engines=pdh.array(L"\\GPU Engine(*)\\Utilization Percentage");
    std::map<std::string,double> totals;
    for(auto& [n,v]:engines){auto index=n.find("_luid_");if(index!=std::string::npos)totals[n.substr(index)]+=v;}
    Number gpu;if(!totals.empty()){double v=0;for(auto& x:totals)v=std::max(v,x.second);gpu=std::clamp(v,0.,100.);}
    metric(r,"GPU utilization",gpu,"%","Busiest physical GPU engine; WDDM counters");
    metric(r,"VRAM used",pdh.sum(L"\\GPU Adapter Memory(*)\\Dedicated Usage"),"B");
    IDXGIFactory1* factory=nullptr;
    if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)))){
        for(UINT i=0;;++i){IDXGIAdapter1* a=nullptr;if(factory->EnumAdapters1(i,&a)!=S_OK)break;DXGI_ADAPTER_DESC1 d{};
            if(SUCCEEDED(a->GetDesc1(&d))&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))metric(r,"VRAM total "+utf(d.Description),double(d.DedicatedVideoMemory),"B");
            a->Release();}
        factory->Release();
    }
    metric(r,"Hardware temperatures",{},"°C","No general reliable Windows sensor API");
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W e{};e.dwSize=sizeof(e);
    FILETIME now;GetSystemTimeAsFileTime(&now);
    if(snapshot&&Process32FirstW(snapshot.h,&e))do{
        RawProcess p;p.p.pid=e.th32ProcessID;p.p.parent=e.th32ParentProcessID;p.p.name=utf(e.szExeFile);p.p.threads=e.cntThreads;
        Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID));
        if(h){FILETIME c,x,k,u;if(GetProcessTimes(h.h,&c,&x,&k,&u)){p.p.identity=ft(c);p.cpu={(ft(k)+ft(u))*100,true};if(ft(now)>=ft(c))p.p.uptime=double(ft(now)-ft(c))/1e7;}
            PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);if(GetProcessMemoryInfo(h.h,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem))){p.p.memory=double(mem.WorkingSetSize);p.p.virtualMemory=double(mem.PrivateUsage);}
            if(details){wchar_t path[32768];DWORD n=32768;if(QueryFullProcessImageNameW(h.h,0,path,&n))p.p.path=utf(path,int(n));p.p.command=command(h.h);p.p.user=user(h.h);}
        }
        r.processes.push_back(std::move(p));
    }while(Process32NextW(snapshot.h,&e));
    r.warning="Exact per-process disk/network throughput requires ETW attribution; unavailable. Process virtual memory is private commit.";
    return r;
}
Result operateNative(const Request& r){
    DWORD access=PROCESS_QUERY_LIMITED_INFORMATION;
    switch(r.action){case Action::Force:access|=PROCESS_TERMINATE;break;case Action::Suspend:case Action::Resume:access|=PROCESS_SUSPEND_RESUME;break;case Action::Priority:case Action::Affinity:access|=PROCESS_SET_INFORMATION;break;default:break;}
    Handle p(OpenProcess(access,FALSE,DWORD(r.pid)));if(!p)return error();
    if(identity(p.h)!=std::optional<U>(r.identity))return {false,"Process identity changed."};
    BOOL critical=FALSE;if(r.pid==4||!IsProcessCritical(p.h,&critical)||critical)return {false,"Critical or unverified system process is protected."};
    switch(r.action){
        case Action::Force:if(!TerminateProcess(p.h,1))return error();break;
        case Action::Terminate:{
            struct Context{DWORD pid;bool sent=false;} ctx{DWORD(r.pid)};
            EnumWindows([](HWND w,LPARAM l)->BOOL{auto& c=*reinterpret_cast<Context*>(l);DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==c.pid&&IsWindowVisible(w)&&PostMessageW(w,WM_CLOSE,0,0))c.sent=true;return TRUE;},reinterpret_cast<LPARAM>(&ctx));
            if(!ctx.sent)return {false,"No responsive top-level window; use force termination for console/background processes."};break;
        }
        case Action::Suspend:case Action::Resume:{
            auto fn=reinterpret_cast<NtAction>(GetProcAddress(ntdll(),r.action==Action::Suspend?"NtSuspendProcess":"NtResumeProcess"));
            if(!fn)return {false,"Native suspend/resume API unavailable."};auto status=fn(p.h);
            if(status<0)return {false,"NTSTATUS "+std::to_string(status),status==NTSTATUS(0xc0000022)};break;
        }
        case Action::Priority:{
            if(r.priority<-2||r.priority>2)return {false,"Priority out of range."};
            const DWORD classes[]={IDLE_PRIORITY_CLASS,BELOW_NORMAL_PRIORITY_CLASS,NORMAL_PRIORITY_CLASS,ABOVE_NORMAL_PRIORITY_CLASS,HIGH_PRIORITY_CLASS};
            if(!SetPriorityClass(p.h,classes[r.priority+2]))return error();break;
        }
        case Action::Affinity:{
            if(GetActiveProcessorGroupCount()>1)return {false,"CPU affinity on multiple processor groups is unsupported."};
            DWORD_PTR mask=0;for(auto c:r.cores){if(c<0||c>=int(sizeof(mask)*8))return {false,"Invalid CPU index."};mask|=DWORD_PTR(1)<<c;}
            if(!mask)return {false,"Select at least one core."};if(!SetProcessAffinityMask(p.h,mask))return error();break;
        }
    }
    return {true,"Process operation requested."};
}
}
