#include "archive_engine.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
volatile std::sig_atomic_t interrupted = 0;
void stop(int) { interrupted = 1; }
std::filesystem::path path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
int run(std::vector<std::string> args) {
    if (args.empty() || args.front() == "--help") {
        std::cout << "arky-cli list|open ARCHIVE [--password-stdin] [--volume FILE ...]\n"
                     "arky-cli extract ARCHIVE DEST [--entry PATH ...] [--conflict fail|skip|rename|overwrite]\n"
                     "arky-cli create ARCHIVE SOURCE ... [--level 0..9] [--threads 1..16]\n"
                     "arky-cli test ARCHIVE [--password-stdin] [--volume FILE ...]\n"
                     "arky-cli add ARCHIVE SOURCE ... | remove ARCHIVE ENTRY ...\n"
                     "arky-cli rename ARCHIVE FROM TO\n"
                     "Read passwords from stdin (never from command-line arguments).\n"
                     "Use -- before positional names beginning with --.\n";
        return args.empty() ? 2 : 0;
    }
    const auto command = args.front();
    std::vector<std::string> positional, entries;
    arky::Callbacks cb;
    arky::CreateOptions create;
    arky::ExtractOptions extract;
    cb.cancelled = [] { return interrupted != 0; };
    bool options = true, password = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        auto value = [&]() { if (++i >= args.size()) throw std::runtime_error("Missing option value"); return args[i]; };
        if (options && arg == "--") options = false;
        else if (options && arg == "--password-stdin") password = true;
        else if (options && arg == "--volume") cb.volumes.push_back(path(value()));
        else if (options && arg == "--entry") entries.push_back(value());
        else if (options && arg == "--level") create.compressionLevel = std::stoi(value());
        else if (options && arg == "--threads") create.threads = static_cast<unsigned>(std::stoul(value()));
        else if (options && arg == "--conflict") {
            const auto mode = value();
            if (mode == "skip") extract.existingFiles = arky::ExistingFilePolicy::Skip;
            else if (mode == "rename") extract.existingFiles = arky::ExistingFilePolicy::Rename;
            else if (mode == "overwrite") extract.existingFiles = arky::ExistingFilePolicy::Overwrite;
            else if (mode != "fail") throw std::runtime_error("Unsupported conflict policy");
        } else if (options && arg.starts_with("--")) throw std::runtime_error("Unknown option");
        else positional.push_back(arg);
    }
    if (positional.empty()) throw std::runtime_error("Archive path required");
    if (password && command == "create") throw std::runtime_error("Encrypted creation is not supported");
    if (password && !std::getline(std::cin, cb.passphrase)) throw std::runtime_error("No password provided on stdin");
    const auto archive = std::filesystem::absolute(path(positional.front()));
    if (!cb.volumes.empty()) cb.volumes.insert(cb.volumes.begin(), archive);
    const auto start = std::chrono::steady_clock::now();
    arky::Result result;
    std::vector<std::filesystem::path> sources;
    for (std::size_t i = 1; i < positional.size(); ++i) sources.push_back(path(positional[i]));
    if (command == "list" || command == "open") {
        result = arky::ArchiveEngine::inspectBatched(archive, [](std::vector<arky::ArchiveEntryInfo> batch) {
            for (const auto& entry : batch)
                std::cout << (entry.sizeKnown ? std::to_string(entry.size) : "?") << '\t' << entry.path << '\n';
        }, cb);
    } else if (command == "extract" && sources.size() == 1)
        result = arky::ArchiveEngine::extract(archive, sources.front(), entries, cb, extract);
    else if (command == "create" && !sources.empty())
        result = arky::ArchiveEngine::create(archive, sources, cb, create);
    else if (command == "test" && sources.empty())
        result = arky::ArchiveEngine::test(archive, cb);
    else if (command == "add" && !sources.empty())
        result = arky::ArchiveEngine::modify(archive, {}, sources, cb);
    else if (command == "remove" && !sources.empty())
        result = arky::ArchiveEngine::modify(archive, {positional.begin()+1, positional.end()}, {}, cb);
    else if (command == "rename" && positional.size() == 3) {
        arky::EditPlan plan;
        plan.renames.push_back({positional[1],positional[2]});
        result = arky::ArchiveEngine::edit(archive, plan, cb);
    } else throw std::runtime_error("Invalid command or arguments; use --help");
    for (const auto& warning : result.warnings) std::cerr << "Warning: " << warning << '\n';
    if (!result.ok) { std::cerr << result.error << '\n'; return 1; }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::cerr << "OK: " << result.filesProcessed << " entries, " << result.bytesProcessed << " bytes, " << ms << " ms\n";
    return 0;
}
}
int main(int argc, char** argv) {
    std::signal(SIGINT, stop);
    try {
        std::vector<std::string> arguments;
#ifdef _WIN32
        int n = 0;
        auto wide = CommandLineToArgvW(GetCommandLineW(), &n);
        if (!wide) return 2;
        for (int i=1; i<n; ++i) {
            auto value = std::filesystem::path(wide[i]).u8string();
            arguments.emplace_back(value.begin(), value.end());
        }
        LocalFree(wide);
#else
        for (int i=1; i<argc; ++i) arguments.emplace_back(argv[i]);
#endif
        return run(std::move(arguments));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
