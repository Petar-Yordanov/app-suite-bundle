#include "archive_engine.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <limits>
#include <iterator>
#include <memory>
#include <utility>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <unordered_map>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace arky {
namespace {
constexpr std::size_t BufferSize = 64 * 1024;
constexpr std::uint64_t MaxExtractBytes = 256ULL * 1024 * 1024 * 1024; // 256 GiB safety cap
constexpr std::size_t MaxEntries = 2'000'000;
constexpr std::size_t MaxPathBytes = 4096;

struct ArchiveReader {
    archive* handle = archive_read_new();
    ArchiveReader() {
        if (!handle) throw std::runtime_error("Cannot allocate archive reader");
        archive_read_support_filter_all(handle);
        archive_read_support_format_all(handle);
        archive_read_support_format_raw(handle); // standalone .gz/.xz etc.
    }
    ~ArchiveReader() { if (handle) archive_read_free(handle); }
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;
};
struct ArchiveWriter {
    archive* handle = archive_write_new();
    ArchiveWriter() { if (!handle) throw std::runtime_error("Cannot allocate archive writer"); }
    ~ArchiveWriter() { if (handle) archive_write_free(handle); }
    ArchiveWriter(const ArchiveWriter&) = delete;
    ArchiveWriter& operator=(const ArchiveWriter&) = delete;
};
struct EntryHolder {
    archive_entry* handle = archive_entry_new();
    EntryHolder() { if (!handle) throw std::runtime_error("Cannot allocate archive entry"); }
    ~EntryHolder() { if (handle) archive_entry_free(handle); }
    EntryHolder(const EntryHolder&) = delete;
    EntryHolder& operator=(const EntryHolder&) = delete;
};

using ZipHandle = std::unique_ptr<zip_t, decltype(&zip_discard)>;
ZipHandle openZip(const fs::path& path, int flags) {
    zip_error_t error;
    zip_error_init(&error);
#ifdef _WIN32
    auto* source = zip_source_win32w_create(path.c_str(), 0, -1, &error);
#else
    auto* source = zip_source_file_create(path.c_str(), 0, -1, &error);
#endif
    zip_t* zip = source ? zip_open_from_source(source, flags, &error) : nullptr;
    if (!zip) {
        if (source) zip_source_free(source);
        std::string reason = zip_error_strerror(&error);
        zip_error_fini(&error);
        throw std::runtime_error("Open ZIP: " + reason);
    }
    zip_error_fini(&error);
    return ZipHandle(zip, &zip_discard);
}

std::string archiveError(archive* a, const char* operation) {
    const char* reason = archive_error_string(a);
    return std::string(operation) + ": " + (reason ? reason : "unknown libarchive error");
}
void requireSuccess(int code, archive* a, const char* operation) {
    if (code < ARCHIVE_WARN) throw std::runtime_error(archiveError(a, operation));
    if (code == ARCHIVE_WARN) throw std::runtime_error(archiveError(a, operation));
}
std::string utf8(const fs::path& path) {
    auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
fs::path fromUtf8(const std::string& path) {
    auto* bytes = reinterpret_cast<const char8_t*>(path.data());
    return fs::path(std::u8string(bytes, bytes + path.size()));
}
void checkCancelled(const Callbacks& cb) {
    if (cb.cancelled && cb.cancelled()) throw std::runtime_error("Operation cancelled");
}
std::vector<fs::path> volumesFor(const fs::path& path, const Callbacks& cb) {
    if (!cb.volumes.empty()) return cb.volumes;
    const auto name = utf8(path.filename());
    auto lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::vector<fs::path> volumes;
    auto add = [&](const std::string& next) {
        const auto item = path.parent_path() / fromUtf8(next);
        if (!fs::is_regular_file(item)) return false;
        volumes.push_back(item); return true;
    };
    if (lowered.ends_with(".001")) {
        volumes.push_back(path);
        const auto prefix = name.substr(0, name.size()-3);
        for (unsigned n = 2; n <= 999; ++n) {
            auto digits = std::to_string(n);
            if (!add(prefix + std::string(3-digits.size(), '0') + digits)) break;
        }
    } else if (lowered.ends_with(".rar")) {
        const auto part = lowered.rfind(".part");
        if (part != std::string::npos) {
            const auto digits = name.substr(part+5, name.size()-part-9);
            if (!digits.empty() && digits.size() <= 6 && digits.back() == '1' &&
                std::all_of(digits.begin(), digits.end()-1, [](char c){ return c == '0'; })) {
                volumes.push_back(path);
                for (unsigned n = 2; n <= 999; ++n) {
                    auto next = std::to_string(n);
                    if (!add(name.substr(0, part+5) + std::string(digits.size()>next.size()?digits.size()-next.size():0, '0') + next + name.substr(name.size()-4))) break;
                }
            }
        } else {
            volumes.push_back(path);
            const auto prefix = name.substr(0, name.size()-4);
            for (unsigned n=0;n<100;++n) {
                auto digits = std::to_string(n);
                if (!add(prefix + ".r" + std::string(2-digits.size(), '0') + digits)) break;
            }
        }
    }
    if (volumes.size() <= 1) volumes.clear();
    return volumes;
}
void openReader(archive* a, const fs::path& path, const Callbacks& cb) {
    if (!cb.passphrase.empty()) requireSuccess(archive_read_add_passphrase(a, cb.passphrase.c_str()), a, "Set passphrase");
    auto volumes = volumesFor(path, cb);
    if (!volumes.empty()) {
        std::vector<std::string> names;
        std::vector<const char*> pointers;
        for (const auto& part : volumes) {
            if (!fs::is_regular_file(part)) throw std::runtime_error("Archive volume missing");
            names.push_back(utf8(part));
#ifdef _WIN32
            if (std::any_of(names.back().begin(), names.back().end(), [](unsigned char c){return c >= 128;}))
                throw std::runtime_error("Multi-volume filenames must be ASCII with this Windows libarchive API");
#endif
        }
        for (const auto& part : names) pointers.push_back(part.c_str());
        pointers.push_back(nullptr);
        requireSuccess(archive_read_open_filenames(a, pointers.data(), BufferSize), a, "Open archive volumes");
        return;
    }
#ifdef _WIN32
    requireSuccess(archive_read_open_filename_w(a, path.c_str(), BufferSize), a, "Open archive");
#else
    requireSuccess(archive_read_open_filename(a, path.c_str(), BufferSize), a, "Open archive");
#endif
}
void openWriter(archive* a, const fs::path& path) {
#ifdef _WIN32
    requireSuccess(archive_write_open_filename_w(a, path.c_str()), a, "Create archive");
#else
    requireSuccess(archive_write_open_filename(a, path.c_str()), a, "Create archive");
#endif
}
std::string entryPath(archive_entry* entry) {
#ifdef _WIN32
    if (const wchar_t* wide = archive_entry_pathname_w(entry)) return utf8(fs::path(wide));
#endif
    if (const char* s = archive_entry_pathname_utf8(entry)) return s;
    if (const char* s = archive_entry_pathname(entry)) return s;
    return {};
}
EntryType entryType(archive_entry* entry) {
    switch (archive_entry_filetype(entry)) {
        case AE_IFREG: return EntryType::Regular;
        case AE_IFDIR: return EntryType::Directory;
        case AE_IFLNK: return EntryType::Link;
        default: return EntryType::Other;
    }
}
ArchiveEntryInfo metadata(archive_entry* entry) {
    ArchiveEntryInfo info;
    info.path = entryPath(entry);
    info.type = entryType(entry);
    if (archive_entry_size_is_set(entry)) {
        auto n = archive_entry_size(entry);
        if (n >= 0) { info.size = static_cast<std::uint64_t>(n); info.sizeKnown = true; }
    }
    if (archive_entry_mtime_is_set(entry)) info.modified = archive_entry_mtime(entry);
    info.permissions = archive_entry_perm(entry) & 0777;
    info.permissionsKnown = archive_entry_mode(entry) != 0;
    return info;
}
// Normalization and conservative filename restrictions are deliberate:
// no absolute paths, parent components, ADS, DOS devices, or Windows-invalid components.
std::string safeRelative(std::string path) {
    if (path.empty()) throw std::runtime_error("Archive contains an empty path");
    if (path.size() > MaxPathBytes) throw std::runtime_error("Archive entry path exceeds 4096 bytes");
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.front() == '/' || path.find('\0') != std::string::npos)
        throw std::runtime_error("Archive contains an absolute or invalid path");
    std::string output;
    std::size_t pos = 0;
    while (pos < path.size()) {
        auto end = path.find('/', pos);
        if (end == std::string::npos) end = path.size();
        auto part = path.substr(pos, end - pos);
        pos = end + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") throw std::runtime_error("Blocked archive path traversal: " + path);
        if (part.size() > 255) throw std::runtime_error("Archive path component exceeds 255 bytes");
        if (part.back() == '.' || part.back() == ' ' ||
            part.find_first_of(":*?\"<>|") != std::string::npos)
            throw std::runtime_error("Blocked unsafe filename: " + path);
        for (unsigned char c : part) if (c < 32) throw std::runtime_error("Blocked control character in filename");
        std::string base = part.substr(0, part.find('.'));
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
            (base.size() == 4 && (base.substr(0, 3) == "COM" || base.substr(0, 3) == "LPT") &&
             base[3] >= '1' && base[3] <= '9'))
            throw std::runtime_error("Blocked reserved device filename: " + path);
        if (!output.empty()) output.push_back('/');
        output += part;
    }
    if (output.empty()) throw std::runtime_error("Invalid empty archive path");
    return output;
}
void validateLimits(const ReadLimits& l) {
    if (!l.maxTotalBytes || l.maxTotalBytes > MaxExtractBytes ||
        !l.maxEntryBytes || l.maxEntryBytes > MaxExtractBytes ||
        !l.maxEntries || l.maxEntries > MaxEntries)
        throw std::runtime_error("Read limits out of range (up to 256 GiB and 2M entries)");
}
void checkDecodedBudget(const ReadLimits& limits, std::uint64_t total,
                        std::uint64_t entryBytes, std::uint64_t next,
                        archive* a) {
    if (next > limits.maxTotalBytes - total)
        throw std::runtime_error("Total decompressed size limit exceeded");
    if (next > limits.maxEntryBytes - entryBytes)
        throw std::runtime_error("Single-entry decompressed size limit exceeded");
    if (limits.maxExpansionRatio && total + next >= limits.ratioCheckAfterBytes) {
        const auto used = archive_filter_bytes(a, -1);
        if (used > 0 && (total + next) / static_cast<std::uint64_t>(used) > limits.maxExpansionRatio)
            throw std::runtime_error("Suspicious archive expansion ratio exceeded configured limit");
    }
}
void setSourceMetadata(archive_entry* entry, const fs::path& source) {
    const auto st = fs::status(source);
    const auto bits = static_cast<unsigned>(st.permissions()) & 0777U;
    archive_entry_set_perm(entry, static_cast<int>(bits ? bits :
                           (fs::is_directory(st) ? 0755 : 0644)));
    const auto stamp = fs::last_write_time(source);
    const auto sysTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        stamp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    archive_entry_set_mtime(entry, std::chrono::system_clock::to_time_t(sysTime), 0);
}
void restoreFileMetadata(const fs::path& path, archive_entry* entry, Result& result) {
    std::error_code ec;
    // Never propagate dangerous setuid/setgid/sticky flags from untrusted archives.
    const auto perms = static_cast<fs::perms>(archive_entry_perm(entry) & 0777);
    if (perms != fs::perms::none) {
        fs::permissions(path, perms, fs::perm_options::replace, ec);
        if (ec) result.warnings.push_back("Could not restore permissions: " + utf8(path));
    }
    if (archive_entry_mtime_is_set(entry)) {
        const auto sys = std::chrono::system_clock::from_time_t(archive_entry_mtime(entry));
        const auto fileTime = std::chrono::time_point_cast<fs::file_time_type::duration>(
            sys - std::chrono::system_clock::now() + fs::file_time_type::clock::now());
        ec.clear();
        fs::last_write_time(path, fileTime, ec);
        if (ec) result.warnings.push_back("Could not restore modification time: " + utf8(path));
    }
}
bool selected(const std::string& path, const std::vector<std::string>& selection) {
    if (selection.empty()) return true;
    for (auto name : selection) {
        while (!name.empty() && name.back() == '/') name.pop_back();
        if (path == name || (path.size() > name.size() &&
            path.compare(0, name.size(), name) == 0 && path[name.size()] == '/')) return true;
    }
    return false;
}
// Every destination path component is examined; pre-existing symlinks are refused.
// This is not a guarantee against a hostile concurrent process replacing directories.
#ifndef _WIN32
class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) : fd_(fd) {}
    ~UniqueFd() { if (fd_ >= 0) ::close(fd_); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) { if (fd_ >= 0) ::close(fd_); fd_ = std::exchange(other.fd_, -1); }
        return *this;
    }
    int get() const { return fd_; }
