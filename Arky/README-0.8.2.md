# Historical Arky 0.8.2 — Qt Quick / QML + libarchive

Arky is a crossplatform archive manager supporting a range of formats.

Refer to `RENAME_0.8.2.md` for the current project and executable names.

## Qt Quick startup fix (0.7.7)

- Removed the invalid `implicitHeight` assignment from the toolbar `Flow`; Qt Quick positioners compute their own implicit dimensions. Qt 6.9.1 rejected this assignment and aborted QML window creation.
- Startup dialogs now prioritize warning/error messages; full QML import traces remain in `%TEMP%\Arky-startup.log`.
- The source checker now detects assigning read-only implicit dimensions to Qt Quick positioners.
- The native archive and transfer tests passed after this change. GUI launch still needs target-OS verification.

## Windows Qt `slots` macro collision fix (0.7.5)

- `src/app/archive_controller.cpp`: renamed the semaphore variable `slots` to `batchPermits` and updated its lambda captures. Qt reserves `slots` as a macro, which caused MSVC C2513 and cascading syntax errors around the incremental archive loader.
- Added regression guards to `scripts/source-check.py` for Qt keyword macro collisions.
- Portable core tests passed; Windows Qt build and actual GUI startup still require verification.

## Windows MSVC narrowing-conversion fix (0.7.4)

- In `src/app/windows_virtual_mime.cpp`, two `FORMATETC` initializers previously passed `unsigned int` clipboard format IDs into `CLIPFORMAT` (`WORD`) fields via list initialization. MSVC reported C2397 at lines 232 and 235. Both initializers now explicitly use `static_cast<CLIPFORMAT>(...)`.
- Keeps format registration as `RegisterClipboardFormatA`, since Windows `CFSTR_FILEDESCRIPTORW` and `CFSTR_FILECONTENTS` names are narrow strings.
- The portable engine tests, transfer tests, and QML resource/source checker passed on Linux. MSVC/Qt/Explorer validation remains pending; follow the normal Windows build instructions below.

## Windows native MIME compile fixes (0.7.3)

- `src/app/windows_virtual_mime.hpp`: registered clipboard format IDs are stored as `unsigned int`, matching the `UINT` returned by the Win32 registration API, without depending on an unavailable `CLIPFORMAT` typedef in this Qt-facing header.
- `src/app/windows_virtual_mime.cpp`: uses `RegisterClipboardFormatA` for the standard Shell format-name constants `CFSTR_FILEDESCRIPTORW` and `CFSTR_FILECONTENTS`. Those constants are **narrow strings** despite the `W` in the Unicode payload format name; passing them to the `W` registration API failed MSVC compilation.
- `tests/mime_bridge_tests.cpp`: makes its Win32 include explicit and checks that both distinct clipboard formats were registered successfully.
- Retains the 0.7.1 and 0.7.2 MSVC fixes.

**Verification here:** portable archive-engine and transfer-planning suites passed, and the Qt/QML source checker passed. **Windows/MSVC compilation and Explorer clipboard/drag-and-drop remain unverified** until run on Windows.

## Windows MSVC compile fixes (0.7.2)

- `tests/archive_engine_tests.cpp`: renamed the `small` variable to `codecSourcePath`. Windows RPC/SDK headers can define `small` as a macro (expanding to `char`), which broke `const auto small` and triggered cascading MSVC errors.
- `src/app/export_store.cpp`: explicitly includes `<QUrl>` rather than relying on a forward declaration; `QMimeData::urls()` returns `QUrl` objects that require the complete definition.
- Preserves the prior 0.7.1 `NOMINMAX` macro guards and C++20 build settings.

**Tested here:** portable archive and transfer tests pass; a separate compile/run of the archive suite with `-Dsmall=char` also passes to simulate the Windows name collision. **Not tested here:** actual MSVC/Qt 6 GUI build and Explorer interoperability. On Windows, run `xmake -r` followed by `xmake run Arky` from an MSVC x64 shell.

## Verification boundary (2026-11-13)

The portable C++ archive engine and transfer-planning tests **compile and pass on Linux** using GCC and the installed libarchive. Both suites also pass local AddressSanitizer and UndefinedBehaviorSanitizer runs. **Qt 6 development packages and MSVC are unavailable in this environment**, so GUI compilation, QML runtime behavior, Qt MIME tests, Windows Explorer integration and Windows deployment remain **unverified**.

See `BATCH_7_STATUS.md`, `TEST_RESULTS.md` and earlier batch status files for detailed results and carryover limitations.

