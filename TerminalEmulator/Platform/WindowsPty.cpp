#include "../Core/Pty.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace term {
namespace {
std::wstring wide(std::string_view s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    std::wstring w(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);return w;
}
std::wstring key(const std::wstring& s){return s.substr(0,s.find(L'=',s.starts_with(L'=')?1:0));}
std::vector<wchar_t> environment(const Launch& l){
    std::vector<std::wstring> e;
    auto* raw=GetEnvironmentStringsW();
    if(raw){for(auto* p=raw;*p;p+=wcslen(p)+1)e.emplace_back(p);FreeEnvironmentStringsW(raw);}
    for(auto& s:l.environment){
        auto w=wide(s),k=key(w);
        std::erase_if(e,[&](const std::wstring& a){return _wcsicmp(key(a).c_str(),k.c_str())==0;});e.push_back(w);
    }
    std::sort(e.begin(),e.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> block;
    for(auto& s:e){block.insert(block.end(),s.begin(),s.end());block.push_back(0);}
    block.push_back(0);if(block.size()==1)block.push_back(0);return block;
}
void close(HANDLE& h){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
class WindowsPty final:public Pty{
    HANDLE input_,outputHandle_,process_,job_;
    HPCON console_;
    std::atomic<bool> stopping_{false},ended_{false};
    std::mutex mutex_;std::condition_variable cv_;
    std::string queue_,initialCwd_;
    COORD size_{};bool resized_=false;
    Output output_;Exited exited_;
    std::thread reader_,writer_,monitor_;
    void readLoop(){
        char buf[32768];DWORD n=0;
        while(ReadFile(outputHandle_,buf,sizeof buf,&n,nullptr)&&n){
            if(!stopping_)output_({buf,n});
        }
        ended_=true;cv_.notify_all();
    }
    void writeLoop(){
        while(!stopping_&&!ended_){
            std::string data;COORD size{};bool resize=false;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock,[&]{return stopping_||ended_||!queue_.empty()||resized_;});
                if(stopping_||ended_)break;
                if(resized_){resize=true;size=size_;resized_=false;}
                if(!queue_.empty()){auto n=std::min<size_t>(queue_.size(),32768);data=queue_.substr(0,n);queue_.erase(0,n);}
            }
            if(resize&&!stopping_)ResizePseudoConsole(console_,size);
            if(!data.empty()&&!stopping_){
                size_t done=0;while(done<data.size()&&!stopping_){
                    DWORD n=0;
                    if(!WriteFile(input_,data.data()+done,DWORD(data.size()-done),&n,nullptr)||!n){ended_=true;break;}
                    done+=n;
                }
            }
        }
    }
public:
    WindowsPty(HANDLE in,HANDLE out,HANDLE proc,HANDLE job,HPCON console,std::string cwd,Output cb,Exited exit):
        input_(in),outputHandle_(out),process_(proc),job_(job),console_(console),initialCwd_(std::move(cwd)),output_(std::move(cb)),exited_(std::move(exit)){
        reader_=std::thread([this]{readLoop();});writer_=std::thread([this]{writeLoop();});
        monitor_=std::thread([this]{
            WaitForSingleObject(process_,INFINITE);DWORD code=1;GetExitCodeProcess(process_,&code);
            ended_=true;cv_.notify_all();exited_(int(code));
        });
    }
    ~WindowsPty() override{stop();}
    bool write(std::string_view data) override{
        if(stopping_||ended_)return false;
        {std::lock_guard lock(mutex_);if(data.size()>QueueLimit-queue_.size())return false;queue_.append(data);}
        cv_.notify_one();return true;
    }
    void resize(int rows,int cols) override{
        if(stopping_||ended_)return;
        {std::lock_guard lock(mutex_);size_={SHORT(cols),SHORT(rows)};resized_=true;}
        cv_.notify_one();
    }
    std::string cwd() const override{return initialCwd_;} // OSC 7 is handled at the emulation layer.
    void stop() override{
        if(stopping_.exchange(true))return;
        cv_.notify_all();
        // Terminate only this session's process tree. Keep draining ConPTY's
        // output during shutdown, otherwise older Windows builds can deadlock.
        if(!job_||!TerminateJobObject(job_,1))TerminateProcess(process_,1);
        if(writer_.joinable()){CancelSynchronousIo(writer_.native_handle());writer_.join();}
        close(input_);
        ClosePseudoConsole(console_);console_=nullptr;
        if(reader_.joinable()){CancelSynchronousIo(reader_.native_handle());reader_.join();}
        if(monitor_.joinable())monitor_.join();
        close(outputHandle_);close(process_);close(job_);
    }
};
}
std::unique_ptr<Pty> Pty::start(const Launch& l,Output out,Exited exit,std::string& error){
    HANDLE inRead=nullptr,inWrite=nullptr,outRead=nullptr,outWrite=nullptr,job=nullptr;
    HPCON pc=nullptr;
    auto fail=[&](std::string msg)->std::unique_ptr<Pty>{
        error=std::move(msg);close(inRead);close(inWrite);close(outRead);close(outWrite);close(job);
        if(pc)ClosePseudoConsole(pc);return {};
    };
    if(!CreatePipe(&inRead,&inWrite,nullptr,0)||!CreatePipe(&outRead,&outWrite,nullptr,0))return fail("Could not create ConPTY pipes");
    auto hr=CreatePseudoConsole({SHORT(l.cols),SHORT(l.rows)},inRead,outWrite,0,&pc);
    close(inRead);close(outWrite);
    if(FAILED(hr))return fail("CreatePseudoConsole failed: "+std::to_string(unsigned(hr)));
    SIZE_T count=0;InitializeProcThreadAttributeList(nullptr,1,0,&count);
    std::vector<unsigned char> attrs(count);
    auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&count))return fail("Could not allocate ConPTY attributes");
    if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,pc,sizeof(pc),nullptr,nullptr)){
        DeleteProcThreadAttributeList(list);return fail("Could not attach ConPTY attribute");
    }
    STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.lpAttributeList=list;
    // Explicitly request fresh console standard handles. Otherwise Windows
    // can duplicate redirected parent handles even with inheritance off.
    // Null hStdInput/hStdOutput/hStdError are populated by ConPTY.
    si.StartupInfo.dwFlags|=STARTF_USESTDHANDLES;
    PROCESS_INFORMATION process{};
    const auto exe=wide(l.executable),cwd=wide(l.cwd);
    auto cmd=quoteWindows(exe);for(auto& arg:l.arguments)cmd+=L" "+quoteWindows(wide(arg));
    auto env=environment(l);
    BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,
       EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED,env.data(),
       cwd.empty()?nullptr:cwd.c_str(),&si.StartupInfo,&process);
    auto code=GetLastError();DeleteProcThreadAttributeList(list);
    if(!ok)return fail("CreateProcessW failed: "+std::to_string(code));
    job=CreateJobObjectW(nullptr,nullptr);
    if(job){
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits)||!AssignProcessToJobObject(job,process.hProcess))close(job);
    }
    ResumeThread(process.hThread);close(process.hThread);
    return std::make_unique<WindowsPty>(inWrite,outRead,process.hProcess,job,pc,l.cwd,std::move(out),std::move(exit));
}
}
