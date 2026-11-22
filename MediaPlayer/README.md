# Media Player

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

Lightweight local video player using **C++20, Qt 6, QML, Xmake and libVLC 3**.

The application uses libVLC for decoding, audio, subtitles, seeking, container
support and native rendering. It does not implement its own demuxer or codec
stack. Only the QML controls and optional sidebar are rendered by Qt Quick.

## Usage

Open one file for directory-based Prev/Next navigation; open several files or
add files to the playlist to use playlist navigation. The explicit playlist can
be reordered, removed from, cleared and played in order. It advances to the next
item when playback finishes; it never loops or shuffles. Directory navigation
orders likely video files by filename; actual video tracks are checked by libVLC
when opened, and a failed candidate is skipped when navigating. Drag & drop and
OS file-open events work too.

Playback includes play/pause/stop, progress/seek, elapsed time and duration,
volume/mute, fullscreen, embedded and external subtitles, audio/subtitle track
selection or disabling, aspect ratio selection and saved playback positions.
The Tracks panel includes all track controls. The Settings panel contains
theme, remembering positions, shortcuts and updates. Saved positions are local,
bounded to 2,000 entries and invalidated when a file changes.

| Action | Default |
| --- | --- |
| Open video(s) | Ctrl+O |
| Play / pause | Space |
| Stop | S |
| Previous / next | P / N |
| Seek 10 seconds | Left / Right |
| Volume +/- 5% | Up / Down |
| Mute / unmute | M |
| Fullscreen | F |
| Leave fullscreen / close sidebar | Esc |
| Playlist | Ctrl+L |
| Load subtitle | Ctrl+T |
| Settings | Ctrl+, |

The Qt portable notation uses Ctrl for Command on macOS. All playback actions
can be remapped; Escape stays reserved for exiting fullscreen. Move the cursor
to the bottom of the fullscreen window to reveal the controls. The remainder
of the screen is dedicated to playback.

The installers register common video extensions and MIME types and allow the
OS to choose Media Player as the default viewer. They do **not** overwrite your
existing default-app choice. Windows: Settings > Apps > Default apps; macOS:
Finder Get Info > Open with > Change All; Linux: file manager Properties / Open
With or desktop MIME defaults.

## Build

Requirements: Qt 6.8.3 desktop SDK (Core, Gui, Widgets, Qml, Quick, QuickWidgets,
QuickControls2, Network), Xmake 3.1.1, a C++20 compiler, official libVLC 3 headers
and an architecture-matched libVLC runtime with its plugin directory. No
MSVC-specific libVLC import library is needed; the C API is resolved from the
installed/shared library using the official header declarations.

Windows x64, PowerShell (Visual Studio 2022 + Qt MSVC SDK):

```powershell
. ./Packaging/fetch-vlc-windows.ps1
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR" "--vlc_include=$env:VLC_INCLUDE_DIR"
xmake build -y mediaplayer
xmake run mediaplayer
```

The fetch script downloads the pinned official VideoLAN 3.0.24 runtime and source
headers and compares SHA-256 against the corresponding upstream manifests.
For Windows ARM64, use matching Qt/MSVC SDK and set `$env:VLC_ARCH='arm64'`
before fetching, configuring with `-a arm64` and packaging.

macOS Apple silicon, with Xcode and matching Qt SDK:

```bash
bash Packaging/fetch-vlc-macos.sh
export VLC_SDK_ROOT="$PWD/.deps/vlc/VLC.app/Contents/MacOS"
export VLC_INCLUDE_DIR="$PWD/.deps/vlc/vlc-3.0.24/include"
xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR" --vlc_include="$VLC_INCLUDE_DIR"
xmake build -y mediaplayer
xmake run mediaplayer
```

On Intel macOS use the matching Intel Qt, `-a x86_64` and the Intel VLC DMG
(the fetch script detects the host architecture).

Linux x86-64, Ubuntu 24.04 or equivalent:

```bash
sudo apt-get install libvlc-dev libvlc5 vlc-plugin-base vlc-plugin-video-output vlc-plugin-access-extra libx11-dev libgl-dev libegl-dev libxcb-cursor0 libxkbcommon-x11-0
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR" --vlc_include=/usr/include
xmake build -y mediaplayer
xmake run mediaplayer
```

Linux ARM64 can be built with an ARM64 Qt SDK/compiler and distribution libVLC
packages using `-a arm64`. Linux uses the distribution's patched libVLC 3
runtime rather than bundling another copy.

## Installation and distribution

The suite's updated `.github/workflows/build.yml` retains ClipboardManager,
ImageViewer and ScreenshotTool. It configures, builds, tests and packages
MediaPlayer on Windows x64, Linux x64 and macOS ARM64 and uploads each installer
as a direct artifact, with no nested password-protected archives.