## What's new in Batch 7 — compression capability negotiation

- **Runtime format capabilities:** `ArchiveEngine::capabilities(filename)` describes writable formats, editability, multi-file support, compression-level support, encryption, and multi-volume limitations. ZIP and 7z codec options are checked using a temporary libarchive writer, so the UI doesn't advertise codecs missing from the installed library.
- **Selectable compression methods:** ZIP supports `store`, `deflate`, and optional `bzip2`, `lzma`, `xz`, `zstd` where the actual libarchive build accepts them. 7z supports `store`, `deflate`, `lzma1`, `lzma2` and optional `bzip2`/`zstd`. Tar filter formats retain their underlying compressor-level options. A 0 level on ZIP/7z with method `auto` explicitly selects **store**.
- **ZIP AES-128 / AES-256 creation:** Explicit modes are available if the installed crypto backend supports them; entering a password with auto mode selects AES-256. The ZIP format's password key derivation follows its established format specification; use a strong passphrase. Existing encrypted archive edits remain blocked.
- **Safer create options:** Unknown methods, incompatible compression levels, encryption without a password, and format/filename mismatches fail clearly. Compression remains streamed with bounded 64 KiB buffers. No claim of in-place modification.
- **Qt Quick:** The scrollable sidebar now contains the archive format, available compression methods, level, and supported ZIP encryption modes. Output format selection controls the native save-dialog filter.
- **Tests:** Added positive compression method round-trips, ZIP header method verification, both ZIP AES modes, and rejection tests for unsupported combinations. Optional methods are skipped only when the installed older libarchive reports them unavailable.

**Limitations:** libarchive still cannot create encrypted 7z or RAR archives or split/multi-volume archives here; these will require an additional backend, not just a new UI option. Some writer formats are read-only for editing. Windows Qt/MSVC compilation and Explorer MIME integration are still unverified.

## What's new in Batch 6 — Windows native MIME improvements

- **Native drag-out of folder trees:** Windows `FileGroupDescriptorW` now includes directory entries, including empty directories, with nested relative paths. Only regular file descriptors provide indexed `FileContents` streams; directory descriptors carry `FILE_ATTRIBUTE_DIRECTORY` without a fabricated stream.
- **Bounded streaming:** Existing libarchive-backed `IStream` provides 64 KiB chunks, seeking, and replay, without loading entire member payloads. Windows virtual transfers of more than 128 selected/tree entries instead use the existing disk-backed export mechanism to avoid costly repeated sequential archive scans.
- **Safer transfer planning:** Validates Windows-reserved device filenames, forbidden characters, dangerous path segments, case-insensitive collisions and a 2,048-descriptor upper bound before exposing a virtual transfer. Unsupported names fall back to staging, which may itself reject unrepresentable paths.
- **Copy virtual:** New Windows-only UI action exposes the live archive as Shell descriptors and streams, reducing temporary disk writes. Keep Arky running, and do not edit the source archive, until paste completes. **Copy files** remains the disk-backed, longer-lived option.
- **Additional Shell metadata:** Windows descriptors now include modification time when known, size for known regular files, and `Preferred DropEffect` = copy; custom Arky JSON metadata accompanies native formats.
- **Automated checks:** Portable unit tests cover folder trees, empty directories, collisions, reserved names and cap limits. Windows-only MIME tests now check directory descriptor attributes, stream indices and nested content bytes. These Windows tests are written but **not executed here**.

**Scope boundary:** This is outbound Windows Explorer support and staged standard `text/uri-list` imports. Incoming Outlook-style virtual attachments (`FileContents` from other apps), macOS file promises, and an actual Explorer drag/paste session remain unfinished or unverified. Qt/MSVC builds are still blocked in this Linux environment.

## What's new in Batch 5 — Windows build readiness

