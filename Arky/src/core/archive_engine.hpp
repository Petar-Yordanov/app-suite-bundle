#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace arky {
namespace fs = std::filesystem;

enum class EntryType { Regular, Directory, Link, Other };

struct ArchiveEntryInfo {
    std::string path; // UTF-8 archive-relative path; no file contents
    std::uint64_t size = 0;
    bool sizeKnown = false;
    std::time_t modified = 0;
    EntryType type = EntryType::Other;
    std::uint64_t compressedSize = 0;
    bool compressedSizeKnown = false;
    unsigned permissions = 0;
    bool permissionsKnown = false;
    std::string compressionMethod;
};

struct Result {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
    std::vector<ArchiveEntryInfo> entries; // populated by inspect only
    std::uint64_t filesProcessed = 0;
    std::uint64_t bytesProcessed = 0;
    bool editable = false; // verified ZIP transaction eligibility
    bool multiVolume = false;
};

enum class ExistingFilePolicy { Fail, Skip, Rename, Overwrite };
// Limits apply to bytes decompressed, not the compressed archive size.
// The expansion-ratio check is approximate (physical bytes consumed by libarchive).
struct ReadLimits {
    std::uint64_t maxTotalBytes = 256ULL * 1024 * 1024 * 1024;
    std::uint64_t maxEntryBytes = 64ULL * 1024 * 1024 * 1024;
    std::size_t maxEntries = 2'000'000;
    std::uint64_t maxExpansionRatio = 10'000; // 0 disables ratio checking
    std::uint64_t ratioCheckAfterBytes = 8ULL * 1024 * 1024;
};
struct ExtractOptions : ReadLimits {
    ExistingFilePolicy existingFiles = ExistingFilePolicy::Fail;
    std::function<ExistingFilePolicy(const std::string&)> conflictResolver;
};

struct EditAddition {
    fs::path source;
    std::string target; // archive-relative path, or directory prefix + source name
    bool replace = false;
};
struct EditRename {
    std::string from;
    std::string to; // directory rename remaps its descendants as well
};
struct EditPlan {
    std::vector<std::string> removals;
    std::vector<EditAddition> additions;
    std::vector<EditRename> renames;
    std::vector<std::string> createDirectories;
};

struct CreateOptions {
    // -1 uses libarchive defaults; levels 0..9 where a codec supports them.
    int compressionLevel = -1;
    // 'auto', 'store', 'deflate', 'bzip2', 'lzma', 'xz', 'zstd',
    // 'lzma1' and 'lzma2' where supported by the chosen container.
    std::string compressionMethod = "auto";
    unsigned threads = 1; // XZ/Zstandard filters only, where supported
};

struct FormatCapabilities {
    std::string extension; // A canonical filename suffix, e.g. '.tar.gz'.
    bool writable = false;
    bool editable = false;
    bool multipleFiles = true;
    bool supportsCompressionLevel = false;
    bool supportsMultiVolume = false; // libarchive writer does not implement split outputs.
    std::vector<std::string> methods; // 'auto' is always the first method.
};

struct Callbacks {
    // Invoked on the calling thread. The UI calls the engine from a worker thread.
    std::function<void(std::uint64_t bytes, const std::string& item)> progress;
    std::function<bool()> cancelled;
    std::string passphrase; // Optional; only retained while the worker is active.
    std::function<void(std::uint64_t total)> totalBytes; // known extraction/creation total
    std::vector<fs::path> volumes; // explicit ordered volumes, if supported by the reader
};

class ArchiveEngine final {
public:
    // Both listing and extraction stream the input; no whole-archive read.
    static Result inspect(const fs::path& archivePath, const Callbacks& cb = {});
    // Streams *metadata* in batches to avoid an additional full list in workers.
    // The sink executes synchronously on the worker thread. Returned result.entries is empty.
    using EntryBatchSink = std::function<void(std::vector<ArchiveEntryInfo>)>;
    static Result inspectBatched(const fs::path& archivePath, EntryBatchSink sink,
                                 const Callbacks& cb = {}, std::size_t batchSize = 512);
    // Empty selection means everything. Directory selections include descendants.
    static Result extract(const fs::path& archivePath, const fs::path& destination,
                          const std::vector<std::string>& selectedPaths = {},
                          const Callbacks& cb = {}, const ExtractOptions& options = {});
    // Sources become top-level entries named after their filenames.
    // Existing output files are never overwritten.
    static Result create(const fs::path& outputArchive,
                         const std::vector<fs::path>& sources,
                         const Callbacks& cb = {}, const CreateOptions& options = {});
    // Return at most maxBytes of one regular entry. Intended for bounded text previews.
    static Result preview(const fs::path& archivePath, const std::string& entryPath,
                          std::string& output, std::size_t maxBytes = 128 * 1024,
                          const Callbacks& cb = {});
    // ZIP-only transactions through libzip; all other formats are read-only.
    static Result modify(const fs::path& archivePath,
                         const std::vector<std::string>& removePaths,
                         const std::vector<fs::path>& addSources,
                         const Callbacks& cb = {});
    // Read and decompress every entry to verify structural/data integrity without extraction.
    static Result test(const fs::path& archivePath, const Callbacks& cb = {},
                       const ReadLimits& limits = {});
    // ZIP transaction; explicitly validate all destination paths and collisions.
    static Result edit(const fs::path& archivePath, const EditPlan& plan,
                       const Callbacks& cb = {});
    // Pure capability lookup; a selected optional codec can still fail at
    // runtime if libarchive was compiled without its compression backend.
    static FormatCapabilities capabilities(const std::string& filename);
    static std::vector<std::string> writableExtensions();
};
} // namespace arky