private:
    int fd_;
};
UniqueFd openDirectoryChain(const fs::path& root, const fs::path& relative,
                            bool create, std::vector<fs::path>* created) {
    UniqueFd dir(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (dir.get() < 0) throw std::runtime_error("Cannot open trusted extraction root");
    auto current = root;
    for (const auto& part : relative) {
        if (part == "." || part.empty()) continue;
        if (part == "..") throw std::runtime_error("Invalid output directory traversal");
        current /= part;
        bool added = false;
        if (create) {
            if (::mkdirat(dir.get(), part.c_str(), 0700) == 0) added = true;
            else if (errno != EEXIST) throw std::runtime_error("Cannot create extraction directory: " + utf8(current));
        }
        UniqueFd next(::openat(dir.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) throw std::runtime_error("Unsafe, missing or replaced extraction directory: " + utf8(current));
        dir = std::move(next);
        if (added && created) created->push_back(current);
    }
    return dir;
}
#endif
// Linux/macOS directory traversal is anchored to a directory descriptor and
// refuses symlinks even if an attacker swaps components between system calls.
// Windows remains on its existing best-effort path checks (documented).
void ensureDirectories(const fs::path& root, const fs::path& relativeParent,
                       std::vector<fs::path>* created = nullptr) {
#ifndef _WIN32
    (void)openDirectoryChain(root, relativeParent, true, created);
#else
    auto current = root;
    for (const auto& component : relativeParent) {
        current /= component;
        auto attrs = GetFileAttributesW(current.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Refusing reparse-point directory: " + utf8(current));
        std::error_code ec;
        auto st = fs::symlink_status(current, ec);
        if (!ec && fs::is_symlink(st)) throw std::runtime_error("Refusing symlink directory: " + utf8(current));
        if (!ec && fs::exists(st)) {
            if (!fs::is_directory(st)) throw std::runtime_error("Destination is not a directory: " + utf8(current));
        } else {
            if (!fs::create_directory(current, ec)) {
                if (ec) throw fs::filesystem_error("Cannot create destination directory", current, ec);
                if (!fs::is_directory(fs::symlink_status(current)))
                    throw std::runtime_error("Destination was replaced during extraction");
            } else if (created) created->push_back(current);
        }
    }
#endif
}
void prepareRoot(const fs::path& root) {
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(root, ec)))
        throw std::runtime_error("Destination root must not be a symlink");
    fs::create_directories(root);
    if (!fs::is_directory(root)) throw std::runtime_error("Invalid destination directory");
#ifdef _WIN32
    auto attrs = GetFileAttributesW(root.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Invalid or reparse-point extraction root");
#endif
}

// Creates a destination file exclusively, so pre-existing files aren't overwritten.
// Removes any partially written file unless finish() is called.
class ExclusiveOutput {
public:
    ExclusiveOutput(fs::path path, const fs::path& root, const fs::path& relative)
        : path_(std::move(path)) {
#ifdef _WIN32
        handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create (existing/locked?) file: " + utf8(path_));
#else
        parentFd_ = openDirectoryChain(root, relative.parent_path(), false, nullptr);
        leaf_ = relative.filename().string();
        fd_ = ::openat(parentFd_.get(), leaf_.c_str(),
                       O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd_ < 0) throw std::runtime_error("Cannot securely create output file: " + utf8(path_));
#endif
    }
    ~ExclusiveOutput() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) ::close(fd_);
#endif
        if (!finished_) {
#ifdef _WIN32
            std::error_code ec; fs::remove(path_, ec);
#else
            if (parentFd_.get() >= 0) ::unlinkat(parentFd_.get(), leaf_.c_str(), 0);
#endif
        }
    }
    void write(const char* bytes, std::size_t n) {
        while (n) {
#ifdef _WIN32
            DWORD part = static_cast<DWORD>((std::min<std::size_t>)(n, 1UL << 20));
            DWORD wrote = 0;
            if (!WriteFile(handle_, bytes, part, &wrote, nullptr) || wrote == 0)
                throw std::runtime_error("Disk write failed");
#else
            auto wrote = ::write(fd_, bytes, n);
            if (wrote <= 0) {
                if (wrote < 0 && errno == EINTR) continue;
                throw std::runtime_error("Disk write failed");
            }
#endif
            bytes += wrote;
            n -= static_cast<std::size_t>(wrote);
        }
    }
    void finish() {
#ifdef _WIN32
        if (!FlushFileBuffers(handle_)) throw std::runtime_error("Cannot flush output file");
#else
        if (::fsync(fd_) != 0) throw std::runtime_error("Cannot flush output file");
#endif
        finished_ = true;
    }
private:
    fs::path path_;
    bool finished_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
    UniqueFd parentFd_;
    std::string leaf_;
#endif
};

