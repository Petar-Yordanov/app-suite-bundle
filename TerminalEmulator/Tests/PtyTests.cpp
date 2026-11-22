#include "../Core/Pty.hpp"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <regex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

static std::string size(){
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),&info))return "no console";
    return std::to_string(info.srWindow.Right-info.srWindow.Left+1)+"x"+std::to_string(info.srWindow.Bottom-info.srWindow.Top+1);
#else
    winsize s{};
    if(!isatty(STDIN_FILENO)||ioctl(STDOUT_FILENO,TIOCGWINSZ,&s))return "no pty";
    return std::to_string(s.ws_col)+"x"+std::to_string(s.ws_row);
#endif
}
// ConPTY emits VT control sequences even between two pieces of the same
// std::cout line (e.g. READY:\033[?25h80x24). Assert the printable
// protocol markers, rather than requiring raw console output to be plain text.
// Work on the accumulated output so sequences split across reads are handled.
static std::string visibleText(const std::string& bytes) {
    static const std::regex osc(R"(\x1b\][^\x07\x1b]*(\x07|\x1b\\))");
    static const std::regex csi(R"(\x1b\[[0-?]*[ -/]*[@-~])");
    return std::regex_replace(std::regex_replace(bytes, osc, ""), csi, "");
}
int main(int argc,char** argv){
    if(argc>1&&std::string_view(argv[1])=="--child"){
        std::cout<<"READY:"<<size()<<std::endl;
        std::string line;std::getline(std::cin,line);
        std::cout<<"INPUT:"<<line<<";SIZE:"<<size()<<std::endl;
        return 0;
    }
    if(term::quoteWindows(L"a b")!=L"\"a b\""||term::quoteWindows(L"x\\")!=L"\"x\\\\\"")return 1;
    if(visibleText("READY:\x1b[?25h80x24")!="READY:80x24" ||
       visibleText("\x1b]0;window title\x07INPUT:hello;SIZE:\x1b[0m100x40")!="INPUT:hello;SIZE:100x40")
        return 1;
    std::mutex mu;std::condition_variable cv;std::string log;bool exited=false;int code=-1;
    term::Launch launch;auto path=std::filesystem::absolute(argv[0]).u8string();
    launch.executable={reinterpret_cast<const char*>(path.data()),path.size()};
    auto cwd=std::filesystem::current_path().u8string();
    launch.cwd={reinterpret_cast<const char*>(cwd.data()),cwd.size()};
    launch.arguments={"--child"};launch.rows=24;launch.cols=80;
    std::string error;
    auto pty=term::Pty::start(launch,[&](std::string_view s){std::lock_guard l(mu);log.append(s);cv.notify_all();},
        [&](int n){std::lock_guard l(mu);code=n;exited=true;cv.notify_all();},error);
    if(!pty){std::cerr<<error<<'\n';return 1;}
    auto wait=[&](auto predicate){std::unique_lock l(mu);return cv.wait_for(l,std::chrono::seconds(8),predicate);};
    auto fail=[&](const char* message){pty->stop();std::lock_guard l(mu);std::cerr<<message<<": "<<log<<'\n';return 1;};
    if(!wait([&]{return visibleText(log).find("READY:80x24")!=std::string::npos;}))return fail("initial PTY dimensions");
    if(pty->cwd().empty())return fail("working directory");
    if(pty->write(std::string(term::QueueLimit+1,'x')))return fail("oversized queue accepted");
    pty->resize(40,100);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if(!pty->write("hello\r"))return fail("input rejected");
    if(!wait([&]{return exited&&visibleText(log).find("INPUT:hello;SIZE:100x40")!=std::string::npos;}))return fail("interactive input/resize/exit");
    pty->stop();
    if(code!=0)return fail("unexpected exit code");
    std::cout<<"PTY input, output, resize, cwd, exit and bounded queue passed\n";
}
