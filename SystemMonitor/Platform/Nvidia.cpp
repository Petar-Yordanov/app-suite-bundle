#include "../Core/Monitor.hpp"
#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif
namespace sm {
#if defined(_WIN32)||defined(__linux__)
namespace {
struct Util {unsigned gpu,memory;};
struct Memory {unsigned long long total,free,used;};
class Nvidia {
#ifdef _WIN32
    HMODULE module=LoadLibraryExW(L"nvml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    void* resolve(const char* name){return module?reinterpret_cast<void*>(GetProcAddress(module,name)):nullptr;}
#else
    void* module=dlopen("libnvidia-ml.so.1",RTLD_LAZY|RTLD_LOCAL);
    void* resolve(const char* name){return module?dlsym(module,name):nullptr;}
#endif
    using Device=void*;
    using Init=int(*)();using Count=int(*)(unsigned*);using Get=int(*)(unsigned,Device*);
    using GetUtil=int(*)(Device,Util*);using GetMem=int(*)(Device,Memory*);using GetTemp=int(*)(Device,unsigned,unsigned*);
    Init init=reinterpret_cast<Init>(resolve("nvmlInit_v2")),shutdown=reinterpret_cast<Init>(resolve("nvmlShutdown"));
    Count count=reinterpret_cast<Count>(resolve("nvmlDeviceGetCount_v2"));
    Get get=reinterpret_cast<Get>(resolve("nvmlDeviceGetHandleByIndex_v2"));
    GetUtil util=reinterpret_cast<GetUtil>(resolve("nvmlDeviceGetUtilizationRates"));
    GetMem mem=reinterpret_cast<GetMem>(resolve("nvmlDeviceGetMemoryInfo"));
    GetTemp temp=reinterpret_cast<GetTemp>(resolve("nvmlDeviceGetTemperature"));
    bool ready=init&&init()==0;
public:
    ~Nvidia(){if(ready&&shutdown)shutdown();
#ifdef _WIN32
        if(module)FreeLibrary(module);
#else
        if(module)dlclose(module);
#endif
    }
    void sample(Raw& r){
        unsigned n=0;if(!ready||!count||!get||count(&n)!=0)return;
        for(unsigned i=0;i<n&&i<64;++i){Device d=nullptr;if(get(i,&d))continue;
            auto label="NVIDIA GPU "+std::to_string(i);Util u{};Memory m{};unsigned t=0;
            r.metrics.push_back({label+" utilization",util&&!util(d,&u)?Number(u.gpu):Number{},"%","NVML"});
            if(mem&&!mem(d,&m)){r.metrics.push_back({label+" VRAM used",double(m.used),"B","NVML"});r.metrics.push_back({label+" VRAM total",double(m.total),"B","NVML"});}
            if(temp&&!temp(d,0,&t))r.metrics.push_back({label+" temperature",double(t),"°C","NVML"});
        }
    }
};
}
void nvidiaMetrics(Raw& r){static thread_local Nvidia n;n.sample(r);}
#else
void nvidiaMetrics(Raw&){}
#endif
}