- **Fixed the old GUI smoke test:** The previous test incorrectly treated the first `archiveChanged` signal (emitted when the model was *cleared*) as a finished archive load. The new `--smoke-test` waits for the completed async operation and indexes the actual visible Qt model.
- **Extended end-to-end smoke scenario:** Through `ArchiveController`, it now opens a ZIP, tests integrity, extracts and byte-compares a file, renames it, copies it as MIME-backed real local-file URLs to the Qt clipboard, removes it, and creates an empty folder. This code is included but **not executed here**; it requires Qt and a GUI platform plugin.
- **Simplified Qt Quick Controls startup:** Replaced an explicit `QQuickStyle` C++ reference with a `qrc:/qtquickcontrols2.conf` resource. The QML plugin is still required, but the extra C++ `QtQuickControls2` linker dependency is no longer required.
- **Reproducible Windows CI configuration:** The workflow now provisions a Qt 6.9.1 MSVC2022 SDK using `install-qt-action` instead of assuming Qt was preinstalled on the runner. Xmake's Qt package is likewise pinned at 6.9.1 to avoid SDK/package version mismatches.
- **Source consistency checker:** `python scripts/source-check.py` validates QML resource paths, referenced controller methods and properties, moc declarations, and basic QML brace structure. It passed locally; it is *not* a QML parser or compiler.
- **MSVC verification script:** `scripts/verify-windows.ps1` defaults to Release, picks up `QT_ROOT_DIR` automatically, builds all targets, runs native and Qt MIME tests, then runs the QML end-to-end smoke test offscreen.

Your role on Windows remains just **build and launch**. Tests are encoded in the project and CI; you do not need to design or run manual feature test cases. There is no claim of a successful Windows build until one actually executes.

## What's new in Batch 4

- **Incremental archive listing:** `ArchiveEngine::inspectBatched` emits metadata in bounded groups (default 512). The Qt model appends them progressively with back-pressure limited to four pending batches. It ultimately retains an in-memory metadata index; it does **not** keep the archive contents in RAM.
- **Extraction safety limits:** configurable maximum decoded bytes per entry, total decoded bytes, entry count and an approximate expansion-ratio guard. The defaults are 64 GiB per entry, 256 GiB total, 2 million entries and 10,000:1 approximate ratio after 8 MiB. These defaults are caps, not claims of complete bomb protection.
- **Metadata preservation:** save source modification times and basic POSIX-style permission bits, and restore supported metadata during extraction. No claim of complete ACL, xattr, sparse-file, or filesystem metadata fidelity.
- **Conservative editing:** reject attempts to rewrite archives with unsupported extended metadata and ZIP archive comments rather than silently discard them; refuse encrypted-entry rewrites. Archive edits still re-encode entries and require free disk space and a backup.
- **Extraction safeguards:** guarded output paths, no-overwrite conflict strategies, rollback of newly created files on failure, and protections against existing destination symlinks. This has Linux tests, not a proof against all filesystem races on Windows.

## Features already present

| Feature | Status |
|---|---|
| Create/read/extract ZIP, 7z, TAR variants, CPIO and AR | Engine tests passed for supported subsets |
| Standalone GZip, BZip2, XZ and Zstd streams | Engine tests passed |
| Archive editing (add, remove, replace, rename/move, create folders) | Engine tests passed for ZIP, TAR.GZ, 7z, CPIO; Qt UI not compiled |
| ZIP AES-128 / AES-256 encrypted creation, encrypted ZIP extraction | Engine tests passed; encrypted editing intentionally blocked |
| Selective extraction, integrity check, bounded text preview | Engine tests passed |
| QML folder browsing and incremental list | Implemented in source; GUI not compiled |
| `QMimeData` clipboard, file URLs and custom JSON payloads | Implemented in source; Qt tests not run |
| Windows Shell `FileGroupDescriptorW`/`FileContents` file and folder drag | Expanded experimental Windows-only source; not built or Explorer-tested |
| macOS/Linux disk-backed drag fallback and export cache | Implemented in source; interoperability not tested |

## Windows setup and build

Open Administrator PowerShell:

```powershell
winget install --id Microsoft.VisualStudio.BuildTools --exact --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --norestart"
winget install --id Xmake-io.Xmake --exact
winget install --id Git.Git --exact
```

Select the **Desktop development with C++** workload (MSVC and Windows SDK) if the VS installer doesn't select it automatically.

Open **x64 Native Tools Command Prompt for VS 2022** and run from the project folder:

```bat
cl
xmake --version
xmake f -p windows -a x64 -m release
xmake -y
xmake run Arky
```

**Dependencies:** `xmake.lua` declares `qt6quick 6.9.1` (with transitive Qt QML packages) and `libarchive 3.8.9` with common compression filters. Xmake's Qt SDK recipe uses `aqtinstall` for Qt binary SDKs. Qt Quick Controls and Qt Quick Dialogs runtime modules must be present in that SDK. If a particular Qt installation lacks them, use the official Qt online installer with Qt Declarative / Qt Quick Controls 2 and configure `xmake f --qt=C:\path\to\Qt\6.x\msvc2022_64`. Avoid mixing MSVC and MinGW Qt binaries.

