#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace term {
struct Launch {
    std::string executable; // absolute UTF-8 path
    std::vector<std::string> arguments; // does not include argv[0]
    std::string cwd;
    std::vector<std::string> environment; // KEY=value overrides
    int rows=24,cols=80;
};
using Output=std::function<void(std::string_view)>;
using Exited=std::function<void(int)>;
class Pty {
public:
    virtual ~Pty()=default;
    virtual bool write(std::string_view)=0;
    virtual void resize(int rows,int cols)=0;
    virtual std::string cwd() const=0;
    virtual void stop()=0;
    static std::unique_ptr<Pty> start(const Launch&,Output,Exited,std::string& error);
};
// CreateProcessW uses the MS CRT argument escaping convention.
inline std::wstring quoteWindows(std::wstring_view s){
    std::wstring out=L"\"";size_t slashes=0;
    for(auto c:s){
        if(c==L'\\'){++slashes;continue;}
        if(c==L'"')out.append(slashes*2+1,L'\\');
        else out.append(slashes,L'\\');
        slashes=0;out+=c;
    }
    out.append(slashes*2,L'\\');out+=L'"';return out;
}
constexpr size_t QueueLimit=1024*1024;
}