struct InputFile { fs::path actual; std::string relative; bool directory; std::uint64_t size; };
void gather(const fs::path& source, const std::string& relative,
            std::vector<InputFile>& result, std::unordered_set<std::string>& seen,
            const Callbacks& cb) {
    checkCancelled(cb);
    if (fs::is_symlink(fs::symlink_status(source)))
        throw std::runtime_error("Source symlinks are intentionally unsupported: " + utf8(source));
    const bool dir = fs::is_directory(source);
    if (!dir && !fs::is_regular_file(source))
        throw std::runtime_error("Not a regular file or directory: " + utf8(source));
    // Ensure our own output will always be safely extractable on Windows too.
    auto clean = safeRelative(relative);
    std::string key = clean;
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    if (!seen.insert(key).second) throw std::runtime_error("Duplicate archive entry: " + clean);
    result.push_back({source, clean, dir, dir ? 0 : static_cast<std::uint64_t>(fs::file_size(source))});
    if (result.size() > MaxEntries) throw std::runtime_error("Too many input files");
    if (dir) {
        for (const auto& child : fs::directory_iterator(source))
            gather(child.path(), clean + "/" + utf8(child.path().filename()), result, seen, cb);
    }
}
// One source of truth for creation format and option validation. Avoid
// claiming that libarchive can create RAR or encrypted 7z volumes.
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
FormatCapabilities describeFormat(const std::string& filename) {
    auto name = lower(filename);
    // Long suffixes before short suffixes. .tgz / .txz / .tbz2 are aliases.
    const std::vector<std::string> suffixes = {".tar.gz", ".tar.xz", ".tar.zst", ".tgz", ".txz", ".zip", ".7z", ".tar"};
    for (const auto& ext : suffixes) {
        if (!name.ends_with(ext)) continue;
        FormatCapabilities c;
        c.extension = ext;
        c.writable = true;
        c.editable = ext == ".zip";
        c.multipleFiles = true;
        c.supportsCompressionLevel = (ext == ".zip" || ext == ".7z" ||
            ext.ends_with(".gz") || ext == ".tgz" || ext.ends_with(".bz2") ||
            ext == ".tbz2" || ext.ends_with(".xz") || ext == ".txz" ||
            ext.ends_with(".zst"));
        c.methods = {"auto"};
        if (ext == ".zip") {
            c.methods = {"auto", "store", "deflate", "bzip2", "lzma", "xz", "zstd"};
        } else if (ext == ".7z") {
            c.methods = {"auto", "store", "deflate", "bzip2", "lzma1", "lzma2", "zstd"};
        }
        // Compile-time libarchive capabilities vary (even across minor
        // versions). Probe the actual writer instead of exposing methods that
        // fail at the first byte. No output file is created by this probe.
        if (ext == ".zip" || ext == ".7z") {
            std::vector<std::string> supported{"auto"};
            for (const auto& method : c.methods) {
                if (method == "auto") continue;
                ArchiveWriter probe;
                const auto formatSet = ext == ".zip"
                    ? archive_write_set_format_zip(probe.handle)
                    : archive_write_set_format_7zip(probe.handle);
                const auto accepted = formatSet == ARCHIVE_OK &&
                    archive_write_set_format_option(probe.handle,
                        ext == ".zip" ? "zip" : "7zip", "compression", method.c_str()) == ARCHIVE_OK;
                if (accepted) supported.push_back(method);
            }
            c.methods = std::move(supported);
        }
        return c;
    }
    return {};
}
void validateCreateOptions(const FormatCapabilities& c, const CreateOptions& o,
                           const std::string& passphrase) {
    if (!passphrase.empty()) throw std::runtime_error("Encrypted archive creation is disabled");
    if (!c.writable) throw std::runtime_error("Unsupported output extension");
    if (o.compressionLevel < -1 || o.compressionLevel > 9)
        throw std::runtime_error("Compression level must be Default or 0..9");
    const auto method = lower(o.compressionMethod);
    if (std::find(c.methods.begin(), c.methods.end(), method) == c.methods.end())
        throw std::runtime_error("Compression method '" + method + "' is unsupported for " + c.extension);
    if (!c.supportsCompressionLevel && o.compressionLevel >= 0)
        throw std::runtime_error("Explicit compression levels are unsupported for " + c.extension);
    if (method == "store" && o.compressionLevel > 0)
        throw std::runtime_error("Store mode cannot use a nonzero compression level");
    if (method == "bzip2" && o.compressionLevel == 0)
        throw std::runtime_error("BZip2 requires a compression level of at least 1");

}
void configureWriter(archive* a, const std::string& filename, const CreateOptions& options = {}) {
    auto ext = describeFormat(filename).extension;
    int code = ARCHIVE_FAILED;
    if (ext == ".zip") code = archive_write_set_format_zip(a);
    else if (ext == ".7z") code = archive_write_set_format_7zip(a);
    else if (!ext.empty()) code = archive_write_set_format_pax_restricted(a);
    requireSuccess(code, a, "Set archive format");
    if (ext == ".gz" || ext == ".tar.gz" || ext == ".tgz") code = archive_write_add_filter_gzip(a);
    else if (ext == ".bz2" || ext == ".tar.bz2" || ext == ".tbz2") code = archive_write_add_filter_bzip2(a);
    else if (ext == ".xz" || ext == ".tar.xz" || ext == ".txz") code = archive_write_add_filter_xz(a);
    else if (ext == ".zst" || ext == ".tar.zst") code = archive_write_add_filter_zstd(a);
    else code = archive_write_add_filter_none(a);
    requireSuccess(code, a, "Set archive filter");
    if (options.threads < 1 || options.threads > 16)
        throw std::runtime_error("Compression threads must be 1..16");
    if (options.threads > 1 && (ext == ".tar.xz" || ext == ".txz" || ext == ".tar.zst")) {
        const auto threads = std::to_string(options.threads);
        requireSuccess(archive_write_set_filter_option(a, ext == ".tar.zst" ? "zstd" : "xz", "threads", threads.c_str()), a, "Set compression threads");
    }
    auto method = lower(options.compressionMethod);
    if (method != "auto") {
        const char* module = ext == ".zip" ? "zip" : "7zip";
        requireSuccess(archive_write_set_format_option(a, module, "compression", method.c_str()),
                       a, "Set compression method");
    }
    if (options.compressionLevel >= 0) {
        auto level = std::to_string(options.compressionLevel);
        if (ext == ".zip" || ext == ".7z") {
            if (options.compressionLevel == 0 && method == "auto") {
                const char* module = ext == ".zip" ? "zip" : "7zip";
                requireSuccess(archive_write_set_format_option(a, module, "compression", "store"),
                               a, "Set store-only compression");
            } else if (method != "store") {
                const char* module = ext == ".zip" ? "zip" : "7zip";
                requireSuccess(archive_write_set_format_option(a, module, "compression-level", level.c_str()),
                               a, "Set container compression level");
            }
        } else {
            const char* filter = nullptr;
            if (ext.ends_with(".gz") || ext == ".tgz") filter = "gzip";
            else if (ext.ends_with(".bz2") || ext == ".tbz2") filter = "bzip2";
            else if (ext.ends_with(".xz") || ext == ".txz") filter = "xz";
            else if (ext.ends_with(".zst")) filter = "zstd";
            if (!filter) throw std::runtime_error("Compression filter has no level control");
            requireSuccess(archive_write_set_filter_option(a, filter, "compression-level", level.c_str()),
                           a, "Set filter compression level");
        }
    }
}
} // namespace

