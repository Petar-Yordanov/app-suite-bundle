#include "../Core/Monitor.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif
static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}}
int main(int argc,char** argv){
    if(argc>1&&std::string_view(argv[1])=="--child"){for(;;)std::this_thread::sleep_for(std::chrono::seconds(1));}
    check(!sm::rate({1,true},{2,true},1),"counter reset");
    check(sm::rate({90,true},{30,true},2)==30,"rate calculation");
    check(!sm::rate({90,false},{30,true},2),"unavailable");
    sm::Sampler s;auto first=s.sample();check(!first.processes.empty()&&!first.metrics.empty(),"native snapshot");
    auto self=sm::ownPid();auto it=std::find_if(first.processes.begin(),first.processes.end(),[&](auto& p){return p.pid==self;});
    check(it!=first.processes.end()&&it->identity&&it->memory&&*it->memory>0,"self process identity and memory");
    auto identity=sm::processIdentity(self);check(identity&&*identity==it->identity,"identity consistency");
    check(!sm::operate({self,*identity,sm::Action::Force,0,{}}).ok,"self protection");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    auto second=s.sample();
    auto cpu=std::find_if(second.metrics.begin(),second.metrics.end(),[](auto& m){return m.name=="CPU total";});
    check(cpu!=second.metrics.end()&&cpu->value&&*cpu->value>=0&&*cpu->value<=100,"CPU utilization");
    sm::Snapshot tree;sm::Process parent,child,grand,stale;
    parent.pid=20;parent.identity=100;child.pid=21;child.parent=20;child.identity=101;grand.pid=22;grand.parent=21;grand.identity=102;stale.pid=23;stale.parent=20;stale.identity=99;
    tree.processes={parent,child,grand,stale};auto requests=sm::processTree(tree,{20,100,sm::Action::Force,0,{}});
    check(requests.size()==3&&requests.front().pid==22&&requests.back().pid==20,"tree children-first with reuse protection");
    sm::U pid=0;
#ifdef _WIN32
    auto exe=std::filesystem::absolute(argv[0]).wstring(),cmd=L"\""+exe+L"\" --child";
    PROCESS_INFORMATION proc{};STARTUPINFOW st{};st.cb=sizeof(st);
    check(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&st,&proc)!=0,"spawn disposable process");pid=proc.dwProcessId;
#else
    auto p=fork();if(p==0){for(;;)pause();}check(p>0,"fork disposable process");pid=p;
#endif
    auto id=sm::processIdentity(pid);check(id.has_value(),"child identity");
    check(!sm::operate({pid,*id+1,sm::Action::Force,0,{}}).ok,"stale PID protection");
    auto suspend=sm::operate({pid,*id,sm::Action::Suspend,0,{}});auto resume=sm::operate({pid,*id,sm::Action::Resume,0,{}});
    auto stop=sm::operate({pid,*id,sm::Action::Force,0,{}});
#ifdef _WIN32
    if(!stop.ok)TerminateProcess(proc.hProcess,1);
    auto done=WaitForSingleObject(proc.hProcess,5000);CloseHandle(proc.hThread);CloseHandle(proc.hProcess);check(done==WAIT_OBJECT_0,"disposable process exited");
#else
    if(!stop.ok)kill(pid,SIGKILL);
    int status=0;waitpid(pid,&status,0);check(WIFSIGNALED(status),"disposable process exited");
#endif
    if(!suspend.ok||!resume.ok||!stop.ok)std::cerr<<suspend.message<<"; "<<resume.message<<"; "<<stop.message<<'\n';
    check(suspend.ok&&resume.ok&&stop.ok,"suspend/resume/force disposable process");
    std::cout<<"Native metrics, delta rates, PID protection, tree ordering and live process control passed.\n";
}
