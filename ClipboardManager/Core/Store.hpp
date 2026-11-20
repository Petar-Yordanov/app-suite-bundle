#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace clip {
namespace fs = std::filesystem;
constexpr std::uint64_t MiB = 1024ull * 1024;
constexpr std::uint64_t MaxPayload = 32 * MiB;
constexpr std::size_t MaxFormats = 128, MaxNodes = 10000;

struct Format { std::string type, bytes; };
struct Capture {
    std::vector<Format> formats;
    std::vector<fs::path> files;
    std::string preview, kind;
};
struct Limits {
    std::size_t entries = 100;
    std::uint64_t bytes = 512 * MiB;
};
struct Entry {
    std::string id, preview, kind, digest;
    std::int64_t timestamp = 0;
    std::uint64_t bytes = 0, charge = 0;
    std::vector<std::string> formats, roots;
};
struct Restored { std::vector<Format> formats; std::vector<fs::path> files; };
struct Result { bool ok = false; std::string message, id; };

std::string utf8(const fs::path&);
fs::path path(const std::string&);
bool sensitiveMarker(std::string name);
bool passwordManager(std::string name);

// Single-owner store. Run on one worker thread; never share an instance across threads.
// Each entry is independently committed using a directory rename.
class Store {
public:
    explicit Store(fs::path root, Limits limits = {});
    const std::vector<Entry>& entries() const { return entries_; }
    std::uint64_t used() const;
    Limits limits() const { return limits_; }
    Result put(Capture capture);
    std::optional<Restored> restore(const std::string& id) const;
    std::vector<Entry> search(const std::string& query, std::size_t limit = 10000) const;
    bool remove(const std::string& id);
    bool clear();
    bool setLimits(Limits limits);
    void protect(std::string id) { lease_ = std::move(id); }
    const std::string& protectedId() const { return lease_; }
private:
    fs::path root_;
    Limits limits_;
    std::vector<Entry> entries_;
    std::string lease_;
    bool makeRoom(std::uint64_t charge, std::size_t count);
    void load();
};
}
