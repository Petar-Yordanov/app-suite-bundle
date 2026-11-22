#include "transfer_plan.hpp"
#include "windows_transfer_plan.hpp"
#include <iostream>
#include <stdexcept>

using namespace arky;
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static ArchiveEntryInfo file(std::string name) {
    ArchiveEntryInfo e;
    e.path = std::move(name);
    e.type = EntryType::Regular;
    return e;
}
static ArchiveEntryInfo folder(std::string name) {
    auto e = file(std::move(name));
    e.type = EntryType::Directory;
    return e;
}
int main() {
    try {
        auto items = planExports({file("src/a.txt"), file("docs/a.txt"), file("src/b.txt")});
        check(items.size() == 3, "Expected three roots");
        check(items[0].exportedName != items[1].exportedName, "Duplicate filename should be disambiguated");
        auto nested = planExports({file("a/readme.md"), folder("a"), file("b/readme.md"), folder("a/nested")});
        check(nested.size() == 2, "Directory selections should subsume child selection");
        auto mixed = planExports({file("x/README.TXT"), file("y/readme.txt")});
        check(mixed[0].exportedName != mixed[1].exportedName, "Case-insensitive copy targets must not collide");
        for (const auto& bad : {"../escape", "/absolute", "c:/device", "a//b", "a/./b"}) {
            bool rejected = false;
            try { (void)planExports({file(bad)}); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "Unsafe exported path was accepted");
        }
        // File descriptors include selected folder descendants and preserve unique
        // Shell-relative names even when the original paths overlap.
        auto tree = planWindowsTransfer({folder("assets"), file("docs/note.txt")},
            {folder("assets"), folder("assets/empty"), folder("assets/nested"),
             file("assets/nested/file.txt"), file("docs/note.txt")});
        check(tree.size() == 5, "Missing folder descendants in transfer manifest");
        check(tree[0].shellPath == "note.txt" || tree[0].shellPath == "assets", "Invalid root export");
        bool foundNested = false, foundEmpty = false;
        for (const auto& entry : tree) {
            if (entry.shellPath == "assets\\nested\\file.txt") foundNested = true;
            if (entry.shellPath == "assets\\empty" && entry.entry.type == EntryType::Directory) foundEmpty = true;
        }
        check(foundNested && foundEmpty, "Folder tree was flattened or empty folders lost");
        for (const auto& name : {"CON", "nul.txt", "LPT9.prn", "a?b.txt", "tail.", "trail "}) {
            check(!safeWindowsName(name), "Invalid Windows Shell filename accepted");
        }
        for (const auto& name : {"normal.txt", "archive.tar.gz", "café.txt", "COM10.txt"}) {
            check(safeWindowsName(name), "Valid Windows Shell filename rejected");
        }
        bool unsafe = false;
        try { (void)planWindowsTransfer({folder("root")},
            {folder("root"), file("root/CON.txt")}); }
        catch (const std::invalid_argument&) { unsafe = true; }
        check(unsafe, "Reserved Windows device name was accepted");
        bool duplicate = false;
        try { (void)planWindowsTransfer({folder("root")},
            {folder("root"), file("root/File.txt"), file("root/file.txt")}); }
        catch (const std::invalid_argument&) { duplicate = true; }
        check(duplicate, "Case-insensitive Windows path collision was accepted");
        bool tooMany = false;
        try { (void)planWindowsTransfer({folder("root")},
            {folder("root"), file("root/1"), file("root/2")}, 2); }
        catch (const std::invalid_argument&) { tooMany = true; }
        check(tooMany, "Virtual file descriptor count limit was bypassed");
        std::cout << "PASS transfer export planning, directories, collisions and unsafe paths\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
