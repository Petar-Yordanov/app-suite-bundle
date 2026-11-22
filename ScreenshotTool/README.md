# Screenshot Tool

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

A clipboard-first C++20 / Qt 6 / QML desktop screenshot utility. No accounts, telemetry, editing, annotation, or automatic image file saving.

## Usage

Launch Screenshot Tool once after installation. It registers configurable global shortcuts and enables login startup by default. Close the settings or preview window to return to the resident tray/shortcut handler. Choose Quit from the tray to exit entirely.

| Action | Windows / Linux default | macOS default |
| --- | --- | --- |
| Capture a rectangular region | Ctrl+Alt+1 | Command+Option+1 |
| Capture the monitor under the pointer | Ctrl+Alt+2 | Command+Option+2 |
| Select a window to capture | Ctrl+Alt+3 | Command+Option+3 |
| Cancel selection / close preview | Esc | Esc |

Shortcut fields use Qt portable notation (Ctrl means Command on macOS; Meta means physical Control). The screen and window pickers in Settings allow explicit source selection. Drag a rectangle on a single screen; right-click or Esc cancels. The screenshot goes to the clipboard immediately and a preview opens. Only **Save PNG...** writes an image file. Closing preview does not clear the clipboard. Settings also expose delay, cursor, theme, login startup and updates.

CLI: `screenshottool --region`, `--screen`, `--window`, `--show`, `--cancel`, `--background`, `--version`. Commands forward to the existing instance. Use `--portable` consistently for a separate writable data folder next to the executable (or .app bundle); a system-installed read-only folder cannot host portable data.

## Build

Use Qt 6.8.3 (Qt Quick, Controls, Widgets, Concurrent, Network, Linux DBus), Xmake 3.1.1 and a C++20 compiler. No codec packages or third-party C++ download recipes are needed.

Windows x64 (PowerShell, Visual Studio 2022):

```powershell
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR"
xmake build -y screenshottool
```

Ubuntu 24.04 x64 (or equivalent Linux distribution with native Qt SDK):

```bash
sudo apt-get install libx11-dev libxrandr-dev libxfixes-dev libgl-dev libegl-dev libxcb-cursor0 libxkbcommon-x11-0
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR"
xmake build -y screenshottool
```

Apple silicon, macOS 14+ (Xcode with macOS 14+ SDK):

```bash
xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR"
xmake build -y screenshottool
```

Other x86-64/ARM64 desktop architectures can be built from source with a matching Qt SDK, Xmake architecture and compiler. They are not in the current three-runner packaging matrix.

Set `APP_VERSION=1.0.0`, `PORTABLE=0`, and `QT_ROOT_DIR` for packaging. Run the appropriate script under Packaging. Windows uses Inno Setup 6 and MSVC redistributables, macOS uses macdeployqt and productbuild, Linux uses patchelf and dpkg-deb (Ubuntu 24.04 ABI). The macOS package is ad-hoc signed, not Developer ID signed or notarized. A release installation on other macs requires appropriate signing/notarization or local authorization. Portable archive build is optional using PORTABLE=1, not enabled in the suite CI.

The repository workflow at `.github/workflows/build.yml` retains ClipDeck and ImageViewer and builds the three Screenshot Tool installers directly as artifacts.

## Design and platform limitations

- Windows: GDI capture per monitor with native physical pixel coordinates; Windows Graphics Capture/D3D11 for selected visible top-level windows, with PrintWindow fallback when capture is unavailable (not when permission is denied). Protected content can be blank or unavailable; some accelerated apps do not implement PrintWindow correctly, and unresponsive window capture can stall in the OS. Cursor inclusion uses the native API. Global shortcuts use RegisterHotKey.
- macOS: ScreenCaptureKit one-shot capture on macOS 14+, with Screen Recording authorization. If permission is denied, enable it in System Settings and retry/relaunch. Retina size is derived from the capture filter's point-pixel scale. Global shortcuts use Carbon hotkeys. Protected/minimized windows may not be available.
- Linux X11: XRandR identifies physical monitors, XGetImage captures the source, XFixes adds the cursor and XGrabKey handles shortcuts. Obscured, off-screen or protected window pixels may be inaccessible; no substitution with another window. X11 clipboard ownership requires the process to remain running unless a clipboard manager retains the data.
- Linux Wayland: uses the desktop's interactive Screenshot portal and GlobalShortcuts portal. The compositor controls what individual monitor/window/region selectors it provides, actual key bindings and cursor inclusion. Choose one source in its dialog. If the GlobalShortcuts portal is unsupported, configure custom desktop shortcuts using the CLI above. No attempt to bypass Wayland permissions. The portal may generate a temporary screenshot file independently of this app; the app imports and removes the returned local file and reports cleanup failures. Some compositor clipboard policies require using the preview's Copy button after focus. On Wayland, Qt must have its Wayland platform plugin available.
- Region selection is restricted to one monitor and maps logical selection dimensions to the native captured pixels, including fractional DPI. If monitors change during capture, retry.
- Backend capture runs off the GUI thread, except the asynchronous compositor portal interaction. A single bounded task runs at once. The background handler does not instantiate a QML engine until needed. QML engines and captured preview buffers are released on dismiss; clipboard ownership may retain one image. Pixel cap is 128 Mi pixels (~512 MiB for one 32-bit image); temporary native buffers and GPU textures can increase peak memory.
- The update mechanism checks a user-configured HTTPS release manifest every six hours, downloads a matching artifact and verifies SHA-256. Installation requires explicit confirmation; no feed is configured by default. See Packaging/release-example.json. Use only your trusted release host.
- Login startup is per-user (Windows Run registry, macOS LaunchAgent, Linux desktop autostart). Disable it before uninstall on macOS/Linux if removing the app manually.

## Tests and performance

```bash
xmake build screenshottool-geometry-tests
xmake run screenshottool-geometry-tests
xmake build screenshottool-tests
xmake run screenshottool-tests
xmake build screenshottool-bench
xmake run screenshottool-bench
```

Linux X11 CI uses `SCREENSHOT_NATIVE_TEST=1 xvfb-run -a xmake run screenshottool-tests` for actual display capture. The integration suite loads all three QML views, checks DPI/crop pixels, clipboard restoration, PNG roundtrip, cancellation and no automatic file saving. Native window selection, global hotkeys, permission dialogs and real mixed-DPI multi-monitor behavior require hands-on desktop tests. Use `xmake run screenshottool-bench --capture` on X11 for the capture timing. The benchmark reports startup, cropping, PNG encoding and optional native capture timings to stdout.

For process-level idle measurements, exit the running app and run:

```bash
python -m pip install psutil
python Benchmarks/idle.py /path/to/screenshottool --seconds 15
```

Pass the actual `Contents/MacOS/screenshottool` on macOS. The benchmark uses a temporary settings profile, avoids changing login startup, and prints the resident process RSS and CPU percentage. The application emits READY_MS when available. Compare on identical hardware and desktop sessions with another resident screenshot tool.
