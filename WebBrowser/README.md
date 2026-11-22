# WebBrowser

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

Native C++20 / Qt Quick browser. The desktop engine is Qt WebEngine (Chromium); iOS uses the system WKWebView via Objective-C++ and a native view embedded in Qt Quick. The browser chrome, tabs, preferences, history, bookmarks, and download model are shared. No engine is reimplemented.

## Build locally

Install Xmake, a C++20 compiler, and **Qt 6.11.3 or a newer compatible patched Qt 6.11 release**. Desktop kits must include Qt WebEngine, WebChannel, Positioning, Quick Controls, SQL and QSQLite. Qt 6.8.3 used by the other apps is not sufficient for this project: the public Quick profile and strict persistent-cookie policy are newer APIs. The CI jobs are independent to keep other applications unchanged.

Windows 11 x64 (PowerShell with Visual Studio 2022 installed):

```powershell
cd WebBrowser
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=C:\Qt\6.11.3\msvc2022_64"
xmake build -y webbrowser
xmake run webbrowser
```

macOS Apple Silicon:

```bash
cd WebBrowser
xmake f -c -y -p macosx -a arm64 -m release --qt="$HOME/Qt/6.11.3/macos"
xmake build -y webbrowser
xmake run webbrowser
```

Linux x64 (Ubuntu 24.04 or compatible distro with Qt 6.11 kit):

```bash
cd WebBrowser
xmake f -c -y -p linux -a x86_64 -m release --qt="$HOME/Qt/6.11.3/gcc_64"
xmake build -y webbrowser
xmake run webbrowser
```

Use the actual Qt SDK directory on your machine. macOS Intel and Linux ARM64 can use the corresponding Qt WebEngine kit where available, but are not part of the CI matrix. Windows ARM64 support depends on availability of a native Qt WebEngine build; the x64 artifact may run under Windows ARM emulation. No native Windows ARM64 artifact is promised.

### iOS (minimum iOS 17)

Install Qt's **iOS** kit and matching host desktop kit on a Mac with supported Xcode. Xmake orchestrates the build via Qt's CMake/Xcode static-QML plugin linker (normal desktop builds use Xmake directly).

```bash
cd WebBrowser
xmake ios-bundle --qt="$HOME/Qt/6.11.3/ios"
open -a Simulator
xcrun simctl list devices booted
xcrun simctl install booted "$(find build/ios-simulator -type d -name webbrowser.app -print -quit)"
xcrun simctl launch booted org.appsuite.webbrowser
```

For an iPhone device, use `xmake ios-bundle --device --qt=...`, open the generated Xcode project under `build/ios-device`, configure an Apple development team, sign and run on your device. The unsigned simulator ZIP from CI cannot be installed on a physical iPhone. iOS updates use the distribution mechanism, not the desktop update feed.

## Build tests and installers

```bash
xmake build -y webbrowser-tests
xmake build -y webbrowser-store-tests
xmake build -y webbrowser-bench
xmake run webbrowser-tests
xmake run webbrowser-store-tests
xmake run webbrowser-bench
xmake run webbrowser --smoke-test
xmake run webbrowser --compatibility-smoke
```

The smoke test uses an isolated profile and a loopback HTTP server, verifies regular cookie reuse, private cookie isolation and disposal of the last private session, and checks private history isolation. Run with a real graphical session (Linux CI uses Xvfb/GL; never disable Chromium sandbox for normal browsing).

For Netflix, Discord and other website compatibility, run `xmake run webbrowser --compatibility-test`. See [COMPATIBILITY.md](COMPATIBILITY.md) for Widevine configuration, a dedicated codec-enabled Qt build, real local audio/video and WebRTC diagnostics, and manual live-site acceptance checks. Desktop packaging also executes `--compatibility-smoke`.

Package with `QT_ROOT_DIR` pointing to the same SDK, using:

- Windows: `./Packaging/package-windows.ps1` (Inno Setup and MSVC runtime).
- Linux: `bash Packaging/package-linux.sh` (patchelf, dpkg-deb, xvfb and runtime libraries).
- macOS: `bash Packaging/package-macos.sh` (macdeployqt, codesign, productbuild).

CI uploads direct EXE, DEB and PKG artifacts, plus a separate iOS simulator ZIP. macOS is ad-hoc signed, not notarized. Windows is not Authenticode signed. Set `PORTABLE=1` when packaging locally for an optional portable bundle. Use `--portable` or a sibling `portable.ini` (writable directory required) to keep data next to the executable. For macOS portable data can instead use `--data-dir /path/to/data`.

## Usage

| Shortcut | Action |
|---|---|
| Ctrl+T | New tab |
| Ctrl+Shift+N | New private tab |
| Ctrl+W | Close tab |
| Ctrl+Shift+T | Reopen last closed regular tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Switch tabs |
| Ctrl+L | Address bar |
| Ctrl+R | Reload |
| Alt+Left / Alt+Right | Back / forward |
| Ctrl+D | Add bookmark |
| Ctrl+H | History |
| Ctrl+J | Downloads |
| F11 | Fullscreen |

All listed shortcuts can be changed in Settings. Ctrl maps to Command on macOS according to Qt shortcut conventions. The tab bar's arrows reorder the selected tab. Use P+ for private tabs. Bookmarks have folders, parent selection on create/edit, recursive deletion with confirmation, JSON and Netscape HTML import/export. Search URLs must include a literal `%s` placeholder. Downloads save only after an explicit destination choice (on iOS to Files > On My iPhone > WebBrowser > Downloads). Download records are session-only, with pause/restart supported where the engine returns resume capability.