Set `APP_VERSION=1.0.0`, `QT_ROOT_DIR`, and `PORTABLE=0` (or 1 for optional
portable archives), plus the VLC paths from the build instructions. Run
`Packaging/package-windows.ps1` (Inno Setup 6),
`bash Packaging/package-macos.sh` (macdeployqt/productbuild), or
`bash Packaging/package-linux.sh` (patchelf/dpkg-deb).

Windows and macOS include the runtime and plugins; Linux packages declare
runtime dependencies. The macOS package is ad-hoc signed, not notarized; for
public distribution arrange proper Developer ID signing and notarization.
The Linux .deb targets Ubuntu 24.04's ABI. Other distributions should use the
native build instructions. Set `PORTABLE=1` for optional portable archives and
launch with `--portable` to store data in a writable folder adjacent to the
executable (or .app). The Linux portable archive still requires the system
libVLC runtime and compatible system libraries.

The updater can be configured using a trusted HTTPS release manifest in
Settings (see `Packaging/release-example.json`). It checks every six hours,
downloads a matching newer version and verifies SHA-256. Installation remains
explicit; no update feed, tracking, account or telemetry is configured by default.

## Rendering and scope

- Windows: native HWND output; macOS: native NSView; Linux X11: X drawable.
  These paths allow VLC to own hardware decoding and video rendering directly.
  They avoid sending each decoded frame through the QML scene graph.
- On Wayland with XWayland available, the default selects the xcb Qt backend for
  libVLC 3 native output. On pure Wayland, or when explicitly setting
  `QT_QPA_PLATFORM=wayland`, the app uses a bounded RV32 software-frame callback
  and draws the result in a QWidget. This requires the Qt Wayland platform
  plugin. That fallback incurs a CPU copy and does not promise native-path
  hardware rendering performance. Use `--software-output` for diagnostics.
- Hardware decoding is requested with VLC's automatic decoder selection; the
  codec, GPU driver, OS and output plugin determine actual acceleration.
  Unsupported/protected files and missing/broken distribution codecs can
  still fail, just as in VLC. Hardware decoding is not forced when unavailable.
- libVLC is loaded lazily on the first open. The UI does not poll an idle player.
  Playback polling is coarse (200 ms when playing, 500 ms paused), and the sidebar
  is created lazily. The native rendering surface is separate from Qt Quick.
- Actual video content is validated using track parsing; unfamiliar local
  containers can attempt playback, muted until video output/track detection.
  Known playlist/stream manifest types and non-local URLs are rejected. This
  is a local-video player, not a network-isolation sandbox for arbitrary
  malicious media or libVLC plugins. Keep the runtime updated.
- No audio-only playback, media library, streaming UI, conversion, loops,
  shuffle, speed changes or subtitle/audio synchronization controls.

## Tests

```bash
xmake build mediaplayer-core-tests
xmake run mediaplayer-core-tests
xmake build mediaplayer-tests
xmake run mediaplayer-tests "$PWD/Tests/fixtures"
```

The integration fixture is synthetic (12 seconds, MPEG-4 video, two AAC
audio tracks, one embedded SRT subtitle). An audio-only WAV and an additional
SRT are included. Integration tests instantiate the QML controls, initialize
libVLC, decode actual frames via the software output, exercise tracks, external
subtitles, seeking, saved positions, playlists, file handling and settings.
Headless tests use the software output and dummy audio (no hardware
acceleration assertions). Native GPU, hardware decoding, UI focus and
association behavior still require a desktop acceptance test on each OS.

## Performance benchmarks

Within the project, run the app with a local clip:

```bash
xmake run mediaplayer --benchmark --benchmark-seconds 5 "$PWD/Tests/fixtures/sample.mkv"
xmake run mediaplayer --benchmark-idle --benchmark-seconds 15
```

The playback benchmark emits JSON containing time to initialize UI and first
reported rendered frame, displayed/decoded/dropped pictures, playback sample
time and renderer/engine details. These timings are measured with a 100 ms
observation interval. The benchmark uses a temporary profile. To run the
headless-friendly software output on CI, add `--software-output`.

For a direct comparison with the standard VLC desktop app, install psutil and
use the actual executable paths with the same long local video and monitor:

```bash
python -m pip install psutil
python Benchmarks/compare.py --player /path/to/mediaplayer --vlc /path/to/vlc --video /path/to/video.mkv --seconds 15 --warmup 5
```

Use the executable inside each .app on macOS or the .exe on Windows. Close both
players before running. Use a video longer than the sample + warmup + 5s,
preferably 1080p, 4K H.264/HEVC and AV1 clips tested separately. Keep the windows
visible and approximately the same size and use the same VLC runtime version
and GPU driver. The harness reports average and peak RSS and process CPU as a
percentage of one logical core, for idle and playback in both programs; it
also runs the MediaPlayer instrumented first-frame/decoder-counter benchmark.
It does not pretend to measure VLC's first-frame timing from its CLI, and
RSS does not represent total GPU memory use. Record GPU engine activity
separately for hardware-decoding validation.
