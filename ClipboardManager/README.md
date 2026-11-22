# ClipDeck — native clipboard manager

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

ClipDeck is a C++20, Qt 6 Quick/QML desktop clipboard manager. It keeps clipboard history on the local machine, without accounts, telemetry, synchronization, or pinned entries.

## Build

Use Qt **6.8 or newer** (Core, Gui, Widgets, Qml, Quick, Quick Controls, Quick Layouts and Network), Xmake, and a C++20 toolchain. The Qt installation must include its development tools and QML modules, not merely runtime libraries.

| Platform | Toolchain |
| --- | --- |
| Windows 11 x64/ARM64 | Visual Studio 2022 C++ workload and an architecture-matched Qt SDK |
| macOS Intel/Apple silicon | Xcode command-line tools and an architecture-matched Qt SDK |
| Linux x86-64/ARM64 | GCC 12+ or Clang 16+, Qt 6 SDK, and X11 development headers (`libx11-dev` on Debian/Ubuntu, `libX11-devel` on Fedora) |

From this directory:

```sh
xmake f -m release --desktop=y --qt=/path/to/Qt/6.8.x/your-kit
xmake build clipdeck
xmake run clipdeck
```

On Windows use the appropriate MSVC Qt kit (not the MinGW kit if building with MSVC), e.g. `xmake f -p windows -a x64 -m release --desktop=y --qt="C:/Qt/6.8.3/msvc2022_64"`. On ARM64 select the corresponding SDK and Xmake architecture (`arm64` for Apple silicon, `arm64` for the MSVC target, `arm64` or the native architecture on Linux). Building natively per OS and architecture is recommended.

The executable/bundle is under Xmake's build output directory. For distribution, use Qt's `windeployqt --qmldir UI <executable>` on Windows or `macdeployqt <app-bundle> -qmldir=UI` on macOS. On Linux provide the matching Qt runtime and QML packages or bundle them in a distribution-specific package. This source archive does not include Qt, signed installers, release hosting, or prebuilt binaries.

## Use

- Run normally to open history. Run with `--background` to start without loading QML when the tray exists.
- Copy normally. Use the tray icon or the default Ctrl+Shift+V shortcut to open history. Change the global shortcut in Settings. On macOS Qt's portable Ctrl notation maps to Command.
- Select **Copy** on an entry to restore it to the system clipboard, then paste normally using the target application's usual command.
- Search history, delete an entry, clear history, or pause capture. Rich text, image formats, opaque MIME payloads, and local copied files/directories are supported when exposed by Qt/native clipboard APIs.
- On the first launch, the app attempts to register itself for login startup. Disable that setting before uninstalling or relocating the app.
- Use `--portable` for a self-contained data directory (`ClipDeckData` next to the executable) on writable installations.
- Use `--show` to open an existing instance. On Wayland, assign that command to a compositor-managed global shortcut where supported. If using portable mode, also pass `--portable`.

Default retention is 100 entries and 512 MiB, configurable to 1–10,000 entries and 1–8,192 MiB. The oldest entries are evicted when necessary, except a file snapshot currently on the clipboard. Individual captures that cannot fit or cannot be safely snapshotted are skipped, not removed from the system clipboard.

## Architecture

- `Core/Store.*`: standalone C++20 storage engine. Each entry is one independently committed directory containing a small manifest, original MIME payloads, and actual filesystem snapshots. There is no SQLite dependency, no database containing all payloads, and no need to load archived binary data at startup.
- `App/Controller.*`: Qt clipboard observation, a dedicated storage worker, capture limits, debounced search, settings, tray, restore actions, and lazy QML initialization. Closing the interface destroys its QML engine; the clipboard service stays active.
- `Platform/Native.*`, `Mac.mm`: native hotkeys, source/marker detection, and startup integration. Windows uses Win32 hotkeys and the per-user Run registry key; Linux X11 uses XGrabKey and XDG autostart; macOS uses Carbon hotkeys and a per-user LaunchAgent.
- `UI/Main.qml`: compact Qt Quick history list and settings; light, dark and system color schemes.
- `App/Updater.*`: opt-in HTTPS update feed, scheduled checks and checksum-verified package downloads.

The storage worker streams file copies and full-text search with 64 KiB buffers. The capture queue allows two pending captures and caps each non-file MIME payload collection at 32 MiB. File trees have a 10,000-node limit. Disk accounting includes payloads, metadata and a conservative per-filesystem-node allowance. A staging directory is renamed after a complete capture; incomplete staging directories are cleaned on restart. Disk usage accounting is an application quota, not a guarantee of exact filesystem block allocation or physical storage consumption.

Deduplication fingerprints the ordered MIME bytes and snapshot paths/content; duplicates move to the newest position. File captures are snapshotted independently of their original paths. Restoring files publishes new URLs referring to the stored copies, retaining the original filenames and directory structure. The currently restored file snapshot is protected from history eviction while the clipboard refers to it. Replacing the clipboard releases that protection. A copied file must be readable when captured; moving or deleting it before the asynchronous snapshot has completed can cause capture to fail. Symlinks, special files, virtual file promises and mixed remote/local file selections are deliberately rejected.