Regular cookies and site data are stored by the engine. Websites control login expiration; the app cannot guarantee that a particular login persists. On desktop the optional session-cookie setting is separate from persistent cookies, open tabs, selected tab and recently closed tabs. On iOS WebKit controls session cookie lifetimes. Private tabs share a separate in-memory engine data store, never write browser history/session records, and are not reopened after closing. Explicitly saved files and bookmarks remain by user choice. Private browsing is not a network anonymity feature.

The ad blocker uses an editable small hostname blocklist with third-party filtering and allowlists. It does not implement cosmetic rules, scriptlets, extension compatibility, or reliable YouTube/in-stream ad blocking. iOS WebKit rules may differ in third-party determination or supported regex/list size. A site can be allowlisted if it breaks.

User-initiated site popups can open in a new tab; automatic script popups are disabled by default. The setting can be enabled if a sign-in flow requires them. Private popups stay private.

## Compatibility and security limitations

- Qt WebEngine uses Chromium, not Chrome's complete distribution. Test Discord (including actual call audio/video), Office 365 sign-in/documents, YouTube and YouTube Music with your accounts on each OS. Camera/microphone requests require an affirmative origin-specific grant plus the OS permission. Desktop screen-sharing requests have a source picker; availability varies on Wayland/macOS.
- Qt WebEngine's licensed proprietary audio/video codecs are dependent on the Qt build. H.264/AAC may require building Qt WebEngine with `-webengine-proprietary-codecs`. The default CI uses official Qt packages, without claiming those codecs are present.
- Netflix additionally needs a compatible CDM and codec combination. Widevine is not bundled or redistributed. A legitimately installed compatible Widevine CDM can be supplied to Qt WebEngine via its documented `--widevine-path` Chromium flag, but playback can still be rejected by Netflix. No DRM bypass, spoofing or guarantee of playback. iOS uses the OS WebKit media support and service restrictions.
- iOS does not support desktop Chromium extensions, Qt WebEngine cookie controls, desktop screen capture, or overnight downloads when the OS suspends the app. WKWebView is a native overlay: the Qt side hides it when browser panels are open. iOS media, popup sign-in, downloads and Files integration require hardware testing.
- TLS certificate errors are rejected. Do not disable Chromium sandbox; resolve platform sandbox setup instead. No developer tools UI or password manager. No app telemetry or account. Qt/Chromium/WebKit still have their own platform and networking behaviors.
- The update checker is opt-in: configure a trusted HTTPS JSON manifest matching Packaging/release-example.json. It checks automatically every six hours, downloads a checksum-verified package in a dedicated update directory, and opens the installer only after explicit user action. Release hosting and signing/notarization are not provided by CI artifact uploads.
- Keep Qt WebEngine updated promptly, because Chromium is security-sensitive. Do not use a frozen Qt build indefinitely.

To publish updates, set `APP_VERSION` to the same major.minor.patch version before Xmake configuration and packaging, upload the three desktop installers to a trusted HTTPS release directory, and generate the feed using `python Packaging/make-release-manifest.py --version 1.0.1 --base-url https://your-host.example/releases/1.0.1 --artifacts artifacts > release.json` (Python 3.11+). Publish the resulting JSON and configure its URL in Settings. HTTPS and the manifest hash protect downloads from corruption, not a compromised feed publisher; use a trusted host and signed packages for distribution.

See [REQUIREMENTS.md](REQUIREMENTS.md) for the full requirement-by-requirement audit.

Primary references:

- https://doc.qt.io/qt-6/qquickwebengineprofile.html
- https://doc.qt.io/qt-6/qtwebengine-features.html
- https://doc.qt.io/qt-6/qtwebengine-platform-notes.html
- https://doc.qt.io/qt-6/qtwebengine-deploying.html
- https://developer.apple.com/documentation/webkit/wkwebview

## Benchmarks

The included C++ microbenchmark measures host rule compilation/matching. For real browser memory/CPU comparisons, install psutil in a Python environment and run the same local page workload with independent profiles:

```bash
python -m pip install psutil
python Benchmarks/browser.py --exe /path/to/webbrowser --tabs 5 --seconds 30
python Benchmarks/browser.py --exe /path/to/webbrowser --compare-exe /path/to/chrome \
  --compare-arg='--user-data-dir={profile}' --compare-arg=--no-first-run \
  --compare-arg=--disable-extensions --tabs 5 --seconds 30
```

The script counts the complete process tree, not only browser chrome. RSS can count shared pages more than once; USS is included where supported. Startup marker is UI construction, not first contentful paint; comparison browser startup is not instrumented. CPU is reported relative to one logical CPU. Use the same tab count, test hardware, refresh rate, permissions and warmup for comparisons. There are no fabricated Chrome/Firefox benchmark results.

Performance design: session tabs load on first selection, inactive tabs follow engine-recommended lifecycle states (including active audio/capture), native engine rendering, bounded HTTP cache, filtered/limited SQLite history, debounced session saves, virtualized QML lists, update/download polling only during activity, bounded rule sets. Modern browser pages and renderer processes will still consume substantially more memory than simple native utilities. Chromium rendering costs are not eliminated by writing the shell in C++.
