#include "archive_engine.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zip.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using arky::ArchiveEngine;
namespace fs = std::filesystem;

static void ensure(bool b, const std::string& what) {
    if (!b) throw std::runtime_error("TEST FAILED: " + what);
}
static void writeBytes(const fs::path& file, std::size_t n) {
    std::ofstream out(file, std::ios::binary);
    ensure(out.good(), "open fixture file");
    std::array<char, 64 * 1024> buf;
    for (std::size_t i = 0; i < n; i += buf.size()) {
        for (std::size_t j = 0; j < buf.size(); ++j) buf[j] = static_cast<char>((i + j) * 71U + 13U);
        out.write(buf.data(), static_cast<std::streamsize>((std::min)(buf.size(), n - i)));
    }
}
static bool equalFiles(const fs::path& a, const fs::path& b) {
    if (fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream l(a, std::ios::binary), r(b, std::ios::binary);
    std::array<char, 8192> b1{}, b2{};
    while (l && r) {
        l.read(b1.data(), b1.size());
        r.read(b2.data(), b2.size());
        if (l.gcount() != r.gcount()) return false;
        if (!std::equal(b1.begin(), b1.begin() + l.gcount(), b2.begin())) return false;
    }
    return true;
}
static void makeMaliciousTar(const fs::path& output) {
    archive* a = archive_write_new();
    ensure(a != nullptr, "allocate malicious tar writer");
    ensure(archive_write_set_format_pax_restricted(a) == ARCHIVE_OK, "format malicious tar");
    ensure(archive_write_open_filename(a, output.string().c_str()) == ARCHIVE_OK, "open malicious tar");
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, "../escape.txt");
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_size(e, 7);
    ensure(archive_write_header(a, e) == ARCHIVE_OK, "write malicious header");
    ensure(archive_write_data(a, "attack!", 7) == 7, "write malicious content");
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
}
int main(int argc, char** argv) {
    const auto root = fs::temp_directory_path() /
        ("arky-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    try {
        fs::create_directories(root / "inputs" / "folder");
        writeBytes(root / "inputs" / "sample.bin", 4 * 1024 * 1024 + 13);
        writeBytes(root / "inputs" / "folder" / "another.txt", 111'111);
        fs::create_directory(root / "extracted");

        for (const auto& ext : {".zip", ".tar.gz", ".7z", ".tar", ".tar.xz", ".tar.zst"}) {
            auto archivePath = root / (std::string("bundle") + ext);
            auto create = ArchiveEngine::create(archivePath, {root / "inputs" / "sample.bin", root / "inputs" / "folder"});
            ensure(create.ok, "create " + std::string(ext) + ": " + create.error);
            ensure(create.bytesProcessed > 4 * 1024 * 1024, "processed input bytes");
            auto listed = ArchiveEngine::inspect(archivePath);
            ensure(listed.ok, "inspect " + std::string(ext) + ": " + listed.error);
            ensure(listed.entries.size() >= 3, "entries include folder and nested file");
            auto output = root / "extracted" / std::string(ext + 1);
            auto extraction = ArchiveEngine::extract(archivePath, output);
            ensure(extraction.ok, "extract " + std::string(ext) + ": " + extraction.error);
            ensure(equalFiles(root / "inputs" / "sample.bin", output / "sample.bin"), "4MiB stream roundtrip");
            ensure(equalFiles(root / "inputs" / "folder" / "another.txt", output / "folder" / "another.txt"), "nested stream roundtrip");
            auto overwrite = ArchiveEngine::extract(archivePath, output);
            ensure(!overwrite.ok, "pre-existing outputs cannot be overwritten");
            auto recreate = ArchiveEngine::create(archivePath, {root / "inputs" / "sample.bin"});
            ensure(!recreate.ok, "existing archive cannot be overwritten");
            std::cout << "PASS format " << ext << "\n";
        }
        auto selection = ArchiveEngine::extract(root / "bundle.zip", root / "selected", {"folder"});
        ensure(selection.ok, "extract selected directory");
        ensure(!fs::exists(root / "selected" / "sample.bin"), "unselected entry excluded");
        ensure(fs::exists(root / "selected" / "folder" / "another.txt"), "selected directory included");

        const auto encryptedPath = argc > 1 ? fs::path(argv[1]) : fs::path("tests/fixtures/password.zip");
        arky::Callbacks encrypt;
        encrypt.passphrase = "arky-fixture-password";
        auto encryptedCreate = ArchiveEngine::create(root / "forbidden.zip", {root / "inputs" / "sample.bin"}, encrypt);
        ensure(!encryptedCreate.ok && !fs::exists(root / "forbidden.zip"), "encrypted creation prohibited");
        ensure(!ArchiveEngine::extract(encryptedPath, root / "bad-password").ok, "encrypted ZIP rejects absent password");
        auto decrypted = ArchiveEngine::extract(encryptedPath, root / "decrypted", {}, encrypt);
        ensure(decrypted.ok && fs::exists(root / "decrypted" / "password.txt"), "read encrypted ZIP: " + decrypted.error);
        auto forbiddenModify = ArchiveEngine::modify(encryptedPath, {"password.txt"}, {}, encrypt);
        ensure(!forbiddenModify.ok, "encrypted archive is read-only");
        ensure(!ArchiveEngine::inspect(encryptedPath).editable, "encrypted ZIP modification disabled");
        std::cout << "PASS password extraction and rejection of encrypted creation/modification\n";

        // Batch 7: describe capability before exposing a setting in QML.
        {
            const auto zip = ArchiveEngine::capabilities("backup.ZIP");
            ensure(zip.writable && zip.editable &&
                   zip.supportsCompressionLevel && !zip.supportsMultiVolume,
                   "ZIP capability fields");
            ensure(std::find(zip.methods.begin(), zip.methods.end(), "deflate") != zip.methods.end(),
                   "ZIP deflate advertised");
            const auto seven = ArchiveEngine::capabilities("backup.7z");
            ensure(seven.writable && !seven.editable &&
                   std::find(seven.methods.begin(), seven.methods.end(), "lzma2") != seven.methods.end(),
                   "7z writer advertises LZMA2 but not encryption");
            const auto raw = ArchiveEngine::capabilities("file.gz");
            ensure(!raw.writable && !raw.editable, "raw stream is read-only");
            ensure(!ArchiveEngine::capabilities("something.rar").writable,
                   "RAR creation not claimed");
            ensure(!ArchiveEngine::capabilities("a.tar.zst").editable, "tar.zst is read-only");
        }
        // Invalid settings fail early with no output or leftover staging file.
        {
            auto invalid = [&](const char* output, const arky::CreateOptions& options,
                               const arky::Callbacks& cb = {}) {
                const auto path = root / output;
                const auto made = ArchiveEngine::create(path, {root / "inputs" / "sample.bin"}, cb, options);
                ensure(!made.ok && !made.error.empty() && !fs::exists(path),
                       std::string("invalid creation rejected: ") + output);
            };
            invalid("bad-method.zip", {.compressionMethod="lzma2"});
            invalid("bad-method.7z", {.compressionMethod="xz"});
            invalid("bad-level.tar", {.compressionLevel=4});
            invalid("bad-bzip2.zip", {.compressionLevel=0, .compressionMethod="bzip2"});
            invalid("bad-store.zip", {.compressionLevel=9, .compressionMethod="store"});
            invalid("bad-encryption.zip", {}, {.progress={}, .cancelled={}, .passphrase="secret"});
            invalid("bad-encrypted.7z", {}, {.progress={}, .cancelled={}, .passphrase="secret"});
            invalid("bad-unknown.rar", {});
            std::cout << "PASS format capability and invalid option validation\n";
        }
        // Compression methods are actually configured on the writer, not just
        // visible in the UI. Test selected methods against decoded payloads.
        {
            const auto codecSourcePath = root / "inputs" / "folder" / "another.txt";
            struct Case {const char* name; const char* method; int level; bool optional;};
            for (const Case test : {
                Case{"codec-store.zip", "store", 0, false},
                Case{"codec-deflate.zip", "deflate", 5, false},
                Case{"codec-bzip2.zip", "bzip2", 5, true},
                Case{"codec-lzma.zip", "lzma", 5, true},
                Case{"codec-xz.zip", "xz", 5, true},
                Case{"codec-zstd.zip", "zstd", 5, true},
                Case{"codec-store.7z", "store", 0, false},
                Case{"codec-lzma2.7z", "lzma2", 5, false},
                Case{"codec-bzip2.7z", "bzip2", 5, true},
                Case{"codec-zstd.7z", "zstd", 5, true},
                Case{"codec-fast.tar.gz", "auto", 1, false},
                Case{"codec-best.tar.xz", "auto", 9, true},
                Case{"codec-balanced.tar.zst", "auto", 5, true},
            }) {
                const auto dst = root / test.name;
                arky::CreateOptions options;
                options.compressionMethod = test.method;
                options.compressionLevel = test.level;
                const auto made = ArchiveEngine::create(dst, {codecSourcePath}, {}, options);
                if (!made.ok && test.optional) {
                    std::cout << "SKIP optional codec " << test.name << ": " << made.error << "\n";
                    continue;
                }
                ensure(made.ok, std::string("create method ") + test.name + ": " + made.error);
                // Inspect ZIP's local-file header to prove store/deflate was
                // actually selected (rather than silently using the default).
                if (std::string(test.name).ends_with(".zip") &&
                    (std::string(test.method) == "store" || std::string(test.method) == "deflate")) {
                    std::ifstream rawZip(dst, std::ios::binary);
                    std::array<unsigned char, 10> header{};
                    rawZip.read(reinterpret_cast<char*>(header.data()), header.size());
                    ensure(rawZip.gcount() == static_cast<std::streamsize>(header.size()) &&
                           header[0] == 0x50 && header[1] == 0x4b && header[2] == 0x03 && header[3] == 0x04,
                           "ZIP local header signature");
                    const unsigned codec = static_cast<unsigned>(header[8]) |
                                           (static_cast<unsigned>(header[9]) << 8);
                    ensure(codec == (std::string(test.method) == "store" ? 0U : 8U),
                           "ZIP method in local header matches selected codec");
                }
                const auto decoded = root / (std::string(test.name) + "-out");
                const auto expanded = ArchiveEngine::extract(dst, decoded);
                ensure(expanded.ok, std::string("decode method ") + test.name + ": " + expanded.error);
                ensure(equalFiles(codecSourcePath, decoded / "another.txt"), std::string("codec roundtrip ") + test.name);
            }
            std::cout << "PASS explicit compression methods and levels\n";
        }
        // Numeric split archives are read as an ordered stream.
        {
            const auto original = root / "bundle.tar";
            std::ifstream input(original, std::ios::binary);
            std::vector<char> bytes(256*1024);
            int part = 1;
            while (input) {
                input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!input.gcount()) break;
                auto suffix = std::to_string(part++);
                std::ofstream dest(root / ("split.tar." + std::string(3-suffix.size(), '0') + suffix), std::ios::binary);
                dest.write(bytes.data(), input.gcount());
            }
            auto split = ArchiveEngine::extract(root / "split.tar.001", root / "split-out");
            ensure(split.ok && equalFiles(root / "inputs" / "sample.bin", root / "split-out/sample.bin"), "numeric multi-volume roundtrip");
        }

        // Bounded preview: the engine never fills output beyond the caller's limit.
        std::string preview;
        auto previewResult = ArchiveEngine::preview(root / "bundle.zip", "sample.bin", preview, 4096);
        ensure(previewResult.ok && preview.size() == 4096, "streaming bounded preview");
        ensure(!ArchiveEngine::preview(root / "bundle.zip", "missing.bin", preview).ok, "missing preview rejected");

        // Regression: append-only must preserve the existing entries and bytes.
        // selected(path, {}) means ALL for extraction, but modification's empty
        // removePaths must never be interpreted as a request to delete everything.
        for (const auto& ext : {".zip"}) {
            const auto dest = root / (std::string("bundle") + ext);
            writeBytes(root / "new-entry.txt", 12345);
            const auto appended = ArchiveEngine::modify(dest, {}, {root / "new-entry.txt"});
            ensure(appended.ok, "append without removal " + std::string(ext) + ": " + appended.error);
            const auto out = root / "append-check" / std::string(ext + 1);
            auto extracted = ArchiveEngine::extract(dest, out);
            ensure(extracted.ok, "extract after append " + std::string(ext) + ": " + extracted.error);
            ensure(equalFiles(root / "inputs" / "sample.bin", out / "sample.bin"), "append preserved existing large file");
            ensure(equalFiles(root / "inputs" / "folder" / "another.txt", out / "folder" / "another.txt"), "append preserved existing directory");
            ensure(equalFiles(root / "new-entry.txt", out / "new-entry.txt"), "append added new file");
            // An existing path cannot silently be duplicated inside an archive.
            const auto collision = ArchiveEngine::modify(dest, {}, {root / "inputs" / "sample.bin"});
            ensure(!collision.ok, "append rejects duplicate entry path");
            const auto verify = ArchiveEngine::inspect(dest);
            ensure(verify.ok && verify.entries.size() == 4, "collision leaves existing archive unchanged");
            std::cout << "PASS safe append and collision guard " << ext << "\n";
        }

        // Modify in a streaming rewrite; original is replaced only after success.
        writeBytes(root / "replacement.txt", 1024);
        auto edit = ArchiveEngine::modify(root / "bundle.zip", {"folder"}, {root / "replacement.txt"});
        ensure(edit.ok, "modify ZIP: " + edit.error);
        auto updated = ArchiveEngine::inspect(root / "bundle.zip");
        ensure(updated.ok, "inspect modified ZIP: " + updated.error);
        bool sawReplacement = false, sawOld = false;
        for (const auto& entry : updated.entries) {
            sawReplacement |= entry.path == "replacement.txt";
            sawOld |= entry.path == "folder/another.txt";
        }
        ensure(sawReplacement && !sawOld, "streamed ZIP modification");
        auto editedExtract = ArchiveEngine::extract(root / "bundle.zip", root / "edited");
        ensure(editedExtract.ok && equalFiles(root / "replacement.txt", root / "edited/replacement.txt"), "modified data roundtrip");
        auto noOp = ArchiveEngine::modify(root / "bundle.zip", {}, {});
        ensure(!noOp.ok, "modify no-op rejected");
        std::cout << "PASS preview, streaming modification, re-extraction\n";

        auto stopped = ArchiveEngine::inspect(root / "bundle.zip", {.progress = {}, .cancelled = [] { return true; }, .passphrase = {}});
        ensure(!stopped.ok, "cancel inspect");
        makeMaliciousTar(root / "evil.tar");
        auto malicious = ArchiveEngine::extract(root / "evil.tar", root / "blocked");
        ensure(!malicious.ok, "block zip slip tar traversal");
        ensure(!fs::exists(root / "escape.txt"), "no extraction outside destination");
        std::cout << "PASS selection, cancellation, traversal protection, no-overwrite\n";
        // Progressive metadata indexing delivers small batches without retaining
        // every entry in the archive engine's result object.
        {
            std::size_t received = 0;
            std::size_t callbacks = 0;
            auto streamed = ArchiveEngine::inspectBatched(root / "bundle.zip",
                [&](std::vector<arky::ArchiveEntryInfo> batch) {
                    ensure(!batch.empty() && batch.size() <= 2, "bounded metadata batch");
                    received += batch.size();
                    ++callbacks;
                }, {}, 2);
            auto full = ArchiveEngine::inspect(root / "bundle.zip");
            ensure(streamed.ok && full.ok && streamed.entries.empty(), "streamed listing succeeds without retained results");
            ensure(received == full.entries.size() && callbacks >= 2, "all entries delivered in batches");
            ensure(!ArchiveEngine::inspectBatched(root / "bundle.zip", [](auto){}, {}, 0).ok,
                   "invalid batch size rejected");
            std::cout << "PASS bounded progressive archive metadata listing\n";
        }
        // Batch 4: extraction and integrity verification must bound decoded data.
        {
            arky::ExtractOptions limits;
            limits.maxEntryBytes = 16 * 1024;
            const auto dest = root / "limited-output";
            const auto rejected = ArchiveEngine::extract(root / "bundle.zip", dest, {}, {}, limits);
            ensure(!rejected.ok, "entry size limit rejects large decompression");
            ensure(!fs::exists(dest) || fs::is_empty(dest), "failed limited extraction rolls back output");
            arky::ExtractOptions limitedEntries;
            limitedEntries.maxEntries = 1;
            ensure(!ArchiveEngine::extract(root / "bundle.zip", root / "limited-entries", {}, {}, limitedEntries).ok,
                   "entry count limit enforced");
            arky::ReadLimits verifyLimits;
            verifyLimits.maxTotalBytes = 32 * 1024;
            ensure(!ArchiveEngine::test(root / "bundle.zip", {}, verifyLimits).ok,
                   "integrity verification also respects read budgets");
            arky::ExtractOptions ratio;
            ratio.maxExpansionRatio = 1;
            ratio.ratioCheckAfterBytes = 1;
            ensure(!ArchiveEngine::extract(root / "bundle.zip", root / "limited-ratio", {}, {}, ratio).ok,
                   "high expansion ratio rejected");
            std::cout << "PASS read limits, ratio guard, rollback and bounded integrity test\n";
        }
        // Batch 4: source file metadata must round-trip where the archive format
        // supports it; editing should not silently reset retained file metadata.
        {
            const auto metaSource = root / "inputs" / "metadata.sh";
            writeBytes(metaSource, 1024);
            const auto desiredTime = fs::file_time_type::clock::now() - std::chrono::hours(24 * 9);
            fs::last_write_time(metaSource, desiredTime);
#ifndef _WIN32
            fs::permissions(metaSource, static_cast<fs::perms>(0751), fs::perm_options::replace);
#endif
            for (const auto& ext : {".zip", ".tar.gz"}) {
                const auto path = root / (std::string("metadata") + ext);
                auto createdMeta = ArchiveEngine::create(path, {metaSource});
                ensure(createdMeta.ok, "create metadata fixture: " + createdMeta.error);
                arky::EditPlan editMeta;
                editMeta.createDirectories = {"extra"};
                auto changedMeta = ArchiveEngine::edit(path, editMeta);
                ensure(changedMeta.ok == (std::string(ext) == ".zip"), "only ZIP modification permitted");
                const auto output = root / (std::string("restored-meta") + ext);
                auto gotMeta = ArchiveEngine::extract(path, output);
                ensure(gotMeta.ok, "extract rewritten metadata fixture: " + gotMeta.error);
                const auto gotTime = fs::last_write_time(output / "metadata.sh");
                ensure(std::chrono::duration_cast<std::chrono::seconds>(
                          gotTime > desiredTime ? gotTime - desiredTime : desiredTime - gotTime).count() <= 4,
                       "preserve file mtime across creation/rewrite/extraction");
#ifndef _WIN32
                const auto mode = static_cast<unsigned>(fs::status(output / "metadata.sh").permissions()) & 0777U;
                ensure(mode == 0751, "preserve executable permission bits");
#endif
            }
            std::cout << "PASS file metadata preservation across archive edits\n";
        }
        // Verify comment fidelity and non-ZIP immutability.
        {
            const auto commented = root / "commented.zip";
            ensure(ArchiveEngine::create(commented, {root / "inputs" / "sample.bin"}).ok, "create comment fixture");
            int error = 0;
            auto* zip = zip_open(commented.string().c_str(), 0, &error);
            ensure(zip != nullptr, "open ZIP comment fixture");
            ensure(zip_set_archive_comment(zip, "NOTE", 4) == 0 && zip_close(zip) == 0, "write ZIP comment fixture");
            arky::EditPlan plan;
            plan.createDirectories = {"new"};
            ensure(ArchiveEngine::edit(commented, plan).ok, "edit commented ZIP");
            zip = zip_open(commented.string().c_str(), ZIP_RDONLY, &error);
            ensure(zip != nullptr, "reopen commented ZIP");
            int length = 0;
            const auto* comment = zip_get_archive_comment(zip, &length, 0);
            ensure(comment && length == 4 && std::string(comment, length) == "NOTE", "archive comment preserved");
            zip_discard(zip);
            for (const auto& ext : {".7z", ".tar.gz", ".tar.xz", ".tar.zst", ".tar"}) {
                const auto source = root / (std::string("bundle") + ext);
                auto before = root / "before";
                fs::copy_file(source, before, fs::copy_options::overwrite_existing);
                ensure(!ArchiveEngine::edit(source, plan).ok && equalFiles(source, before), "read-only archive unchanged");
            }
        }
#ifndef _WIN32
        // A symlink that appears in the extraction destination cannot redirect
        // writes outside the chosen root (openat + O_NOFOLLOW).
        {
            auto guarded = root / "guarded";
            auto outside = root / "outside";
            fs::create_directories(guarded);
            fs::create_directories(outside);
            fs::create_directory_symlink(outside, guarded / "folder");
            const auto rejected = ArchiveEngine::extract(root / "bundle.tar.gz", guarded, {"folder"});
            ensure(!rejected.ok, "reject destination symlink");
            ensure(fs::is_empty(outside), "symlink cannot redirect extraction");
            std::cout << "PASS anchored extraction destination rejects symlinks\n";
        }
#endif
        // Planned edits: subfolder adds, folder rename/move, replacement, and empty directories.
        for (const auto& ext : {".zip"}) {
            const auto target = root / (std::string("edit-plan") + ext);
            ensure(ArchiveEngine::create(target, {root / "inputs" / "sample.bin", root / "inputs" / "folder"}).ok,
                   "create planned-edit archive");
            fs::create_directories(root / "inputs" / "newsrc");
            writeBytes(root / "inputs" / "newsrc" / "new.txt", 30000);
            writeBytes(root / "inputs" / "updated.bin", 12000);
            arky::EditPlan plan;
            plan.renames.push_back({"folder", "moved/nested"});
            plan.createDirectories.push_back("empty/new-folder");
            plan.additions.push_back({root / "inputs" / "newsrc", "moved/imported", false});
            plan.additions.push_back({root / "inputs" / "updated.bin", "sample.bin", true});
            auto changed = ArchiveEngine::edit(target, plan);
            ensure(changed.ok, "planned edit " + std::string(ext) + ": " + changed.error);
            ensure(ArchiveEngine::test(target).ok, "test decoded edited archive");
            const auto listed = ArchiveEngine::inspect(target);
            ensure(listed.ok, "list planned edit");
            const auto output = root / "plan-extract" / std::string(ext+1);
            auto restored = ArchiveEngine::extract(target, output);
            ensure(restored.ok, "planned edit extraction: " + restored.error);
            ensure(equalFiles(root / "inputs" / "updated.bin", output / "sample.bin"), "replace file bytes");
            ensure(!fs::exists(output / "folder"), "renamed folder old name removed");
            ensure(equalFiles(root / "inputs" / "folder" / "another.txt", output / "moved/nested/another.txt"), "renamed subtree data");
            ensure(equalFiles(root / "inputs" / "newsrc" / "new.txt", output / "moved/imported/new.txt"), "nested import");
            ensure(fs::is_directory(output / "empty/new-folder"), "empty folder is materialized");
            arky::EditPlan collision;
            collision.renames.push_back({"moved/nested/another.txt", "sample.bin"});
            ensure(!ArchiveEngine::edit(target, collision).ok, "rename must reject collision");
            ensure(ArchiveEngine::test(target).ok, "failed edit retains working archive");
            arky::EditPlan deletion;
            deletion.removals = {"moved"};
            auto removed = ArchiveEngine::edit(target, deletion);
            ensure(removed.ok, "remove folder after rename: " + removed.error);
            const auto after = ArchiveEngine::inspect(target);
            ensure(after.ok, "inspect after subtree deletion");
            for (const auto& e : after.entries) ensure(!e.path.starts_with("moved/"), "subtree delete retains no descendants");
            std::cout << "PASS planned edits " << ext << "\n";
        }
        // Extraction policies: skip and rename preserve existing content.
        auto extractPolicyTarget = root / "extract-conflicts";
        fs::create_directories(extractPolicyTarget);
        writeBytes(extractPolicyTarget / "sample.bin", 50);
        arky::ExtractOptions skip;
        skip.existingFiles = arky::ExistingFilePolicy::Skip;
        auto skipped = ArchiveEngine::extract(root / "bundle.zip", extractPolicyTarget, {}, {}, skip);
        ensure(skipped.ok, "skip conflict: " + skipped.error);
        ensure(fs::file_size(extractPolicyTarget / "sample.bin") == 50, "skip must preserve existing");
        arky::ExtractOptions rename;
        rename.existingFiles = arky::ExistingFilePolicy::Rename;
        auto renamed = ArchiveEngine::extract(root / "bundle.zip", extractPolicyTarget, {}, {}, rename);
        ensure(renamed.ok, "rename conflict: " + renamed.error);
        ensure(fs::file_size(extractPolicyTarget / "sample.bin") == 50, "rename preserves existing");
        ensure(fs::exists(extractPolicyTarget / "sample (1).bin"), "rename creates alternative output");
        arky::ExtractOptions replace;
        replace.existingFiles = arky::ExistingFilePolicy::Overwrite;
        auto replaced = ArchiveEngine::extract(root / "bundle.zip", extractPolicyTarget, {"sample.bin"}, {}, replace);
        ensure(replaced.ok && equalFiles(root / "inputs" / "sample.bin", extractPolicyTarget / "sample.bin"), "overwrite confirmed");
        auto rollbackTarget = root / "extraction-rollback";
        fs::create_directories(rollbackTarget);
        writeBytes(rollbackTarget / "existing.txt", 40);
        std::uint64_t observed = 0;
        arky::Callbacks abort;
        abort.progress = [&](std::uint64_t amount, const std::string&) {observed = amount;};
        abort.cancelled = [&]() {return observed >= 64*1024;};
        auto aborted = ArchiveEngine::extract(root / "bundle.tar.gz", rollbackTarget, {}, abort);
        ensure(!aborted.ok, "extraction cancellation must fail");
        ensure(!fs::exists(rollbackTarget / "sample.bin"), "cancelled partial file cleaned");
        ensure(fs::exists(rollbackTarget / "existing.txt"), "existing files never removed on rollback");
        // Overwrite cancellation restores original bytes.
        writeBytes(rollbackTarget / "sample.bin", 123);
        observed = 0;
        auto rollbackOverwrite = ArchiveEngine::extract(root / "bundle.tar.gz", rollbackTarget, {}, abort, replace);
        ensure(!rollbackOverwrite.ok && fs::file_size(rollbackTarget / "sample.bin") == 123, "cancelled overwrite restores original");
        std::cout << "PASS extraction conflict and rollback policies\n";
        fs::remove_all(root);
        std::cout << "All archive core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        fs::remove_all(root);
        return 1;
    }
}