## Security and platform limitations

Clipboard history is private to the user account (owner-only storage directory permissions on Unix; inherited user-directory ACLs on Windows). It is **not encrypted**. Do not use portable history on a shared or untrusted disk. Clearing history and disabling persistence are ordinary file deletions, not forensic erasure.

ClipDeck checks clipboard sensitivity markers before saving payloads, including common KeePass/KeePassXC markers, Windows monitor/history/roaming exclusion formats, and concealed/transient pasteboard markers. Identifiable KeePass and KeePassXC process names are also excluded. Clipboard contents from these sources are not intentionally changed or cleared. The same marker checks are repeated in the core and immediately before submitting captures to the worker.

No cross-platform clipboard API provides an authenticated, universal identity for the original source application. A password manager or another application may emit content without a detectable marker, or a compositor/clipboard proxy may obscure the original writer. **Exclusion cannot be guaranteed for unmarked clipboard content with an unidentifiable source.** Use password managers that emit sensitive-content hints; if exclusion is essential and the source cannot be detected, pause capture before copying secrets.

| Platform | Limitations |
| --- | --- |
| Windows | Source detection uses clipboard owner and foreground process IDs. Clipboard notifications are backed by a 250 ms coarse change-counter poll. Delayed rendering and temporarily locked clipboards receive bounded retries. Explicit history/cloud opt-out flags are excluded; a validated opt-in flag is not itself sensitive content. Native private formats may lack a Qt MIME conversion. |
| macOS | Background capture uses a 250 ms coarse pasteboard change-count poll; short-lived changes between polls may be missed. Frontmost-app identity is a heuristic, not the guaranteed pasteboard writer. macOS privacy restrictions may require user permission. |
| Linux X11 | Selection owner/focus/window PID hints can be absent; clipboard managers may proxy selections. X11 short-lived owners can disappear before data is read. |
| Linux Wayland | Background clipboard observation, global hotkeys and source detection are compositor-dependent. Capture may only work while focused or where the compositor provides access. The compositor-managed shortcut is the documented fallback. |

Original MIME bytes are retained for supported formats. Qt's internal image objects are represented using lossless PNG in addition to exposed image MIME data; native file-transfer formats must be reconstructed with new paths rather than replaying the original path or move instructions. Unsupported native/private formats, provider-dependent objects, and images or MIME payloads exceeding the capture limits may not be recorded. Qt can allocate a clipboard value while retrieving it, before ClipDeck can measure its size; therefore the application-level limits do not bound a malicious clipboard provider's transient platform allocation. Session-only history uses temporary on-disk snapshots while running; it is removed on normal shutdown and cleaned on the next launch after a crash.

## Updates

No release server or signing keys are included. Supply a trusted HTTPS JSON manifest in Settings. Once configured, checks run on launch and every six hours. An available update is downloaded automatically, capped at 256 MiB, verified against its SHA-256 checksum and offered for installation. Installation requires opening the downloaded package; there is no silent self-replacement. Use platform-signed packages and a trusted manifest host in production. HTTPS and a checksum alone are not a substitute for package signing.

Example manifest (replace URLs and checksums with real release artifacts):

```json
{
  "version": "1.1.0",
  "artifacts": {
    "windows-x86_64": {
      "url": "https://your-release-host.example/ClipDeck-1.1.0.msi",
      "filename": "ClipDeck-1.1.0.msi",
      "sha256": "REPLACE_WITH_64_HEX_DIGITS"
    }
  }
}
```

Artifact keys use `windows`, `macos`, or `linux` followed by Qt's CPU architecture string (typically `x86_64` or `arm64`).

## Tests

```sh
xmake build clipdeck-tests
xmake run clipdeck-tests
xmake build clipdeck-integration-tests
xmake run clipdeck-integration-tests
xmake build clipdeck-bench
xmake run clipdeck-bench
```

The Qt integration test uses an isolated temporary history, disables autostart, verifies captures with no QML window (including a separate clipboard writer on native desktop platforms), closes and reopens the real QML UI, captures/restores rich text and binary MIME, checks a sensitive marker, and tests restoration of an independent file snapshot. It changes the test session's clipboard; run it in an isolated session. The CI workflow uses native desktop backends (Xvfb/X11 on Linux). For a headless Qt test on Linux, use the offscreen plugin and software rendering:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software xmake run clipdeck-integration-tests
```

Core-only builds (no Qt SDK needed):

```sh
xmake f -m release --desktop=n
xmake build clipdeck-tests
xmake run clipdeck-tests
xmake build clipdeck-bench
xmake run clipdeck-bench
```

Or, to test the independent storage engine with a C++ compiler:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -ICore Core/Store.cpp Tests/StoreTests.cpp -o clipdeck-tests
./clipdeck-tests
g++ -std=c++20 -O2 -ICore Core/Store.cpp Benchmarks/StoreBench.cpp -o clipdeck-bench
./clipdeck-bench
```

See [VERIFICATION.md](VERIFICATION.md) for measured results, verification boundaries, and remaining target-platform acceptance checks.
