# App Suite — build and run locally

Each application remains an independent Xmake project. The GitHub workflows use the same source and target names, with a pinned preinstalled Qt SDK for reproducible packaging. The local route does not require the CI environment or an installer.

## Quick start

Install Xmake (3.1.1 or later compatible release), your platform's C++20 compiler and Python 3.11+ (for the optional SDK bootstrap). On Windows use PowerShell, not MSYS/Git Bash, so Xmake selects MSVC. Install Visual Studio 2022 Build Tools with Desktop development with C++ and the Windows SDK; on Mac use Xcode/command line tools; on Linux use GCC/Clang and the native development packages below.

For the most reliable local setup, from the repository root run:

```sh
python tools/dev.py ImageViewer
```

On macOS/Linux use `python3` if `python` is not available. The command selects the native compiler architecture, discovers a compatible Qt SDK (including its required modules), downloads a matching official Qt kit using aqtinstall if necessary, configures Xmake, builds the GUI target and runs it. No global Qt environment variable or hard-coded SDK location is required. The Qt download is cached under `~/.appsuite/Qt`, outside the source tree. Dependencies may require significant first-run download/build time; subsequent runs reuse the cache. The helper does not install OS packages or elevate privileges.

```sh
python tools/dev.py MediaPlayer --configure-only
cd MediaPlayer
xmake build -y mediaplayer
xmake run mediaplayer
```

Other options: `--build-only`, `--mode debug`, `--no-download` (do not provision Qt/libVLC; Xmake may still fetch other declared packages), `--qt /path/to/sdk`, and `--` followed by application arguments. Example: `python tools/dev.py WebBrowser -- https://example.org`.

## Direct Xmake (like Findex)

The nine ordinary desktop projects now declare `qt6quick` and `qt6widgets` when an explicit SDK is not selected, and attach the packages to their GUI and Qt-dependent test targets. The Xmake Qt rule integrates its discovered SDK or the package SDK; non-Qt test targets are unchanged. The CI's explicit `--qt` path takes priority and avoids fetching a second Qt SDK. `QT_ROOT_DIR` is also accepted as an SDK override.

```sh
cd ImageViewer
xmake f -c -y -m release
xmake build -y imageviewer
xmake run imageviewer
```

If a package SDK lacks the additional Qt image format plugins, use the bootstrap above to obtain the full SDK. Qt WebEngine requires Qt 6.11.3+ and is not available via the older Xmake Qt package recipes, so use the bootstrap for a clean browser setup. Once configured, normal `xmake build` and `xmake run webbrowser` work without rerunning the bootstrap. The WebBrowser Xmake script also searches standard Qt installer paths and the bootstrap cache automatically.

| Directory | GUI target (`xmake run <target>`) |
| --- | --- |
| `Arky` | `Arky` |
| `ClipboardManager` | `clipdeck` |
| `DockerGUI` | `dockergui` |
| `ImageViewer` | `imageviewer` |
| `MediaPlayer` | `mediaplayer` |
| `ScreenshotTool` | `screenshottool` |
| `ScreenRecorder` | `screenrecorder` |
| `TerminalEmulator` | `terminalemulator` |
| `SystemMonitor` | `systemmonitor` |
| `WebBrowser` | `webbrowser` |

Use the exact target names in the table. `xmake run` runs an already-built target; build first or use the bootstrap to do both. On an existing checkout with stale Xmake cache, run `xmake f -c -y -m release` once (or configure with the explicit SDK path); never commit `.xmake` or platform-specific build outputs. Add `**/.xmake/`, `**/build/`, `**/.deps/`, `**/dist/` and `**/artifacts/` to your existing root `.gitignore`. Old tracked cache files need to be untracked separately.

## Native prerequisites and exceptions

- Linux (Ubuntu 24.04): `sudo apt install build-essential pkg-config libgl-dev libegl-dev libx11-dev libxrandr-dev libxfixes-dev libxcb-cursor0 libxkbcommon-x11-0 nasm`. Screenshot/clipboard/media player need the respective X11 development libraries. On other distros use equivalent packages.
- Image Viewer: the Qt `qtimageformats` module supplies TIFF and WebP plugins; the bootstrap includes it. Xmake also builds the existing libheif/libraw/STB dependencies. Do not remove them to make a local build pass.
- Media Player: needs official libVLC 3 headers and runtime. The bootstrap detects them and, on Windows/macOS if missing, invokes the existing checksum-verifying VideoLAN fetch scripts. Xmake automatically detects their cached headers/runtime afterward; a custom `--vlc_include` and `--vlc_root` are supported. Linux: `sudo apt install libvlc-dev libvlc5 vlc-plugin-base vlc-plugin-video-output vlc-plugin-access-extra` (or equivalent distro packages).
- Screen Recorder: install FFmpeg **at runtime**, e.g. `winget install Gyan.FFmpeg`, `brew install ffmpeg`, or `sudo apt install ffmpeg`. Linux also needs `libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-pipewire pulseaudio-utils xdg-desktop-portal`. The recorder locates FFmpeg on PATH or in standard locations; see its README for `FFMPEG_BINARY` and capture permissions.
- Terminal Emulator: Windows needs the MSVC-compatible Clang toolchain (LLVM/Clang tools for Windows in Visual Studio) to compile the existing libvterm C99 sources.
- Docker GUI: install Docker Engine/CLI independently; the GUI is not an engine installer.
- Browser: desktop Qt WebEngine, WebChannel and Positioning modules are needed; the bootstrap downloads the pinned 6.11.3 kit, separately from other apps. The iOS target still uses `xmake ios-bundle --qt=/path/to/Qt/6.11.3/ios` on Mac; physical iPhone deployment requires signing. DRM/live-site restrictions from the browser README remain.
- macOS screen capture/audio and global shortcuts may require OS permissions; Wayland restrictions remain documented by each app. Native GUI tests require a graphical session.

## CI and persistent downloads

The suite now uses independent workflows in `.github/workflows/`. Each three-platform desktop matrix starts only when its app or own workflow changes (or via Actions > Run workflow). WebBrowser has a separate iOS simulator workflow. The existing explicit Qt SDK, native tests, benchmark and installer steps are retained. Shared publication-script changes trigger all workflows. A short-lived Actions artifact remains available for seven days, for diagnostics and publication.

**Migration:** replace the previous root `.github/workflows/build.yml` with these workflows. Delete the obsolete `build.yml` and any old `build-imageviewer.yml` in your checkout if present; they are not included in this ZIP. Leaving either old workflow enabled would trigger duplicate builds. The first commit adding all new workflow files normally triggers all apps once; if billing is blocked, you can use `[skip ci]` on that migration commit and manually run only the apps you need afterward.

After all desktop builds for one application pass, a small Linux publish job updates only that app's three installers in the permanent **Latest App Suite Builds** release (`latest-builds`). The browser iOS simulator is published independently. A failed app build leaves its existing assets untouched. The publisher stages and verifies SHA-256 before replacing a previous file. No changes to unrelated installers. Publication only runs on the default branch (including manual runs on that branch), not pull requests. The release tag's source ZIP is not a snapshot of all installers; asset labels identify their build commits. The repo must permit GitHub Actions `contents: write`, and the rolling release must not be immutable. Private repository releases remain private.

Permanent download URL: `https://github.com/<owner>/<repository>/releases/tag/latest-builds`.

## Verification

This migration is source-level and preserves the existing application logic except improved VLC/FFmpeg discovery and a Windows-only compiler flag. Native Qt builds require the target SDK/compiler; do not treat syntax/structural tests as proof that each app has run on every OS. See each app's README and CI results for full verification.