Result ArchiveEngine::inspectBatched(const fs::path& archivePath, EntryBatchSink sink,
                                     const Callbacks& cb, std::size_t batchSize) {
    Result result;
    try {
        if (!sink) throw std::runtime_error("Archive listing callback is required");
        if (!batchSize || batchSize > 16'384)
            throw std::runtime_error("Archive listing batch size must be 1..16384");
        ArchiveReader reader;
        openReader(reader.handle, archivePath, cb);
        result.multiVolume = !volumesFor(archivePath, cb).empty();
        ZipHandle zip(nullptr, &zip_discard);
        // ZIP central-directory metadata contains actual compressed lengths/methods;
        // libarchive has no general compressed-size-per-entry accessor.
        if (!result.multiVolume && lower(utf8(archivePath.filename())).ends_with(".zip")) {
            try { zip = openZip(archivePath, ZIP_RDONLY); result.editable = true; } catch (...) {}
        }
        archive_entry* entry = nullptr;
        std::vector<ArchiveEntryInfo> batch;
        batch.reserve(batchSize);
        while (true) {
            checkCancelled(cb);
            const auto code = archive_read_next_header(reader.handle, &entry);
            if (code == ARCHIVE_EOF) break;
            requireSuccess(code, reader.handle, "Read archive entry");
            if (result.filesProcessed >= MaxEntries) throw std::runtime_error("Archive has too many entries");
            if (archive_format(reader.handle) == ARCHIVE_FORMAT_RAW && archive_filter_code(reader.handle, 0) == ARCHIVE_FILTER_NONE)
                throw std::runtime_error("Not a recognized archive or compressed stream");
            auto info = metadata(entry);
            if (zip) {
                zip_stat_t st;
                zip_stat_init(&st);
                if (zip_stat(zip.get(), info.path.c_str(), ZIP_FL_ENC_UTF_8, &st) == 0) {
                    if ((st.valid & ZIP_STAT_ENCRYPTION_METHOD) && st.encryption_method != ZIP_EM_NONE)
                        result.editable = false;
                    if (st.valid & ZIP_STAT_COMP_SIZE) { info.compressedSize = st.comp_size; info.compressedSizeKnown = true; }
                    if (st.valid & ZIP_STAT_COMP_METHOD) {
                        switch (st.comp_method) {
                            case 0: info.compressionMethod = "Store"; break;
                            case 8: info.compressionMethod = "Deflate"; break;
                            case 12: info.compressionMethod = "BZip2"; break;
                            case 14: info.compressionMethod = "LZMA"; break;
                            case 93: info.compressionMethod = "Zstandard"; break;
                            case 95: info.compressionMethod = "XZ"; break;
                            default: info.compressionMethod = "ZIP method " + std::to_string(st.comp_method);
                        }
                    }
                }
            }
            if (info.type == EntryType::Directory && (info.path == "." || info.path == "./")) {
                requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip root metadata");
                continue;
            }
            try { info.path = safeRelative(info.path); }
            catch (const std::exception&) { /* Keep malformed records visible; extraction rejects them. */ }
            batch.push_back(std::move(info));
            requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip entry contents");
            ++result.filesProcessed;
            if (cb.progress && result.filesProcessed % 128 == 0)
                cb.progress(result.filesProcessed, batch.back().path);
            if (batch.size() == batchSize) {
                sink(std::move(batch));
                batch.clear();
                batch.reserve(batchSize);
            }
        }
        if (!batch.empty()) sink(std::move(batch));
        result.ok = true;
    } catch (const std::exception& e) { result.error = e.what(); }
    return result;
}

Result ArchiveEngine::inspect(const fs::path& archivePath, const Callbacks& cb) {
    Result result;
    // Keep the convenience API for callers that need the entire catalog.
    auto summary = inspectBatched(archivePath, [&result](std::vector<ArchiveEntryInfo> batch) {
        result.entries.insert(result.entries.end(),
                              std::make_move_iterator(batch.begin()),
                              std::make_move_iterator(batch.end()));
    }, cb);
    result.ok = summary.ok;
    result.error = std::move(summary.error);
    result.filesProcessed = summary.filesProcessed;
    result.editable = summary.editable;
    result.multiVolume = summary.multiVolume;
    return result;
}

Result ArchiveEngine::extract(const fs::path& archivePath, const fs::path& destination,
                              const std::vector<std::string>& selectedPaths, const Callbacks& cb,
                              const ExtractOptions& options) {
    Result result;
    std::vector<fs::path> created;
    struct DirMetadata {fs::path path; int depth; int perms; std::time_t mtime; bool hasMtime;};
    std::vector<DirMetadata> directoriesToRestore;
    std::vector<DirMetadata> filesToRestore;
    std::vector<std::pair<fs::path,fs::path>> overwritten;
    struct PendingLink { fs::path path; fs::path relative; fs::path link; };
    std::vector<PendingLink> links;
    try {
        validateLimits(options);
        const bool rootExisted = fs::exists(destination);
        prepareRoot(destination);
        if (!rootExisted) created.push_back(destination);
        const auto root = fs::weakly_canonical(destination);
        const auto inputVolumes = volumesFor(archivePath, cb);
        std::uint64_t knownBytes = 0;
        bool knownTotal = true;
        std::unordered_set<std::string> outputPaths;
        Callbacks scan = cb;
        scan.progress = {};
        auto preflight = inspectBatched(archivePath, [&](std::vector<ArchiveEntryInfo> batch) {
            for (const auto& item : batch) {
                if ((item.path == "." || item.path == "./") && item.type == EntryType::Directory) continue;
                const auto relative = safeRelative(item.path);
                if (item.type != EntryType::Regular || !selected(relative, selectedPaths)) continue;
                // Default desktop Windows/macOS filesystems may compare names case-insensitively.
                #if defined(_WIN32) || defined(__APPLE__)
                const auto collisionKey = lower(relative);
                #else
                const auto& collisionKey = relative;
                #endif
                if (!outputPaths.insert(collisionKey).second)
                    throw std::runtime_error("Duplicate archive file path: " + relative);
                if (!item.sizeKnown) { knownTotal = false; continue; }
                if (item.size > options.maxEntryBytes || item.size > options.maxTotalBytes - knownBytes)
                    throw std::runtime_error("Extraction exceeds configured safety limits");
                knownBytes += item.size;
            }
        }, scan);
        if (!preflight.ok) throw std::runtime_error(preflight.error);
        outputPaths.clear();
        outputPaths.rehash(0);
        if (knownBytes > fs::space(root).available)
            throw std::runtime_error("Insufficient available disk space");
        if (knownTotal && cb.totalBytes) cb.totalBytes(knownBytes);
        ArchiveReader reader;
        openReader(reader.handle, archivePath, cb);
        archive_entry* entry = nullptr;
        std::size_t entries = 0;
        std::array<char, BufferSize> buffer{};
        while (true) {
            checkCancelled(cb);
            const int code = archive_read_next_header(reader.handle, &entry);
            if (code == ARCHIVE_EOF) break;
            requireSuccess(code, reader.handle, "Read archive header");
            if (++entries > options.maxEntries) throw std::runtime_error("Archive has too many entries");
            const auto original = entryPath(entry);
            if ((original == "." || original == "./") && entryType(entry) == EntryType::Directory) {
                requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip root entry");
                continue;
            }
            const auto relative = safeRelative(original);
            if (!selected(relative, selectedPaths)) {
                requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip entry");
                continue;
            }
            const auto type = entryType(entry);
            if (type == EntryType::Link) {
                auto* rawLink = archive_entry_symlink_utf8(entry);
                if (!rawLink) rawLink = archive_entry_symlink(entry);
                if (!rawLink) throw std::runtime_error("Missing symbolic link target");
                const auto cleanLink = fromUtf8(safeRelative(rawLink));
                const auto rel = fromUtf8(relative);
                links.push_back({root / rel, rel, cleanLink});
                requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip link data");
                continue;
            }
            if (archive_entry_hardlink(entry)) {
                result.warnings.push_back("Skipped hard link: " + relative);
                requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip hard link data");
                continue;
            }
            if (type != EntryType::Regular && type != EntryType::Directory) {
                result.warnings.push_back("Skipped link/special entry: " + relative);
                requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip special entry");
                continue;
            }
            fs::path relPath = fromUtf8(relative);
            auto target = root / relPath;
            if (type == EntryType::Directory) {
                const auto countBefore = created.size();
                ensureDirectories(root, relPath, &created);
                // Only alter metadata on directories created by this operation.
                if (created.size() > countBefore && created.back() == target) {
                    directoriesToRestore.push_back({target,
                        static_cast<int>(std::distance(relPath.begin(), relPath.end())),
                        static_cast<int>(archive_entry_perm(entry)),
                        archive_entry_mtime(entry), archive_entry_mtime_is_set(entry) != 0});
                }
                result.filesProcessed++;
                continue;
            }
            ensureDirectories(root, relPath.parent_path(), &created);
            // Existing file conflicts are resolved explicitly; no implicit overwrites.
            if (fs::exists(fs::symlink_status(target))) {
                if (fs::equivalent(target, archivePath) || std::any_of(inputVolumes.begin(), inputVolumes.end(), [&](const auto& volume){ return fs::equivalent(target, volume); }))
                    throw std::runtime_error("Extraction cannot overwrite the original archive or its volumes");
                const auto policy = options.conflictResolver ? options.conflictResolver(utf8(target)) : options.existingFiles;
                if (policy == ExistingFilePolicy::Fail)
                    throw std::runtime_error("File already exists at extraction destination: " + utf8(target));
                if (policy == ExistingFilePolicy::Skip) {
                    result.warnings.push_back("Skipped existing file: " + relative);
                    requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip conflict");
                    continue;
                }
                if (policy == ExistingFilePolicy::Overwrite) {
                    if (!fs::is_regular_file(fs::symlink_status(target)))
                        throw std::runtime_error("Only existing regular files can be overwritten");
#ifdef _WIN32
                    const auto attrs = GetFileAttributesW(target.c_str());
                    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                        throw std::runtime_error("Refusing to overwrite a reparse point");
#endif
                    auto backup = target;
                    do { backup = target; backup += ".arky-rollback-" + std::to_string(std::random_device{}()); }
                    while (fs::exists(fs::symlink_status(backup)));
                    fs::rename(target, backup);
                    overwritten.emplace_back(target, backup);
                } else {
                auto stem = target.stem().native(), ext = target.extension().native();
                bool found = false;
                for (unsigned i = 1; i < 10'000; ++i) {
                    auto candidate = target.parent_path() / fs::path(stem +
#ifdef _WIN32
                        L" (" + std::to_wstring(i) + L")" + ext
#else
                        " (" + std::to_string(i) + ")" + ext
#endif
                    );
                    if (!fs::exists(fs::symlink_status(candidate))) {
                        target = std::move(candidate); found = true; break;
                    }
                }
                if (!found) throw std::runtime_error("Could not choose a conflict-free destination name");
                result.warnings.push_back("Renamed conflicting output: " + relative);
                }
            }
            // ExclusiveOutput also refuses symlink destinations and removes partial writes.
            {
                ExclusiveOutput out(target, root, target.lexically_relative(root));
                std::uint64_t entryBytes = 0;
                while (true) {
                    checkCancelled(cb);
                    const auto count = archive_read_data(reader.handle, buffer.data(), buffer.size());
                    if (count < 0) throw std::runtime_error(archiveError(reader.handle, "Decompress file"));
                    if (count == 0) break;
                    checkDecodedBudget(options, result.bytesProcessed, entryBytes,
                                       static_cast<std::uint64_t>(count), reader.handle);
                    if ((entryBytes % (4ULL*1024*1024) == 0 || count != static_cast<la_ssize_t>(buffer.size())) &&
                        static_cast<std::uint64_t>(count) > fs::space(root).available)
                        throw std::runtime_error("Insufficient disk space during extraction");
                    out.write(buffer.data(), static_cast<std::size_t>(count));
                    entryBytes += static_cast<std::uint64_t>(count);
                    result.bytesProcessed += static_cast<std::uint64_t>(count);
                    if (cb.progress) cb.progress(result.bytesProcessed, relative);
                }
                out.finish();
            }
            created.push_back(target);
            filesToRestore.push_back({target, 0, static_cast<int>(archive_entry_perm(entry)),
                                      archive_entry_mtime(entry), archive_entry_mtime_is_set(entry) != 0});
            result.filesProcessed++;
        }
        // Delay link creation until no regular files will be written through links.
        for (const auto& item : links) {
            checkCancelled(cb);
            ensureDirectories(root, item.relative.parent_path(), &created);
            if (fs::exists(fs::symlink_status(item.path))) {
                if (options.existingFiles == ExistingFilePolicy::Skip) continue;
                throw std::runtime_error("Symbolic link destination already exists");
            }
            auto cursor = root;
            const auto resolved = item.relative.parent_path() / item.link;
            for (const auto& component : resolved) {
                cursor /= component;
                if (fs::is_symlink(fs::symlink_status(cursor)))
                    throw std::runtime_error("Symbolic link target traverses another link");
#ifdef _WIN32
                const auto attrs = GetFileAttributesW(cursor.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                    throw std::runtime_error("Symbolic link target traverses a reparse point");
#endif
            }
            std::error_code error;
            if (fs::is_directory(root / resolved)) fs::create_directory_symlink(item.link, item.path, error);
            else fs::create_symlink(item.link, item.path, error);
            if (error) result.warnings.push_back("Could not restore symbolic link: " + utf8(item.path));
            else { created.push_back(item.path); ++result.filesProcessed; }
        }
        // Apply file metadata only after extraction succeeds. Rollback must never
        // have to remove a read-only file that we just restored from the archive.
        for (const auto& file : filesToRestore) {
            EntryHolder entry;
            archive_entry_set_perm(entry.handle, file.perms);
            if (file.hasMtime) archive_entry_set_mtime(entry.handle, file.mtime, 0);
            restoreFileMetadata(file.path, entry.handle, result);
        }
        // Directory mtimes are restored after all children have been written.
        std::sort(directoriesToRestore.begin(), directoriesToRestore.end(),
                  [](const DirMetadata& a, const DirMetadata& b) {return a.depth > b.depth;});
        for (const auto& dir : directoriesToRestore) {
            EntryHolder entry;
            archive_entry_set_perm(entry.handle, dir.perms);
            if (dir.hasMtime) archive_entry_set_mtime(entry.handle, dir.mtime, 0);
            restoreFileMetadata(dir.path, entry.handle, result);
        }
        for (const auto& [original, backup] : overwritten) {
            std::error_code error;
            fs::remove(backup, error);
            if (error) result.warnings.push_back("Could not remove rollback copy: " + utf8(backup));
        }
        result.ok = true;
    } catch (const std::exception& e) {
        result.error = e.what();
        // Roll back only files/folders created during *this* extraction; never
        // delete destination content that existed before the operation.
        for (auto it = created.rbegin(); it != created.rend(); ++it) {
            std::error_code ec;
            fs::remove(*it, ec); // Only removes empty directories; no remove_all.
            if (ec) result.warnings.push_back("Rollback could not remove: " + utf8(*it));
        }
        for (auto it = overwritten.rbegin(); it != overwritten.rend(); ++it) {
            std::error_code error;
            if (!fs::exists(fs::symlink_status(it->first, error))) {
                error.clear(); fs::rename(it->second, it->first, error);
            } else error = std::make_error_code(std::errc::file_exists);
            if (error) result.warnings.push_back("Original retained at: " + utf8(it->second));
        }
    }
    return result;
}

Result ArchiveEngine::create(const fs::path& outputArchive, const std::vector<fs::path>& sources,
                             const Callbacks& cb, const CreateOptions& options) {
    Result result;
    fs::path temp;
    try {
        validateCreateOptions(describeFormat(utf8(outputArchive.filename())), options, cb.passphrase);
        if (sources.empty()) throw std::runtime_error("Select at least one file or folder");
        if (fs::exists(outputArchive)) throw std::runtime_error("Output archive already exists; refusing to overwrite");
        if (!fs::exists(outputArchive.parent_path()))
            throw std::runtime_error("Output folder does not exist");
        std::vector<InputFile> files;
        std::unordered_set<std::string> seen;
        for (const auto& src : sources) {
            if (!fs::exists(src)) throw std::runtime_error("Source path does not exist: " + utf8(src));
            gather(src, utf8(src.filename()), files, seen, cb);
        }
        if (cb.totalBytes) {
            std::uint64_t total = 0;
            for (const auto& file : files) total += file.size;
            cb.totalBytes(total);
        }
        // The temporary file lives in the destination directory to enable atomic rename.
        // Random suffix prevents common temp-name collisions.
        std::random_device rng;
        for (int i = 0; i < 32; ++i) {
            temp = outputArchive;
            temp += ".arky-part-" + std::to_string(rng());
            if (!fs::exists(temp)) break;
        }
        if (fs::exists(temp)) throw std::runtime_error("Cannot obtain a free temporary output name");
        ArchiveWriter writer;
        configureWriter(writer.handle, utf8(outputArchive.filename()), options);
        openWriter(writer.handle, temp);
        std::array<char, BufferSize> buffer{};
        for (const auto& file : files) {
            checkCancelled(cb);
            EntryHolder entry;
            archive_entry_set_pathname_utf8(entry.handle, file.relative.c_str());
            archive_entry_set_filetype(entry.handle, file.directory ? AE_IFDIR : AE_IFREG);
            setSourceMetadata(entry.handle, file.actual);
            archive_entry_set_size(entry.handle, static_cast<la_int64_t>(file.size));
            requireSuccess(archive_write_header(writer.handle, entry.handle), writer.handle, "Write entry header");
            if (!file.directory) {
                std::ifstream input(file.actual, std::ios::binary);
                if (!input) throw std::runtime_error("Cannot open source file: " + utf8(file.actual));
                std::uint64_t inputBytes = 0;
                while (input) {
                    checkCancelled(cb);
                    input.read(buffer.data(), buffer.size());
                    const auto got = static_cast<std::size_t>(input.gcount());
                    std::size_t offset = 0;
                    while (offset < got) {
                        const auto wrote = archive_write_data(writer.handle, buffer.data() + offset, got - offset);
                        if (wrote <= 0) throw std::runtime_error(archiveError(writer.handle, "Compress file"));
                        offset += static_cast<std::size_t>(wrote);
                    }
                    result.bytesProcessed += got;
                    inputBytes += got;
                    if (cb.progress) cb.progress(result.bytesProcessed, file.relative);
                }
                if (input.bad()) throw std::runtime_error("Error reading source: " + utf8(file.actual));
                if (inputBytes != file.size) throw std::runtime_error("Source changed during compression");
            }
            requireSuccess(archive_write_finish_entry(writer.handle), writer.handle, "Finish entry");
            result.filesProcessed++;
        }
        requireSuccess(archive_write_close(writer.handle), writer.handle, "Finalize archive");
        // Both paths live in the same directory. Hard-link publication is atomic
        // and fails if the target already exists (unlike POSIX rename()).
        // Unsupported filesystems fail closed instead of risking an overwrite.
        std::error_code ec;
        fs::create_hard_link(temp, outputArchive, ec);
        if (ec) throw std::runtime_error("Could not publish archive without overwriting: " + ec.message());
        fs::remove(temp, ec);
        if (ec) result.warnings.push_back("Remove temporary build file manually: " + utf8(temp));
        temp.clear();
        result.ok = true;
    } catch (const std::exception& e) { result.error = e.what(); }
    if (!temp.empty()) { std::error_code ec; fs::remove(temp, ec); }
    return result;
}

Result ArchiveEngine::preview(const fs::path& archivePath, const std::string& wanted,
                              std::string& output, std::size_t maxBytes, const Callbacks& cb) {
    Result result;
    output.clear();
    try {
        if (maxBytes == 0 || maxBytes > 1024 * 1024)
            throw std::runtime_error("Preview limit must be 1 byte to 1 MiB");
        const auto cleanWanted = safeRelative(wanted);
        ArchiveReader reader;
        openReader(reader.handle, archivePath, cb);
        archive_entry* entry = nullptr;
        std::array<char, BufferSize> buffer{};
        bool found = false;
        while (true) {
            checkCancelled(cb);
            int code = archive_read_next_header(reader.handle, &entry);
            if (code == ARCHIVE_EOF) break;
            requireSuccess(code, reader.handle, "Read archive header");
            if (archive_format(reader.handle) == ARCHIVE_FORMAT_RAW && archive_filter_code(reader.handle, 0) == ARCHIVE_FILTER_NONE)
                throw std::runtime_error("Not a recognized archive or compressed stream");
            auto name = entryPath(entry);
            try { name = safeRelative(name); } catch (const std::exception&) {}
            if (name == cleanWanted && entryType(entry) == EntryType::Regular) {
                found = true;
                while (output.size() < maxBytes) {
                    checkCancelled(cb);
                    const auto n = archive_read_data(reader.handle, buffer.data(),
                                                    (std::min)(buffer.size(), maxBytes - output.size()));
                    if (n < 0) throw std::runtime_error(archiveError(reader.handle, "Read preview"));
                    if (n == 0) break;
                    output.append(buffer.data(), static_cast<std::size_t>(n));
                }
                result.filesProcessed = 1;
                result.bytesProcessed = output.size();
                break;
            }
            requireSuccess(archive_read_data_skip(reader.handle), reader.handle, "Skip entry");
        }
        if (!found) throw std::runtime_error("Entry not found or is not a regular file");
        result.ok = true;
    } catch (const std::exception& e) { result.error = e.what(); output.clear(); }
    return result;
}

// Validate payloads by actually decoding the archive rather than merely
// reading headers. libarchive performs checksum verification when supported.
Result ArchiveEngine::test(const fs::path& archivePath, const Callbacks& cb,
                           const ReadLimits& limits) {
    Result result;
    try {
        validateLimits(limits);
        ArchiveReader reader;
        openReader(reader.handle, archivePath, cb);
        archive_entry* entry = nullptr;
        std::array<char, BufferSize> buffer{};
        while (true) {
            checkCancelled(cb);
            auto code = archive_read_next_header(reader.handle, &entry);
            if (code == ARCHIVE_EOF) break;
            requireSuccess(code, reader.handle, "Read archive header");
            if (++result.filesProcessed > limits.maxEntries) throw std::runtime_error("Archive has too many entries");
            if (archive_format(reader.handle) == ARCHIVE_FORMAT_RAW && archive_filter_code(reader.handle, 0) == ARCHIVE_FILTER_NONE)
                throw std::runtime_error("Not a recognized archive or compressed stream");
            auto name = entryPath(entry);
            std::uint64_t entryBytes = 0;
            while (true) {
                checkCancelled(cb);
                auto n = archive_read_data(reader.handle, buffer.data(), buffer.size());
                if (n < 0) throw std::runtime_error(archiveError(reader.handle, "Verify archive data"));
                if (n == 0) break;
                checkDecodedBudget(limits, result.bytesProcessed, entryBytes,
                                   static_cast<std::uint64_t>(n), reader.handle);
                entryBytes += static_cast<std::uint64_t>(n);
                result.bytesProcessed += static_cast<std::uint64_t>(n);
                if (cb.progress) cb.progress(result.bytesProcessed, name);
            }
        }
        result.ok = true;
    } catch (const std::exception& e) { result.error = e.what(); }
    return result;
}

Result ArchiveEngine::modify(const fs::path& archivePath,
                             const std::vector<std::string>& removePaths,
                             const std::vector<fs::path>& addSources,
                             const Callbacks& cb) {
    EditPlan plan;
    plan.removals = removePaths;
    for (const auto& file : addSources)
        plan.additions.push_back({file, utf8(file.filename()), false});
    return edit(archivePath, plan, cb);
}

Result ArchiveEngine::edit(const fs::path& archivePath, const EditPlan& plan, const Callbacks& cb) {
    Result result;
    try {
        if (!describeFormat(utf8(archivePath.filename())).editable)
            throw std::runtime_error("Unsupported modification: only ZIP archives are editable");
        if (!fs::is_regular_file(fs::symlink_status(archivePath)))
            throw std::runtime_error("Original archive must be a regular file");
        if (plan.removals.empty() && plan.additions.empty() && plan.renames.empty() && plan.createDirectories.empty())
            throw std::runtime_error("Nothing to change");
        checkCancelled(cb);
        const auto originalSize = fs::file_size(archivePath);
        const auto originalTime = fs::last_write_time(archivePath);
        auto z = openZip(archivePath, ZIP_CHECKCONS);
        auto* zip = z.get();
        auto check = [&](bool success) {
            if (!success) throw std::runtime_error(std::string("ZIP transaction: ") + zip_strerror(zip));
        };
        // Keep an empty ZIP instead of letting zip_close remove the original.
        check(zip_set_archive_flag(zip, ZIP_AFL_CREATE_OR_KEEP_FILE_FOR_EMPTY_ARCHIVE, 1) == 0);
        struct Record { zip_uint64_t index; std::string path, finalPath; bool directory, remove; };
        std::vector<Record> records;
        std::unordered_map<std::string, bool> names;
        const auto count = zip_get_num_entries(zip, 0);
        if (count < 0 || static_cast<std::uint64_t>(count) > MaxEntries)
            throw std::runtime_error("Invalid or excessive ZIP entry count");
        std::vector<std::string> removes;
        for (const auto& name : plan.removals) removes.push_back(safeRelative(name));
        std::vector<EditRename> renames;
        for (const auto& rule : plan.renames) {
            auto from = safeRelative(rule.from), to = safeRelative(rule.to);
            if (from == to) continue;
            if (selected(to, {from})) throw std::runtime_error("Cannot move a folder into itself");
            for (const auto& other : renames)
                if (selected(from, {other.from}) || selected(other.from, {from}))
                    throw std::runtime_error("Overlapping rename rules");
            renames.push_back({std::move(from), std::move(to)});
        }
        std::vector<bool> matched(renames.size());
        auto addName = [&](const std::string& name, bool directory) {
            if (!names.emplace(name, directory).second)
                throw std::runtime_error("Duplicate ZIP destination: " + name);
        };
        for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
            checkCancelled(cb);
            zip_stat_t st;
            zip_stat_init(&st);
            check(zip_stat_index(zip, i, 0, &st) == 0);
            if (!st.name) throw std::runtime_error("Missing ZIP entry name");
            if ((st.valid & ZIP_STAT_ENCRYPTION_METHOD) && st.encryption_method != ZIP_EM_NONE)
                throw std::runtime_error("Encrypted ZIP archives are read-only");
            const std::string original(st.name);
            auto path = safeRelative(original);
            const bool dir = original.ends_with('/');
            const bool remove = !removes.empty() && selected(path, removes);
            auto final = path;
            for (std::size_t j = 0; j < renames.size(); ++j) {
                if (selected(path, {renames[j].from})) {
                    final = renames[j].to + path.substr(renames[j].from.size());
                    matched[j] = true;
                    break;
                }
            }
            if (!remove) addName(final, dir);
            records.push_back({i, original, final, dir, remove});
        }
        if (std::find(matched.begin(), matched.end(), false) != matched.end())
            throw std::runtime_error("Rename source not found");
        std::vector<InputFile> additions;
        std::unordered_set<std::string> gathered, replacements;
        for (const auto& item : plan.additions) {
            const auto destination = safeRelative(item.target.empty() ? utf8(item.source.filename()) : item.target);
            gather(item.source, destination, additions, gathered, cb);
            if (item.replace) {
                if (!fs::is_regular_file(item.source)) throw std::runtime_error("Replacement must be a regular file");
                replacements.insert(destination);
            }
        }
        for (const auto& item : additions) {
            if (replacements.contains(item.relative)) {
                const auto found = names.find(item.relative);
                if (found == names.end() || found->second) throw std::runtime_error("Replacement target not a file");
            } else addName(item.relative, item.directory);
        }
        for (const auto& dir : plan.createDirectories) addName(safeRelative(dir), true);
        for (const auto& [name, dir] : names) {
            for (auto pos = name.find('/'); pos != std::string::npos; pos = name.find('/', pos + 1)) {
                const auto parent = names.find(name.substr(0, pos));
                if (parent != names.end() && !parent->second)
                    throw std::runtime_error("File conflicts with directory: " + parent->first);
            }
        }
        // Delete first, then rename via unique temporary names *inside* the transaction.
        // This supports swaps without touching the archive on disk until zip_close.
        for (const auto& entry : records)
            if (entry.remove) { check(zip_delete(zip, entry.index) == 0); ++result.filesProcessed; }
        std::string prefix;
        do { prefix = ".arky-transaction-" + std::to_string(std::random_device{}()) + "/"; }
        while (std::any_of(records.begin(), records.end(), [&](const auto& r){return r.path.starts_with(prefix) || r.finalPath.starts_with(prefix);}));
        for (const auto& entry : records) {
            const auto final = entry.finalPath + (entry.directory ? "/" : "");
            if (!entry.remove && final != entry.path)
                check(zip_file_rename(zip, entry.index, (prefix + std::to_string(entry.index) + (entry.directory ? "/" : "")).c_str(), ZIP_FL_ENC_UTF_8) == 0);
        }
        for (const auto& entry : records) {
            const auto final = entry.finalPath + (entry.directory ? "/" : "");
            if (!entry.remove && final != entry.path) {
                check(zip_file_rename(zip, entry.index, final.c_str(), ZIP_FL_ENC_UTF_8) == 0);
                ++result.filesProcessed;
            }
        }
        for (const auto& dir : plan.createDirectories) {
            check(zip_dir_add(zip, (safeRelative(dir) + "/").c_str(), ZIP_FL_ENC_UTF_8) >= 0);
            ++result.filesProcessed;
        }
        for (const auto& item : additions) {
            checkCancelled(cb);
            if (item.directory) {
                check(zip_dir_add(zip, (item.relative + "/").c_str(), ZIP_FL_ENC_UTF_8) >= 0);
            } else {
#ifdef _WIN32
                auto* source = zip_source_win32w(zip, item.actual.c_str(), 0, -1);
#else
                auto* source = zip_source_file(zip, item.actual.c_str(), 0, -1);
#endif
                check(source != nullptr);
                const auto index = zip_file_add(zip, item.relative.c_str(), source,
                    ZIP_FL_ENC_UTF_8 | (replacements.contains(item.relative) ? ZIP_FL_OVERWRITE : 0));
                if (index < 0) { zip_source_free(source); check(false); }
                EntryHolder metadata;
                setSourceMetadata(metadata.handle, item.actual);
                check(zip_file_set_mtime(zip, static_cast<zip_uint64_t>(index), archive_entry_mtime(metadata.handle), 0) == 0);
                const auto attr = (0100000U | static_cast<unsigned>(archive_entry_perm(metadata.handle))) << 16;
                check(zip_file_set_external_attributes(zip, static_cast<zip_uint64_t>(index), 0, ZIP_OPSYS_UNIX, attr) == 0);
                result.bytesProcessed += item.size;
            }
            ++result.filesProcessed;
        }
        checkCancelled(cb);
        if (fs::file_size(archivePath) != originalSize || fs::last_write_time(archivePath) != originalTime)
            throw std::runtime_error("ZIP changed during edit; no transaction committed");
        zip_register_cancel_callback_with_state(zip, [](zip_t*, void* ptr) -> int {
            auto& c = *static_cast<const Callbacks*>(ptr);
            return c.cancelled && c.cancelled() ? 1 : 0;
        }, nullptr, const_cast<Callbacks*>(&cb));
        zip_register_progress_callback_with_state(zip, 0.01, [](zip_t*, double fraction, void* ptr) {
            auto& c = *static_cast<const Callbacks*>(ptr);
            if (c.progress) c.progress(static_cast<std::uint64_t>(fraction * 100), "ZIP commit (%)");
        }, nullptr, const_cast<Callbacks*>(&cb));
        // On error libzip leaves the source untouched; discard releases pending changes.
        check(zip_close(zip) == 0);
        z.release();
        result.ok = true;
    } catch (const std::exception& e) { result.error = e.what(); }
    return result;
}

FormatCapabilities ArchiveEngine::capabilities(const std::string& filename) {
    return describeFormat(filename);
}

std::vector<std::string> ArchiveEngine::writableExtensions() {
    return {".zip", ".7z", ".tar", ".tar.gz", ".tar.xz", ".tar.zst", ".tgz", ".txz"};
}
} // namespace arky
