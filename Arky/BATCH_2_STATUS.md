# Arky 0.3 — Batch 2: Windows/Qt readiness and QML integration

**Status: source-level improvements and portable-engine verification completed; Windows/Qt validation pending.**

## Implemented this batch

1. **Real Qt Quick startup smoke harness**: `xmake run Arky --smoke-test` loads embedded QML (with Qt Quick Controls and native dialog imports), creates a temporary ZIP using libarchive, opens it through the asynchronous Qt controller, and validates the visible list model. Returns nonzero on failure or timeout. Reports QML load warnings to stderr. The main UI still runs normally without the flag.
2. **Windows test automation**: `scripts/verify-windows.ps1` configures/builds with Xmake and runs archive, MIME, and UI smoke tests; checks exit codes. Accepts `-QtSdk` when using a manually installed Qt SDK. The `.github/workflows/windows-qt-smoke.yml` workflow can run it on an actual Windows/MSVC runner when committed to a GitHub repo; it has not been executed in this environment.
3. **Qt model view correctness**: `ArchiveController.visibleCount` describes the entries visible in the current folder/search filter rather than the total archive size. QML now displays a real empty-search/folder state and a clear-filter action.
4. **Encrypted data transfer**: UI passes the entered archive password when copying selected files to the clipboard and preparing row drag-out, rather than silently passing an empty password.
5. **Honest native drag semantics**: native `QDrag::exec` can begin immediately for a previously prepared selection. A long-press on an unprepared row now stages its files instead of attempting a native drag *after* the mouse gesture ended. The user can start a new drag with **Drag staged**.
6. **Editing dialogs**: dark panel backgrounds and readable labels consistent with the main window.

## Executed verification

- Linux portable `archive_tests` compiled with GCC C++20 and libarchive 3.7.4: **PASS**.
- Linux AddressSanitizer + UndefinedBehaviorSanitizer `archive_tests`: **PASS**, no sanitizer reports.
- Validated Qt 6 FileDialog selected filter API, Popup palette API, Xmake qt.quickapp/qrc rule against upstream docs; these are documentation checks, not a compiled application.
- Checked QML resource references and archive/test source presence.

## Not verified / risks

- **Cannot claim Qt GUI compiled**: this runtime has no Qt SDK or Xmake; Debian apt refresh timed out. Windows MSVC, Qt Quick Controls module availability, native QML dialogs and MIME/drag-drop still need the Windows harness executed on a machine with dependencies installed.
- Native drag of **unstaged** virtual archive entries remains a two-gesture workflow; Windows FileGroupDescriptorW / FileContents streaming, macOS file promises remain outside this batch.
- Archive metadata fidelity, safety hardening, and other audit gaps remain for subsequent batches.

## Windows commands

In **x64 Native Tools Command Prompt for VS 2022** from the extracted project folder:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1
```

Or individually:

```bat
xmake f -p windows -a x64 -m debug -y
xmake -y
xmake run archive_tests
xmake run mime_tests
xmake run Arky --smoke-test
xmake run Arky
```

If you manually installed Qt and want to select its SDK:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1 -QtSdk "C:\Qt\6.x\msvc2022_64"
```

## Next batch

Batch 3 is native MIME/drag-and-drop semantics, clipboard lifetime, file promises, and cross-platform tests. If the new Windows test harness uncovers compile/runtime errors, those become highest priority before Batch 3 features.
