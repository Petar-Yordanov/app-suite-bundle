# Verification and performance

## Executed in the development environment

The standalone storage engine was compiled with GCC using C++20 and optimized builds. Its regression suite passed **61 assertions in 6 suites**. AddressSanitizer and UndefinedBehaviorSanitizer also passed with leak detection disabled because the execution sandbox prevents LeakSanitizer from inspecting the process.

Covered:

- Multi-format binary/rich-text persistence and restoration.
- Duplicate recency and retention.
- Actual file/directory snapshot independence.
- Count, disk and payload limits and protected active snapshots.
- KeePass/KeePassXC source-name and sensitive-MIME classification.
- Recovery from interrupted and corrupt storage, rejected links.

The current environment does not include the Qt 6 development SDK, QML toolchain, Xmake, or the required X11 development headers. The Qt application, QML integration test, and native OS adapters have **not** been compiled or run here. Windows, macOS and Linux GUI support therefore remains unverified. The implementation is delivered as source, not as a certified release build.

## Core benchmark

An optimized run on the available Linux host:

| Operation | Observed |
| --- | ---: |
| Empty store initialization | 0.045 ms |
| 1,000 distinct text entries (insert total) | 158.0 ms |
| Complete 1,000-entry search miss | 16.2 ms |
| Restore most recent text | 0.029 ms |
| Snapshot 64 MiB local file | 614.2 ms |
| Reopen 1,000-entry store | 38.9 ms |
| Peak process resident memory | 4,224 KiB |

The workload uses small distinct text entries and a freshly written local file with warm filesystem caches. The file snapshot triggers count-based eviction of one entry. This is a measurement of the standalone C++ store, **not the whole Qt application**, not its startup time, and not its idle RAM or CPU use. Results will vary with the target disk, compiler, filesystem and machine.

## Native acceptance checklist (requires target desktop)

Run this on Windows 11 x64/ARM64, macOS Intel/Apple silicon, Linux X11 and the intended Wayland compositors:

1. Build the app and run the Qt integration test, including the external writer with no QML window, and the copy after closing the window. Confirm QML loads and the tray/window/global shortcut operate. These added native regression tests are scheduled in CI; they have not run in this development environment.
2. Copy text, HTML, Unicode, images of different formats, files, and directories from other native applications. Restore and paste them into appropriate target applications.
3. Copy a file, wait for the entry to appear, remove the original, then restore. Verify the resulting paste retains the snapshot bytes, not a stale path.
4. Copy from real KeePass/KeePassXC installations using passwords, usernames and secret fields. Confirm there are no entries in history or new snapshot files and that normal paste works. Record any unmarked/obscured source limitations. Do not approve a platform for secret-handling use if these cases fail.
5. Configure small storage/count limits, copy an oversized item, and confirm normal paste is unaffected. Test protected snapshots, deduplication and persistence across restart.
6. Verify autostart enable/disable, login startup without loading QML, global shortcut conflicts, portable mode, and themes.
7. Configure a controlled HTTPS test release feed. Check correct and incorrect checksum handling, oversized package rejection, and opening the downloaded installer.
8. With history closed and the app idle, measure private/resident memory, CPU usage, wakeups and battery impact for at least 60 seconds. Repeat with 100 and 1,000 history entries and report median and peak. On Linux use `pidstat` and `/proc/<pid>/smaps_rollup`; use Task Manager/Performance Monitor on Windows and Activity Monitor on macOS. Compare cold and warm startup and history-window open latency.

These checks are required before claiming verified end-to-end behavior or whole-app performance on each OS. No whole-app measurements are fabricated.
