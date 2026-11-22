#pragma once
#include <string>
#include <string_view>
#include <charconv>
#include <algorithm>
#include <cctype>

namespace dock {
// Strict bounded HTTP/1.1 decoder for the local Unix socket transport.
enum class Parse { incomplete, complete, invalid };
constexpr size_t MaxBody = 16 * 1024 * 1024;
struct Response { int status = 0; std::string body; };
inline Parse decode(std::string_view wire, Response& out, bool eof = false) {
    auto end = wire.find("\r\n\r\n");
    if (end == wire.npos) return wire.size() > 65536 || eof ? Parse::invalid : Parse::incomplete;
    if (end > 65536 || !wire.starts_with("HTTP/1.") || wire.size() < 12) return Parse::invalid;
    int status = 0;
    auto [p,ec] = std::from_chars(wire.data()+9,wire.data()+12,status);
    if (ec != std::errc{} || p != wire.data()+12) return Parse::invalid;
    std::string header(wire.substr(0,end)); 
    std::transform(header.begin(),header.end(),header.begin(),[](unsigned char c){return char(std::tolower(c));});
    auto body = wire.substr(end+4);
    out.status = status;
    if (header.find("\r\ntransfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        while (true) {
            auto e = body.find("\r\n");
            if (e == body.npos) return eof || body.size()>128 ? Parse::invalid : Parse::incomplete;
            auto hex = body.substr(0,e); hex = hex.substr(0,hex.find(';'));
            size_t n = 0;
            auto [q,err] = std::from_chars(hex.data(),hex.data()+hex.size(),n,16);
            if (hex.empty() || err != std::errc{} || q != hex.data()+hex.size() || n > MaxBody-decoded.size()) return Parse::invalid;
            body.remove_prefix(e+2);
            if (!n) {
                if (!body.starts_with("\r\n") && body.find("\r\n\r\n")==body.npos) return eof ? Parse::invalid : Parse::incomplete;
                out.body=std::move(decoded); return Parse::complete;
            }
            if (body.size()<n+2) return eof ? Parse::invalid : Parse::incomplete;
            if (body.substr(n,2)!="\r\n") return Parse::invalid;
            decoded.append(body.substr(0,n)); body.remove_prefix(n+2);
        }
    }
    auto pos = header.find("\r\ncontent-length:");
    if (pos != std::string::npos) {
        pos += 17;
        while (pos<header.size() && header[pos]==' ') ++pos;
        auto raw=std::string_view(header).substr(pos);
        raw=raw.substr(0,raw.find("\r\n"));
        size_t n=0;
        auto [q,err]=std::from_chars(raw.data(),raw.data()+raw.size(),n);
        if (raw.empty() || err!=std::errc{} || q!=raw.data()+raw.size() || n>MaxBody) return Parse::invalid;
        if (body.size()<n) return eof?Parse::invalid:Parse::incomplete;
        out.body=std::string(body.substr(0,n)); return Parse::complete;
    }
    if(body.size()>MaxBody) return Parse::invalid;
    if(!eof) return Parse::incomplete;
    out.body=std::string(body); return Parse::complete;
}
}
