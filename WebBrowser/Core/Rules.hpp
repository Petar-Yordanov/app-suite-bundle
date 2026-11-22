#pragma once
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace web {
std::string host(std::string_view);
bool domainMatch(std::string_view host,std::string_view domain);
std::vector<std::string> parseHosts(std::string_view input);
struct Rules {
    bool enabled=true;
    std::unordered_set<std::string> blocked,exceptions;
    // Literal URL path prefixes indexed by hostname. No regex evaluation on
    // Chromium's request thread, and no unsupported filter options guessed.
    std::unordered_map<std::string,std::vector<std::string>> paths;
    bool blocks(std::string_view request,std::string_view firstParty,bool mainFrame=false,std::string_view path={})const;
    static Rules make(bool enabled,std::string_view extra,std::string_view exceptions);
};
std::string safeFilename(std::string_view input);
}
