#pragma once

#include "archive_engine.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace arky {
struct ExportItem {
    ArchiveEntryInfo entry;
    std::string exportedName; // Unique, flat name of actual file/directory handed to the shell.
};

// Build top-level roots and stable collision-free destination names. A selected
// folder subsumes any selected children. No file content is buffered here.
inline std::vector<ExportItem> planExports(std::vector<ArchiveEntryInfo> entries) {
    auto clean = [](std::string p) {
        std::replace(p.begin(), p.end(), '\\', '/');
        while (!p.empty() && p.back() == '/') p.pop_back();
        if (p.empty() || p[0] == '/' || p.find('\0') != std::string::npos || p.find(':') != std::string::npos)
            throw std::invalid_argument("Invalid export path");
        for (std::size_t begin = 0; begin <= p.size();) {
            auto end = p.find('/', begin);
            if (end == std::string::npos) end = p.size();
            const auto part = p.substr(begin, end - begin);
            if (part.empty() || part == "." || part == "..") throw std::invalid_argument("Unsafe export path");
            if (end == p.size()) break;
            begin = end + 1;
        }
        return p;
    };
    for (auto& e : entries) e.path = clean(e.path);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.path.size() != b.path.size()) return a.path.size() < b.path.size();
        return a.path < b.path;
    });
    std::vector<ExportItem> roots;
    std::unordered_set<std::string> seen;
    for (const auto& entry : entries) {
        if (!seen.insert(entry.path).second) continue;
        bool underSelectedDirectory = false;
        for (const auto& root : roots) {
            if (root.entry.type == EntryType::Directory && entry.path.size() > root.entry.path.size()
                && entry.path.compare(0, root.entry.path.size(), root.entry.path) == 0
                && entry.path[root.entry.path.size()] == '/') {
                underSelectedDirectory = true;
                break;
            }
        }
        if (underSelectedDirectory) continue;
        roots.push_back({entry, {}});
    }
    std::unordered_set<std::string> reserved;
    auto nameKey = [](std::string name) {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return name;
    };
    for (auto& root : roots) {
        const auto pos = root.entry.path.find_last_of('/');
        const auto basename = root.entry.path.substr(pos == std::string::npos ? 0 : pos + 1);
        auto ext = root.entry.type == EntryType::Regular ? basename.find_last_of('.') : std::string::npos;
        if (ext == 0) ext = std::string::npos;
        const std::string stem = ext == std::string::npos ? basename : basename.substr(0, ext);
        const std::string extension = ext == std::string::npos ? "" : basename.substr(ext);
        std::string candidate = basename;
        for (int i = 2; !reserved.insert(nameKey(candidate)).second; ++i) {
            if (i > 100000) throw std::invalid_argument("Too many colliding names");
            candidate = stem + " (" + std::to_string(i) + ")" + extension;
        }
        root.exportedName = candidate;
    }
    return roots;
}
} // namespace arky
