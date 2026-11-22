# Browser requirement traceability

This is a source-level implementation audit, not a declaration that every feature has passed native runtime tests. The CI runs the desktop builds, store and rules tests, installer deployment, and browser/media smoke tests. See VERIFICATION.md for actual local results.

| Requirement | Implementation / acceptance evidence |
| --- | --- |
| C++20, Qt 6, QML, Xmake | `xmake.lua`, C++ controllers/models, QML chrome; iOS Xmake task invokes Qt's static CMake/Xcode linker. |
| Desktop and iPhone engines | `DesktopPage.qml`, `DesktopProfiles.cpp` use Qt WebEngine; `IOSView.mm` uses WKWebView. CI builds Windows x64, Linux x64, macOS ARM64, iOS x86-64 simulator and iOS ARM64 device. Other desktop architectures depend on Qt SDK availability. |
| Tabs, reordering, reopen | `Browser.cpp`, `Main.qml`; store/model tests. Private closed tabs intentionally excluded. |
| Independently configurable session restoration | Settings for open tabs, selected tab, closed regular tabs and desktop session cookies. Tests cover selection, lazy tabs and privacy. |
| Private browsing, remember-me | Dedicated off-the-record Qt profile / nonpersistent WK data store; normal persistent profile. Packaged desktop smoke checks cookie isolation and private profile reset. Websites control login lifetime. |
| History | SQLite, search, delete and clear, successful navigation and SPA URL changes; private and disabled history excluded. Tests cover persistence and title changes. |
| Bookmark folders/import/export | SQLite hierarchy, rename/move/delete, search, HTML/JSON import/export; store tests verify nesting and round trips. |
| Download management | `Downloads.cpp`, native desktop request actions, `BrowserDownload` WKDownload delegate; progress, pause/resume/cancel where engine permits. On iOS use native NSProgress when available; unknown totals remain indeterminate. |
| Search engine | Configurable HTTP(S) template, URL parsing; tests reject unsupported explicit schemes. |
| Ad blocking | Host-based third-party rules, configurable custom hosts and exceptions; WK content blockers on iOS. No promise of YouTube in-stream ad blocking. |
| Mic, camera, permissions | Origin-specific Qt permission dialog and WK media permission prompts; OS permissions still apply. Desktop screen-share picker. |
| Discord voice/video, Office, YouTube, YouTube Music | Real engine navigation, media/capture capability checks, popup handling and optional automatic site popups. Account-specific live-site checks remain necessary. |
| Netflix | Standard EME and legitimate compatible Widevine option; no bundled CDM or guarantee. See COMPATIBILITY.md. |
| Themes, configurable shortcuts, minimal UI | `Main.qml`, `Browser.cpp` settings. No password manager, autofill UI, devtools interface, account requirement or application telemetry. |
| Portable, updates | Desktop portable data option and local packaging, configurable HTTPS feed, checksum-verified installers and explicit installation; manifest generator supplied. iOS uses normal signed app distribution. |
| Performance | Lazy restored tabs, engine-recommended inactive lifecycle states, bounded caches/history, virtualized lists, debounced session storage, hardware-accelerated engine where available. `Benchmarks/browser.py` compares process-tree CPU/RAM against another browser. |
| Tests, instructions, limitations | `Tests/`, `Benchmarks/`, `README.md`, `COMPATIBILITY.md`, `VERIFICATION.md`, and integrated workflow. Native GUI, real accounts, iPhone and measured benchmark comparisons must still be validated. |

No synthetic website results are substituted for actual service compatibility. Desktop builds target a maintained Qt WebEngine SDK; keep it patched for security. The iOS simulator artifact is not a signed iPhone installer.
