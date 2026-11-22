#include "../Core/Monitor.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <atomic>
#include <array>
#include <climits>
#include <mutex>
#include <thread>

namespace sm {
namespace {
constexpr GUID diskGuid{0x3d6fa8d4,0xfe05,0x11d0,{0x9d,0xda,0x00,0xc0,0x4f,0xd7,0xba,0x7c}};
constexpr GUID tcpGuid{0x9a280ac0,0xc8e0,0x11d1,{0x84,0xe2,0x00,0xc0,0x4f,0xb9,0x98,0xa2}};
constexpr GUID udpGuid{0xbf3a50c5,0xa9c9,0x4988,{0xa0,0x05,0x2d,0xf0,0xb7,0xc8,0x0f,0x80}};
struct Properties {
    EVENT_TRACE_PROPERTIES p{};
    wchar_t name[128]{};
    Properties(){p.Wnode.BufferSize=sizeof(*this);p.Wnode.Flags=WNODE_FLAG_TRACED_GUID;p.LoggerNameOffset=sizeof(p);}
};
bool property(EVENT_RECORD* e,const wchar_t* name,ULONG& value){
    PROPERTY_DATA_DESCRIPTOR desc{};desc.PropertyName=reinterpret_cast<ULONGLONG>(name);desc.ArrayIndex=ULONG_MAX;
    ULONG size=0;
    return TdhGetPropertySize(e,0,nullptr,1,&desc,&size)==ERROR_SUCCESS&&size==sizeof(value)&&
           TdhGetProperty(e,0,nullptr,1,&desc,sizeof(value),reinterpret_cast<PBYTE>(&value))==ERROR_SUCCESS;
}
struct Totals {U identity=0;std::array<U,4> bytes{};};
class Trace {
    TRACEHANDLE session_=0,consumer_=INVALID_PROCESSTRACE_HANDLE;
    std::wstring name_;
    std::thread thread_;
    std::atomic<bool> ended_{false};
    std::mutex mutex_;
    std::unordered_map<U,Totals> totals_;
    std::unordered_map<DWORD,DWORD> threads_;
    unsigned events_=0;
    bool overflow_=false;
    bool diskFailed_=false,netFailed_=false,seenDisk_=false,seenNetwork_=false;
    ULONG lastLost_=0,lastBuffersLost_=0;
    bool warming_=true;
    DWORD lastError_=0;
    std::chrono::steady_clock::time_point retry_{};
    static void WINAPI eventCallback(EVENT_RECORD* e){if(e->UserContext)static_cast<Trace*>(e->UserContext)->event(e);}
    void event(EVENT_RECORD* e){
        const auto& guid=e->EventHeader.ProviderId;auto opcode=e->EventHeader.EventDescriptor.Opcode;
        const bool disk=IsEqualGUID(guid,diskGuid),network=IsEqualGUID(guid,tcpGuid)||IsEqualGUID(guid,udpGuid);
        if(!disk&&!network)return;
        if(disk&&(opcode!=10&&opcode!=11))return;
        if(network&&(opcode!=10&&opcode!=11&&opcode!=26&&opcode!=27))return;
        // No strings, addresses, paths or packet contents are collected.
        // TDH validates the event version/property layout before attribution.
        {
            std::lock_guard guard(mutex_);
            if(events_>=50000){overflow_=true;return;}
            ++events_;
        }
        ULONG id=0,size=0;
        const bool decoded=disk?(property(e,L"IssuingThreadId",id)&&property(e,L"TransferSize",size)):
            (property(e,L"PID",id)&&property(e,L"size",size));
        std::lock_guard guard(mutex_);
        if(!decoded){if(disk)diskFailed_=true;else netFailed_=true;return;}
        if(disk)seenDisk_=true;else seenNetwork_=true;
        if(disk){auto i=threads_.find(id);if(i==threads_.end())return;id=i->second;}
        auto i=totals_.find(id);if(i==totals_.end())return;
        auto slot=disk?(opcode==10?0u:1u):((opcode==10||opcode==26)?3u:2u);
        i->second.bytes[slot]+=size;
    }
    bool start(){
        if(std::chrono::steady_clock::now()<retry_)return false;
        retry_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        name_=L"AppSuite-SystemMonitor-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        Properties props;props.p.Wnode.ClientContext=1;
        if(FAILED(CoCreateGuid(&props.p.Wnode.Guid)))return false;
        props.p.LogFileMode=EVENT_TRACE_REAL_TIME_MODE|EVENT_TRACE_SYSTEM_LOGGER_MODE;
        props.p.EnableFlags=EVENT_TRACE_FLAG_DISK_IO|EVENT_TRACE_FLAG_NETWORK_TCPIP;
        props.p.BufferSize=64;props.p.MinimumBuffers=4;props.p.MaximumBuffers=64;props.p.FlushTimer=1;
        auto status=StartTraceW(&session_,name_.c_str(),&props.p);
        if(status!=ERROR_SUCCESS){session_=0;lastError_=status;return false;}
        EVENT_TRACE_LOGFILEW log{};log.LoggerName=name_.data();log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD;
        log.EventRecordCallback=eventCallback;log.Context=this;
        consumer_=OpenTraceW(&log);
        if(consumer_==INVALID_PROCESSTRACE_HANDLE){lastError_=GetLastError();stop();return false;}
        ended_=false;warming_=true;lastLost_=0;lastBuffersLost_=0;
        try{thread_=std::thread([this]{ProcessTrace(&consumer_,1,nullptr,nullptr);ended_=true;});}
        catch(...){stop();return false;}
        return true;
    }
public:
    ~Trace(){stop();}
    void stop(){
        if(session_){Properties props;ControlTraceW(session_,name_.c_str(),&props.p,EVENT_TRACE_CONTROL_STOP);session_=0;}
        if(consumer_!=INVALID_PROCESSTRACE_HANDLE)CloseTrace(consumer_);
        if(thread_.joinable())thread_.join();
        consumer_=INVALID_PROCESSTRACE_HANDLE;
        totals_.clear();threads_.clear();events_=0;overflow_=false;warming_=true;
        diskFailed_=netFailed_=seenDisk_=seenNetwork_=false;
    }
    void sample(Raw& raw,bool enabled){
        if(!enabled){if(session_)stop();retry_={};return;}
        if(ended_&&session_)stop();
        if(!session_&&!start()){raw.warning+=" ETW unavailable (Windows error "+std::to_string(lastError_)+"); administrator access may be required.";return;}
        Properties props;
        const auto status=ControlTraceW(session_,name_.c_str(),&props.p,EVENT_TRACE_CONTROL_QUERY);
        std::unordered_map<DWORD,DWORD> threads;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        bool threadMapOk=snapshot!=INVALID_HANDLE_VALUE;
        if(threadMapOk){
            THREADENTRY32 e{};e.dwSize=sizeof(e);
            if(Thread32First(snapshot,&e))do{
                if(threads.size()>=131072){threadMapOk=false;break;}
                threads.emplace(e.th32ThreadID,e.th32OwnerProcessID);
            }while(Thread32Next(snapshot,&e));else threadMapOk=false;
            CloseHandle(snapshot);
        }
        std::lock_guard guard(mutex_);
        const bool lost=status!=ERROR_SUCCESS||props.p.EventsLost!=lastLost_||props.p.RealTimeBuffersLost!=lastBuffersLost_||overflow_||ended_;
        lastLost_=props.p.EventsLost;
        lastBuffersLost_=props.p.RealTimeBuffersLost;
        // Retain only currently observed process identities, and only
        // publish intervals with a complete trace buffer. A reused PID
        // cannot inherit the previous process's counters.
        std::unordered_map<U,Totals> next;
        for(auto& p:raw.processes)if(p.p.identity){
            Totals t{p.p.identity,{}};
            if(auto i=totals_.find(p.p.pid);i!=totals_.end()&&i->second.identity==p.p.identity)t=i->second;
            next.emplace(p.p.pid,t);
            if(lost||warming_)continue;
            if(seenNetwork_&&!netFailed_){p.netRead={t.bytes[2],true};p.netWrite={t.bytes[3],true};}
            if(seenDisk_&&!diskFailed_&&threadMapOk&&!threads_.empty()){p.read={t.bytes[0],true};p.write={t.bytes[1],true};}
        }
        totals_.swap(next);threads_.swap(threads);if(!threadMapOk)threads_.clear();
        events_=0;overflow_=false;warming_=lost;diskFailed_=netFailed_=false;
        raw.warning+=lost?" ETW data lost or sampling budget exceeded; rates unavailable.":" Optional ETW disk and TCP/UDP estimates; excludes unmapped/exited threads and processes. Buffer delivery can lag.";
    }
};
}
void collectWindowsTrace(Raw& raw,bool enabled){static thread_local Trace trace;trace.sample(raw,enabled);}
}
