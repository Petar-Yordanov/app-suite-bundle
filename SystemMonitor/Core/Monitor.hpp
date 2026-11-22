#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace sm {
using U=std::uint64_t;
using Number=std::optional<double>;
struct Metric {std::string name; Number value; std::string unit, detail;};
struct Process {
    U pid=0, parent=0, identity=0;
    std::string name,user,path,command,state;
    Number cpu,memory,virtualMemory,read,write,upload,download,uptime,threads;
    bool operator==(const Process&)const=default;
};
struct Snapshot {std::vector<Metric> metrics;std::vector<Process> processes;std::string warning;bool valid=true;};
struct Counter {U value=0;bool valid=false;};
struct RawProcess {Process p;Counter cpu,read,write;}; // CPU in nanoseconds
struct Raw {std::vector<Metric> metrics;std::vector<RawProcess> processes;std::unordered_map<std::string,Counter> counters;std::string warning;};
enum class Action {Terminate,Force,Suspend,Resume,Priority,Affinity};
struct Request {U pid=0,identity=0;Action action=Action::Terminate;int priority=0;std::vector<int> cores;};
struct Result {bool ok=false;std::string message;bool permission=false;};
Raw collectNative(bool details);
Result operateNative(const Request&);
bool protectPriority();
U ownPid();
std::optional<U> processIdentity(U pid);
unsigned cpuCount();
void nvidiaMetrics(Raw&);
class Sampler {
    Raw previous_;
    std::chrono::steady_clock::time_point time_{};
public:
    Snapshot sample(bool details=true);
    Snapshot compare(Raw current,std::chrono::steady_clock::time_point now);
};
Number rate(Counter now,Counter before,double seconds);
std::vector<Request> processTree(const Snapshot&,const Request&);
Result operate(const Request&);
}
