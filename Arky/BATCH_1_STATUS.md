# Arky 0.3 — Batch 1 checkpoint

## Validated here

- Built `src/core/archive_engine.cpp` plus `tests/archive_engine_tests.cpp` with GCC C++20 and system libarchive 3.7.4 on Linux.
- Ran tests successfully on 2026-11-13. Passing coverage includes archive create/extract, ZIP AES, append, archive-edit plans for ZIP / TAR.GZ / 7z / CPIO, extraction conflicts and rollback handling, path traversal protection, and cancellation.
- Only the portable C++ engine test executable was compiled and run. Qt 6 development headers were unavailable.

## Source changes present, but not UI-verified

- Rename and move entries, add folders and empty directories, replace files.
- QML folder navigation, edit dialogs, and extraction conflict selector.
- Copy selected archive files to clipboard after staging actual files to disk.
- Rewrite verification and backup preservation.

## Not validated / incomplete

- Actual Windows MSVC + Xmake compilation, Qt/QML runtime, Qt MIME tests, GUI interaction, cross-platform integration.
- Native Windows virtual-file drag-out / FileGroupDescriptorW and macOS file promises.
- Complete fidelity of archive metadata, compression methods, encrypted archive rewriting.
- Security guarantees against all filesystem races and malicious archive variants.
- Distribution/installers and a comprehensive format compatibility matrix.

## How to run core tests (Linux)

```sh
g++ -std=c++20 -O1 -Wall -Wextra -pedantic src/core/archive_engine.cpp tests/archive_engine_tests.cpp -Isrc/core -larchive -o archive_tests
./archive_tests
```

## Windows build attempt

From the x64 Native Tools Command Prompt with Xmake installed:

```bat
xmake f -p windows -a x64 -m debug
xmake -y
xmake run archive_tests
xmake run mime_tests
xmake run Arky
```

This is a **source checkpoint**, not a confirmed working Windows build.