Qt runtime deployment on Windows requires `windeployqt` and may require copying additional non-Qt DLLs. After building, run:

```powershell
.\scripts\deploy-windows.ps1 -QtBin "C:\Qt\6.x\msvc2022_64\bin"
```

The deployment script is a starter, not an installer or guarantee of a standalone working binary. Use the MSVC Qt SDK matching your app's toolchain.

## Automated checks, without manual QA

When this source is pushed to GitHub, `.github/workflows/windows-qt-smoke.yml` is designed to provision MSVC, Qt, and Xmake, compile all targets, run native and MIME tests, and execute `Arky --smoke-test` through a software-rendered, offscreen Qt Quick window. **This workflow has not been run or validated against GitHub Actions in the present environment.** No connector is configured to submit or watch a build automatically.

You can also run the Windows check in one command, but it's optional and intended mainly for development/CI:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1 -Configuration release
```

## Limitations and remaining work

- **Platform validation:** run `scripts/verify-windows.ps1` with MSVC and Qt 6.9.1; actual Explorer drag/drop and GUI interactions still need automated Windows integration coverage. Qt/QML bindings need real compilation.
- **Incomplete format parity with KDE Ark:** no RAR creation, multi-volume handling, full archive comments/properties, alternative backends or universal encryption creation.
- **Metadata fidelity:** unsupported ACL/xattr/sparse/flags entries and ZIP comments block editing. Many format-specific properties (including compression methods and settings) are not guaranteed to survive a rewrite.
- **Filesystem races:** no guarantee against every concurrent directory replacement, Windows reparse-point/junction edge case or time-of-check/time-of-use attack. Do not run as administrator when inspecting untrusted archives.
- **Resource limits:** bounds are configurable and approximate, but decompressor implementations can allocate substantial additional memory, and the Qt model ultimately keeps metadata for all indexed entries. Multi-million-entry archives can still be expensive.
- **Native transfers:** outbound Windows virtual-file drag is experimental. Inbound Outlook-style virtual files, macOS file promises, Wayland edge cases and shell installation are unfinished.
- **App packaging:** no signed Windows/macOS installer or verified deployed bundle.

## Building and testing

```bat
xmake f -p windows -a x64 -m release
xmake -y
xmake run Arky
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1
```

In the source tree the core can be tested without Qt on Linux:

```bash
g++ -std=c++20 -O1 -Wall -Wextra -pedantic -Isrc/core \
  tests/archive_engine_tests.cpp src/core/archive_engine.cpp -larchive -o archive_tests
./archive_tests

g++ -std=c++20 -O1 -Wall -Wextra -pedantic -Isrc/core \
  tests/transfer_plan_tests.cpp -o transfer_plan_tests
./transfer_plan_tests
```

`xmake-requires.lock` is not included: dependency resolution has not run successfully in this container. Xmake generates it when resolving the packages. See `BATCH_6_STATUS.md` for the next milestones and current verification limits.

## Windows MSVC compatibility fix (0.7.1)

Defines `NOMINMAX` for Windows builds and guards standard `std::min` / `std::numeric_limits<...>::max` expressions against Windows macro expansion. This fixes MSVC `C2589` at `src/core/archive_engine.cpp` near the bounded preview read. No archive format behavior changes.


## Startup diagnostics (0.7.6)

If `xmake run Arky` compiles successfully but exits with code 1,
the `QQmlApplicationEngine` has failed to load `qrc:/qml/main.qml`.
On Windows, Arky now shows a native error dialog containing Qt's
QML error and writes `%TEMP%\Arky-startup.log` on every launch.
The log records the QML resource, import paths and required QML module
checks. On Windows it also finds the QML import directory beside the actually
loaded Qt6Core.dll when Xmake is using a cached Qt SDK. This is more reliable than `stderr` for a Windows GUI executable.

Run normally:

```powershell
xmake f -p windows -a x64 -m release -y
xmake -r
xmake run Arky
```

If it still fails, share the error shown or the content of:

```powershell
Get-Content (Join-Path $env:TEMP 'Arky-startup.log') -Tail 80
```

For verbose QML module resolution tracing, the executable accepts `--diagnose`:

```powershell
xmake run Arky --diagnose
```

QML modules must be installed using the same Qt version as the runtime.
Qt's `windeployqt --qmldir src\qml` can collect Qt Quick imports for deployment;
`xmake run` alone does not establish that those imports are installed.

**Verification:** The portable archive/transfer tests and source check passed
in this environment. This diagnostic patch has not been compiled on Windows.
