#include "../Core/Pty.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#include <libproc.h>
#include <sys/proc_info.h>
#include <crt_externs.h>
#else
#include <pty.h>
extern char** environ;
#endif

namespace term {
namespace {
void configureFd(int fd){
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
}
class PosixPty final:public Pty {
    int master_,wakeRead_,wakeWrite_;
    std::atomic<bool> stopping_{false};
    mutable std::mutex processMutex_,queueMutex_;
    pid_t pid_;
    std::string outgoing_,initialCwd_;
    Output output_;Exited exited_;
    std::thread worker_;
    void wake(){char x=1;(void)::write(wakeWrite_,&x,1);}
    void run(){
        char buffer[32768];
        bool ended=false;
        while(!stopping_&&!ended){
            bool writing;
            {std::lock_guard lock(queueMutex_);writing=!outgoing_.empty();}
            pollfd fds[2]={{master_,short(POLLIN|(writing?POLLOUT:0)),0},{wakeRead_,POLLIN,0}};
            int n=poll(fds,2,-1);
            if(n<0){if(errno==EINTR)continue;break;}
            if(fds[1].revents&POLLIN){while(::read(wakeRead_,buffer,sizeof buffer)>0){}}
            if(stopping_)break;
            if(fds[0].revents&(POLLIN|POLLHUP|POLLERR)){
                // A bounded number of reads, so input gets serviced during floods.
                for(int i=0;i<8&&!stopping_;++i){
                    auto got=::read(master_,buffer,sizeof buffer);
                    if(got>0)output_({buffer,size_t(got)});
                    else if(got==0){ended=true;break;}
                    else if(errno==EINTR)continue;
                    else {if(errno!=EAGAIN&&errno!=EWOULDBLOCK)ended=true;else if(fds[0].revents&(POLLHUP|POLLERR))ended=true;break;}
                }
            }
            if(!ended&&(fds[0].revents&POLLOUT)){
                std::lock_guard lock(queueMutex_);
                if(!outgoing_.empty()){
                    auto n=::write(master_,outgoing_.data(),std::min<size_t>(outgoing_.size(),32768));
                    if(n>0)outgoing_.erase(0,size_t(n));
                    else if(n<0&&errno!=EINTR&&errno!=EAGAIN&&errno!=EWOULDBLOCK)ended=true;
                }
            }
            if(fds[0].revents&POLLNVAL)ended=true;
        }
        int status=0,code=-1;
        // Reap only our child. Keep the PID protected until waitpid completes.
        for(int i=0;;++i){
            {
                std::lock_guard lock(processMutex_);
                if(pid_<=0)break;
                auto r=waitpid(pid_,&status,WNOHANG);
                if(r==pid_){pid_=-1;code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);break;}
                if(r<0&&errno!=EINTR){pid_=-1;break;}
                if(stopping_&&i>5){kill(-pid_,SIGKILL);kill(pid_,SIGKILL);}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        exited_(code);
    }
public:
    PosixPty(int master,int rd,int wr,pid_t pid,std::string cwd,Output output,Exited exit):
        master_(master),wakeRead_(rd),wakeWrite_(wr),pid_(pid),initialCwd_(std::move(cwd)),output_(std::move(output)),exited_(std::move(exit)){
        worker_=std::thread([this]{run();});
    }
    ~PosixPty() override{stop();}
    bool write(std::string_view s) override{
        if(stopping_)return false;
        {std::lock_guard lock(queueMutex_);if(s.size()>QueueLimit-outgoing_.size())return false;outgoing_.append(s);}
        wake();return true;
    }
    void resize(int rows,int cols) override{
        if(stopping_)return;
        winsize size{};size.ws_row=static_cast<unsigned short>(rows);size.ws_col=static_cast<unsigned short>(cols);
        ioctl(master_,TIOCSWINSZ,&size);
    }
    std::string cwd() const override{
        std::lock_guard lock(processMutex_);
        if(pid_<=0)return initialCwd_;
#ifdef __APPLE__
        proc_vnodepathinfo info{};
        if(proc_pidinfo(pid_,PROC_PIDVNODEPATHINFO,0,&info,sizeof info)==sizeof info)return info.pvi_cdir.vip_path;
#else
        char buf[4096];auto path="/proc/"+std::to_string(pid_)+"/cwd";
        auto n=readlink(path.c_str(),buf,sizeof buf);
        if(n>0&&size_t(n)<sizeof buf)return std::string(buf,size_t(n));
#endif
        return initialCwd_;
    }
    void stop() override{
        if(stopping_.exchange(true))return;
        {
            std::lock_guard lock(processMutex_);
            if(pid_>0){
                auto fg=tcgetpgrp(master_);
                if(fg>0&&fg!=pid_)kill(-fg,SIGHUP);
                kill(-pid_,SIGHUP);kill(pid_,SIGHUP);
            }
        }
        wake();if(worker_.joinable())worker_.join();
        close(master_);close(wakeRead_);close(wakeWrite_);
    }
};
std::string keyOf(const std::string& s){return s.substr(0,s.find('='));}
}
std::unique_ptr<Pty> Pty::start(const Launch& l,Output out,Exited exit,std::string& error){
    if(l.executable.empty()){error="No shell executable";return {};}
    // Prepare all allocations before fork: child uses only async-signal-safe
    // calls until exec, important in a multithreaded Qt process.
    std::vector<std::string> strings{l.executable};strings.insert(strings.end(),l.arguments.begin(),l.arguments.end());
    std::vector<char*> argv;for(auto& s:strings)argv.push_back(s.data());argv.push_back(nullptr);
    std::vector<std::string> env;
#ifdef __APPLE__
    auto parentEnv=*_NSGetEnviron();
#else
    auto parentEnv=environ;
#endif
    for(char** e=parentEnv; e&&*e;++e)env.emplace_back(*e);
    for(const auto& s:l.environment){
        auto k=keyOf(s);std::erase_if(env,[&](const std::string& x){return keyOf(x)==k;});env.push_back(s);
    }
    std::vector<char*> envp;for(auto& s:env)envp.push_back(s.data());envp.push_back(nullptr);
    int wake[2];if(pipe(wake)<0){error=strerror(errno);return {};}
    configureFd(wake[0]);configureFd(wake[1]);
    winsize size{};size.ws_row=static_cast<unsigned short>(l.rows);size.ws_col=static_cast<unsigned short>(l.cols);
    int fd=-1;auto pid=forkpty(&fd,nullptr,nullptr,&size);
    if(pid==0){
        close(wake[0]);close(wake[1]);
        if(!l.cwd.empty()&&chdir(l.cwd.c_str())<0){
            const char msg[]="Could not enter working directory\r\n";::write(2,msg,sizeof(msg)-1);_exit(126);
        }
        execve(l.executable.c_str(),argv.data(),envp.data());
        const char msg[]="Could not execute shell\r\n";::write(2,msg,sizeof(msg)-1);_exit(127);
    }
    if(pid<0){error=strerror(errno);close(wake[0]);close(wake[1]);return {};}
    configureFd(fd);
    return std::make_unique<PosixPty>(fd,wake[0],wake[1],pid,l.cwd,std::move(out),std::move(exit));
}
}
