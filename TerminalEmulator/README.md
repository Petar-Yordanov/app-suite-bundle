# Terminal Emulator

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

A native C++20/Qt 6 Quick terminal, backed by libvterm 0.3.3 and real POSIX
pseudoterminals or Windows ConPTY. No web engine or embedded shell interpreter.

## Build locally

Install Qt 6.8.3 (Desktop, Qt Quick, Qt Quick Controls, Qt Quick Dialogs and Qt
Network), Xmake 3.1.1 and the platform C++ compiler. Xmake builds the pinned
libvterm dependency automatically. The first configuration needs internet access.

Windows: Visual Studio 2022 with Desktop development with C++ and Clang/LLVM
tools (clang-cl), using a PowerShell terminal rather than Git Bash.

```powershell
cd TerminalEmulator
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR"
xmake build -y terminalemulator
xmake run terminalemulator
```

Linux: build-essential, Qt desktop runtime dependencies, and Xmake.
macOS: Xcode command-line tools and the macOS Qt desktop SDK.

```bash
cd TerminalEmulator
# Linux:
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR"
# macOS Apple Silicon (instead of the preceding command):
# xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR"
xmake build -y terminalemulator
xmake run terminalemulator
```

Set QT_ROOT_DIR to the actual Qt kit directory (the directory containing
bin/qmake). If Qt is already discoverable by Xmake, omit the --qt argument.
Other desktop architectures can be built with their matching native Qt SDK and
compiler; the supplied GitHub CI intentionally builds only Windows x64, Linux
x64 and macOS ARM64.

## Usage

| Action | Default shortcut |
| --- | --- |
| New tab | Ctrl+Shift+T |
| Close active pane | Ctrl+Shift+W |
| Split left/right | Ctrl+Shift+D |
| Split top/bottom | Ctrl+Shift+E |
| Copy selection | Ctrl+Shift+C |
| Paste | Ctrl+Shift+V |
| Find terminal output | Ctrl+Shift+F |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab |
| Duplicate tab | Ctrl+Shift+U |
| Rename tab | Ctrl+Shift+R |
| Settings | Ctrl+, |

Use the shell selector to create a tab with another detected shell. The toolbar
provides tab reordering, duplication, splitting and search. Drag the dividers to
resize panes. Select text with the mouse (Shift overrides application mouse
reporting); Ctrl-click HTTP(S) hyperlinks. Ctrl+C remains a shell interrupt.
The settings dialog customizes shortcuts, font, colors, cursor, opacity, history,
themes, CWD inheritance and independent tab/pane restoration.

Layout restoration and duplication start new shell processes, not restored
processes or replayed terminal output. When only panes are restored, restore the
previously selected tab's pane layout. On Linux/macOS, the native shell process
CWD is queried; on Windows the app recognizes OSC 7 and CMD/PowerShell prompt
reports. WSL/Git Bash and other shells must emit a supported CWD report for
arbitrary directory inheritance; otherwise their starting directory is reused.
No persistent user shell profiles are created or edited.

## Implementation and resource bounds

- libvterm handles ANSI/VT parsing, Unicode cells, alternate screen, application
  cursor modes, mouse reporting, bracketed paste and terminal responses.
- One C++ Qt Quick painted surface per pane; batched text/background drawing.
  No per-cell QML items. Repaint notifications are coalesced, and blinking is
  disabled by default.
- PTY I/O is independent of the GUI; bounded queues provide backpressure.
  Input/output queues are limited to 1 MiB per pane; paste is limited to 512 KiB.
- History is bounded by both the configured line limit and a 32 MiB per-pane
  cap, with allocation capped to 128 MiB of history-cell buffers across panes.
  Maximum 32 tabs, 64 panes and 12 tree levels.
- OSC 8 links are bounded and user-activated HTTP(S) only; external metadata
  may be discarded during reflow. Font coverage depends on installed fonts;
  complex emoji shaping and terminal-specific extensions are not guaranteed.
- Qt Quick rendering and PTY timing depend on the OS and driver. No performance
  superiority is claimed before measurements on the target systems.

## Tests and benchmarks

```bash
xmake build -y terminal-pty-tests
xmake build -y terminal-vt-tests
xmake build -y terminal-layout-tests
xmake run terminal-pty-tests
xmake run terminal-vt-tests
xmake run terminal-layout-tests
xmake run terminalemulator --smoke-test
```

For CI, set QT_QPA_PLATFORM=offscreen and QT_QUICK_BACKEND=software for GUI tests.
The VT suite prints parsing time for 100,000 lines as well as checking history
bounds. Measure real GUI startup/idle RSS and CPU from a native display session:

```bash
python3 Benchmarks/gui.py --exe /absolute/path/to/terminalemulator
```

For a macOS .app, use the executable inside Contents/MacOS. On Windows use the
.exe, or sample an existing process with `--pid PID`. This measures the GUI
process separately from child shells. Re-run with 1/4/8 panes (using --pid) and
compare with the same workload, font, grid size and scrollback in another emulator.

Manual compatibility checks: run vim/nvim, top/htop, PowerShell and CMD as
available; test Ctrl+C, shell completion, mouse reporting, full-screen redraw,
emoji/CJK, pasting, splits during output, exiting and restarting a shell.

## Distribution

The integrated root workflow builds, tests, deploys and uploads one installer
per target: Windows .exe, macOS .pkg, Linux .deb. No nested archive is required.
Local packaging scripts are under Packaging/. They require the Qt SDK plus Inno
Setup (Windows), macdeployqt/productbuild (macOS), or patchelf/dpkg-deb (Linux).
The Linux package targets Ubuntu 24.04-compatible x64 systems. macOS packages
are ad-hoc signed, not notarized; normal Gatekeeper requirements apply.

Start with --portable to use a TerminalEmulator-data directory next to the
executable/application bundle. Use PORTABLE=1 when packaging for optional
portable archives.

Automatic updates: configure a trusted HTTPS release manifest in Settings.
Packaging/release-example.json defines its schema. The app checks periodically,
downloads updates, validates their SHA-256 and lets the user open the installer.
Until a real release feed is configured, no background network requests are
made. There are no accounts or telemetry. This is not an unattended installer.
