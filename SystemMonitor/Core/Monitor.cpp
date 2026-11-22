#include "Monitor.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace sm {
Number rate(Counter now,Counter before,double seconds){
    if(!now.valid||!before.valid||now.value<before.value||seconds<=0)return {};
    return double(now.value-before.value)/seconds;
}
Snapshot Sampler::sample(bool details){auto raw=collectNative(details);nvidiaMetrics(raw);return compare(std::move(raw),std::chrono::steady_clock::now());}
Snapshot Sampler::compare(Raw current,std::chrono::steady_clock::time_point now){
    double elapsed=std::chrono::duration<double>(now-time_).count();
    bool first=time_==std::chrono::steady_clock::time_point{};
    std::unordered_map<U,const RawProcess*> old;
    for(const auto& r:previous_.processes)old.emplace(r.p.pid,&r);
    Snapshot s;s.warning=current.warning;s.metrics=current.metrics;s.processes.reserve(current.processes.size());
    for(auto& r:current.processes){
        auto it=old.find(r.p.pid);
        if(!first&&r.p.identity&&it!=old.end()&&it->second->p.identity==r.p.identity){
            if(auto n=rate(r.cpu,it->second->cpu,elapsed))r.p.cpu=std::clamp(*n/1e7/std::max(1u,cpuCount()),0.,100.);
            r.p.read=rate(r.read,it->second->read,elapsed);r.p.write=rate(r.write,it->second->write,elapsed);
            if(r.p.path.empty())r.p.path=it->second->p.path;
            if(r.p.command.empty())r.p.command=it->second->p.command;
            if(r.p.user.empty())r.p.user=it->second->p.user;
        }
        s.processes.push_back(r.p);
    }
    for(auto& m:s.metrics){
        auto it=current.counters.find(m.name);
        auto oldIt=previous_.counters.find(m.name);
        if(it!=current.counters.end()){
            m.value=first||oldIt==previous_.counters.end()?Number{}:rate(it->second,oldIt->second,elapsed);
        }
        // Cumulative CPU busy and total tick counters are sampled together.
        auto b=current.counters.find(m.name+"/busy"),t=current.counters.find(m.name+"/total");
        auto ob=previous_.counters.find(m.name+"/busy"),ot=previous_.counters.find(m.name+"/total");
        if(!first&&b!=current.counters.end()&&t!=current.counters.end()&&ob!=previous_.counters.end()&&ot!=previous_.counters.end()){
            auto busy=rate(b->second,ob->second,elapsed),total=rate(t->second,ot->second,elapsed);
            if(busy&&total&&*total>0)m.value=std::clamp(100**busy / *total,0.,100.);
        }
    }
    previous_=std::move(current);time_=now;
    return s;
}
Result operate(const Request& r){
    if(r.pid<=1||r.pid==ownPid()||!r.identity)return {false,"Protected process or unavailable process identity."};
    auto id=processIdentity(r.pid);
    if(!id||*id!=r.identity)return {false,"Process exited, changed identity, or is inaccessible."};
    return operateNative(r);
}
std::vector<Request> processTree(const Snapshot& s,const Request& root){
    std::unordered_map<U,std::vector<const Process*>> children;
    const Process* found=nullptr;
    for(auto& p:s.processes){children[p.parent].push_back(&p);if(p.pid==root.pid&&p.identity==root.identity)found=&p;}
    if(!found||!root.identity)return {};
    std::vector<Request> result;std::vector<std::pair<const Process*,bool>> stack{{found,false}};
    std::unordered_set<U> seen;
    while(!stack.empty()){
        auto [p,visited]=stack.back();stack.pop_back();
        if(visited){auto r=root;r.pid=p->pid;r.identity=p->identity;result.push_back(r);continue;}
        if(!p->identity||!seen.insert(p->pid).second)continue;
        stack.push_back({p,true});
        for(auto* c:children[p->pid])
            if(c->identity>=p->identity)stack.push_back({c,false});
    }
    return result;
}
}
