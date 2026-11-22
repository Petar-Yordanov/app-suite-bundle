#include "Rules.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
namespace web {
std::string host(std::string_view v){
    if(v.empty()||v.size()>254)return {};
    std::string s;for(unsigned char c:v){if(c>=128||!(std::isalnum(c)||c=='.'||c=='-'))return {};s+=char(std::tolower(c));}
    if(s.back()=='.')s.pop_back();
    if(s.empty()||s.find('.')==std::string::npos||s.front()=='.'||s.back()=='.'||s.find("..")!=std::string::npos)return {};
    bool alpha=false;for(auto c:s)if(c>='a'&&c<='z')alpha=true;
    if(!alpha)return {};
    return s;
}
bool domainMatch(std::string_view h,std::string_view d){
    return h==d||(h.size()>d.size()&&h.ends_with(d)&&h[h.size()-d.size()-1]=='.');
}
std::vector<std::string> parseHosts(std::string_view input){
    std::vector<std::string> result;std::unordered_set<std::string> seen;
    if(input.size()>2*1024*1024)input=input.substr(0,2*1024*1024);
    std::string copy(input);std::replace(copy.begin(),copy.end(),',','\n');
    std::istringstream lines(copy);std::string line;
    while(std::getline(lines,line)&&result.size()<25000){
        if(auto hash=line.find('#');hash!=std::string::npos)line.resize(hash);
        std::istringstream tokens(line);std::string token;
        while(tokens>>token&&result.size()<25000){
            auto h=host(token);if(!h.empty()&&h!="localhost.localdomain"&&seen.insert(h).second)result.push_back(std::move(h));
        }
    }return result;
}
static bool contains(const std::unordered_set<std::string>& domains,std::string h){
    if(h.empty())return false;
    for(;;){if(domains.contains(h))return true;auto i=h.find('.');if(i==std::string::npos)return false;h.erase(0,i+1);}
}
bool Rules::blocks(std::string_view req,std::string_view first,bool main)const{
    if(!enabled||main)return false;
    auto h=host(req),f=host(first);
    if(h.empty()||f.empty()||contains(exceptions,f)||domainMatch(h,f)||domainMatch(f,h))return false;
    return contains(blocked,h);
}
Rules Rules::make(bool enabled,std::string_view extra,std::string_view allow){
    Rules r;r.enabled=enabled;
    static constexpr std::string_view defaults="doubleclick.net\ngooglesyndication.com\ngoogleadservices.com\nadnxs.com\nadform.net\ncriteo.com\ncriteo.net\ntaboola.com\noutbrain.com\nscorecardresearch.com\nadsrvr.org\npubmatic.com\nrubiconproject.com\nopenx.net\nsmartadserver.com\nindexww.com\n";
    for(auto& h:parseHosts(defaults))r.blocked.insert(h);
    for(auto& h:parseHosts(extra))r.blocked.insert(h);
    for(auto& h:parseHosts(allow))r.exceptions.insert(h);
    return r;
}
std::string safeFilename(std::string_view input){
    std::string s;auto pos=input.find_last_of("/\\");if(pos!=std::string_view::npos)input.remove_prefix(pos+1);
    // The Unicode-aware UI also validates via QFileInfo. Keep this byte-level
    // function for ASCII control/path characters; UTF-8 bytes are retained.
    for(unsigned char c:input){if(c<32||c==127||c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')s+='_';else s+=char(c);}
    while(!s.empty()&&(s.back()=='.'||s.back()==' '))s.pop_back();
    if(s.empty()||s=="."||s=="..")return "download";
    std::string stem=s.substr(0,s.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(std::toupper(c));});
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='1'&&stem[3]<='9'))s="_"+s;
    return s;
}
}
