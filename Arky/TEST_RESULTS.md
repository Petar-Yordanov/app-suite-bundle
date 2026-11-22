# Arky 0.7.5 — targeted Qt macro build fix

- PASS — portable archive-engine regression suite (`g++ -std=c++20 -O0 -g0 -Wall -Wextra -Werror`, libarchive 3.7.4).
- PASS — portable transfer-planning suite (`g++ -std=c++20 -O0 -g0 -Wall -Wextra -Werror`).
- PASS — `python3 scripts/source-check.py` including new Qt macro collision guards.
- PASS — source review: all `slots` occurrences in `ArchiveController::openUrl()` local declaration and lambda captures replaced with `batchPermits`.
- NOT RUN — Windows MSVC/Qt/QML build and Explorer tests. Qt SDK unavailable in this environment.

---

# Arky 0.7.4 — targeted compiler fix verification

- PASS — portable archive-engine tests (`g++ -std=c++20 -O0 -g0`, system libarchive).
- PASS — portable transfer-plan tests (`g++ -std=c++20 -O0 -g0`).
- PASS — `python scripts/source-check.py`.
- Source inspection confirmed exactly two `FORMATETC` brace initializers; both use explicit `CLIPFORMAT` casts.
- NOT RUN — MSVC compile, Qt/QML runtime, Windows Shell integration, Windows MIME tests.

---

# 0.7.3 native Windows MIME compile patch

- **PASS** — portable archive engine tests compiled and ran (GCC + libarchive).
- **PASS** — portable transfer planning tests compiled and ran.
- **PASS** — Qt/QML source consistency checker.
- **Added, not executed here** — Win32 clipboard format registration assertion in Qt MIME test.
- **NOT RUN** — MSVC/Qt Windows build; full Explorer virtual-file transfer tests. Qt and MSVC toolchains are not available in this environment.

# 0.7.2 MSVC fixes — additional local checks

- **PASS** — C++20 archive tests compiled and executed on Linux/GCC.
- **PASS** — C++20 transfer-planning tests compiled and executed on Linux/GCC.
- **PASS** — Simulated Windows `small` macro collision: compiled archive test with `-Dsmall=char` and ran the full suite.
- **PASS** — Qt/QML resource and bridge source consistency script.
- **NOT RUN** — Windows MSVC build; Qt headers/runtime unavailable here.

# Arky 0.7 / Batch 7 — test results (2026-11-13)

## Executed here (Linux / GCC / libarchive 3.7.4)

- **PASS** — Full archive-engine regression suite, including ZIP, 7z, TAR and CPIO.
- **PASS** — New ZIP/7z compression method and level tests, including ZIP local header verification for `store` and `deflate`.
- **PASS** — ZIP AES-128 and AES-256 creation, refusal without password and round-trip extraction with password.
- **PASS** — Creation capability checks and invalid option rejection, including unsupported 7z encryption and unknown output formats.
- **PASS** — Portable MIME/transfer plan tests.
- **PASS** — Archive and transfer tests with AddressSanitizer + UndefinedBehaviorSanitizer, leak checks enabled.
- **PASS** — QML resource/method consistency checker (not a real Qt build).
- **SKIP (capability-probed)** — Optional ZIP bzip2/lzma/xz/zstd and 7z zstd writer method tests on this environment's older libarchive; those methods are absent from the actual writer and will not be shown in the UI. Tests are performed when a newer compatible libarchive accepts them.

## Not executed here

- **Unverified** — Qt6/MSVC Windows compilation, QML runtime, Xmake Qt package resolution.
- **Unverified** — Windows Explorer virtual file drag/drop and clipboard integration.
- **Unsupported** — 7z encrypted writing, RAR creation, multi-volume writing, automatic external backend discovery.

## Build/test commands used

```bash
c++ -std=c++20 -O0 -g0 -Wall -Wextra -Werror -Isrc/core tests/archive_engine_tests.cpp src/core/archive_engine.cpp -larchive -o archive_tests
./archive_tests
c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -Isrc/core tests/transfer_plan_tests.cpp -o transfer_tests
./transfer_tests
python3 scripts/source-check.py
# Rebuilt and executed both suites with:
# -fsanitize=address,undefined -fno-omit-frame-pointer
# ASAN_OPTIONS=detect_leaks=1
```

## Earlier Batch 6 result for reference

# Arky 0.6 / Batch 6 — test results (2026-11-13)

## Executed here (Linux, GCC, system libarchive)

- **PASS** — archive engine full regression suite in normal mode.
- **PASS** — transfer planner suite with new Windows shell tree planning cases (including nested and empty directories, invalid DOS filenames, case collisions, and descriptor limit).
- **PASS** — both portable suites with AddressSanitizer + UndefinedBehaviorSanitizer, leak checks enabled.
- **PASS** — `scripts/source-check.py` validating QML resources and C++ bridge names.

## Authored but not executable here

- Windows `mime_tests`: virtual descriptor+stream for a ZIP member; folder/empty-directory descriptor attributes; streamed nested file bytes; indexed contents; Copy DropEffect.
- Windows MSVC/Qt compilation, Qt Quick startup, and actual Windows Explorer drag-out and clipboard paste.

**No Qt 6 SDK or MSVC is installed in this container.** A source-level/portable-suite PASS is not proof of Windows COM interoperability.

## Commands executed

```bash
c++ -std=c++20 -O1 -g -Wall -Wextra -Isrc/core tests/archive_engine_tests.cpp src/core/archive_engine.cpp -larchive -o archive_tests
./archive_tests
c++ -std=c++20 -O1 -g -Wall -Wextra -Isrc/core tests/transfer_plan_tests.cpp -o transfer_plan_tests
./transfer_plan_tests
python3 scripts/source-check.py
# Both suites recompiled and executed with:
# -fsanitize=address,undefined -fno-omit-frame-pointer
```

The Windows GitHub Actions CI workflow invokes `scripts/verify-windows.ps1`, which builds and executes these Windows-specific tests when run on the target platform. This workflow has not been executed here.

## 0.8.0 Findex visual system pass (2026-11-14)

- PASS — source-check.py including all 148 QML/resource entries and design-system icons.
- PASS — portable C++ archive engine regression suite (GCC, libarchive, zip/7z/tar etc).
- PASS — portable transfer-planning regression suite.
- NOT RUN — Windows Qt 6.9.1 GUI compile and QML runtime startup; Qt SDK unavailable in this environment.
- The C++ archive engine and native MIME code were not modified by this UI styling pass.

## Arky 0.8.2 rename verification

- Renamed Xmake project and Windows executable target to Arky.
- Rebranded Qt application name, startup diagnostics, title, deployment scripts, C++ namespace and custom MIME types.
- Preserved legacy metadata MIME alias for compatibility.
- GCC/libarchive portable archive regression suite: **PASS**.
- GCC transfer-planning suite: **PASS**.
- QML resource/source check (148 paths): **PASS**.
- Windows Qt GUI, MIME bridge and smoke tests: **not run in this environment**.
