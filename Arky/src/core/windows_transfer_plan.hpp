#pragma once

#include "transfer_plan.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace arky {

// Shell virtual file descriptors are not a general-purpose archive filesystem.
// They must contain valid Windows relative paths; their lindex must match the
// descriptor index, including directories (which have no FileContents stream).
struct WindowsTransferItem {
    ArchiveEntryInfo entry;
    std::string shellPath; // UTF-8, relative Windows destination path (backslashes)
};

inline bool safeWindowsName(const std::string& name) {
    if (name.empty() || name.size() > 240 || name.back() == '.' || name.back() == ' ') return false;
    for (unsigned char c : name) {
        if (c < 32 || c == 127 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') return false;
    }
    std::string stem = name.substr(0, name.find('.'));
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$") return false;
    if (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") &&
        stem.back() >= '1' && stem.back() <= '9') return false;
    return true;
}

// No file payloads are read. Directory contents are gathered from the already
// indexed archive entries, retaining empty folders and the original UTF-8 paths
// for libarchive reads while only the Shell-facing names are remapped.
inline std::vector<WindowsTransferItem> planWindowsTransfer(
    const std::vector<ArchiveEntryInfo>& selected,
    const std::vector<ArchiveEntryInfo>& index,
    std::size_t maxDescriptors = 2048) {
    auto roots = planExports(selected);
    std::vector<WindowsTransferItem> out;
    std::unordered_set<std::string> seenShell;
    auto add = [&](ArchiveEntryInfo e, std::string shellPath) {
        if (e.type != EntryType::Regular && e.type != EntryType::Directory)
            throw std::invalid_argument("Unsupported archive entry in Windows transfer");
        std::string lower = shellPath;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!seenShell.insert(lower).second)
            throw std::invalid_argument("Case-insensitive destination path collision");
        if (out.size() >= maxDescriptors)
            throw std::invalid_argument("Too many entries for Windows virtual transfer");
        out.push_back({std::move(e), std::move(shellPath)});
    };
    for (const auto& root : roots) {
        if (!safeWindowsName(root.exportedName))
            throw std::invalid_argument("Filename not supported by Windows Explorer");
        if (root.entry.type == EntryType::Regular) {
            add(root.entry, root.exportedName);
            continue;
        }
        if (root.entry.type != EntryType::Directory)
            throw std::invalid_argument("Only regular files and directories are transferable");
        add(root.entry, root.exportedName);
        const std::string prefix = root.entry.path + "/";
        std::vector<ArchiveEntryInfo> children;
        for (const auto& e : index) {
            if (e.path.size() > prefix.size() && e.path.compare(0, prefix.size(), prefix) == 0)
                children.push_back(e);
        }
        // Parents first, deterministic shell indices; never exceed the cap.
        std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
            if (a.path.size() != b.path.size()) return a.path.size() < b.path.size();
            return a.path < b.path;
        });
        for (const auto& e : children) {
            auto suffix = e.path.substr(prefix.size());
            if (suffix.empty()) continue;
            std::string shell = root.exportedName + "\\";
            std::size_t pos = 0;
            for (;;) {
                const auto end = suffix.find('/', pos);
                const auto segment = suffix.substr(pos, end == std::string::npos ? end : end - pos);
                if (!safeWindowsName(segment))
                    throw std::invalid_argument("Archive contains an unsafe Windows filename");
                shell += segment;
                if (end == std::string::npos) break;
                shell += '\\';
                pos = end + 1;
            }
            add(e, std::move(shell));
        }
    }
    return out;
}
} // namespace arky
