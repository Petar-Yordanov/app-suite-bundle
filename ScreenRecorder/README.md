# ScreenRecorder 1.0.0

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

Minimal Qt Quick screen recorder with C++20, FFmpeg and native capture.
No accounts, telemetry, editing, streaming or clipboard use.

## Local build

Use Xmake 3.x, Qt 6.8.3 desktop SDK, and a C++20 compiler. Install FFmpeg with
libx264, H.264 encoders, AAC and the device backends for your OS. FFmpeg 7.1+
is recommended (Windows window capture requires a build supporting gdigrab
`hwnd=`). Qt SDK directories below are examples; change them as necessary.

### Windows 11 (x64)

Install Visual Studio 2022 C++ desktop development tools, Xmake and Qt's
MSVC 2022 64-bit SDK. Install FFmpeg, e.g. `choco install ffmpeg -y`, or put a
compatible FFmpeg binary on PATH.

```powershell
cd ScreenRecorder
$env:QT_ROOT_DIR = "C:\Qt\6.8.3\msvc2022_64"
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR"
xmake build -y screenrecorder
xmake run screenrecorder
```

### macOS Apple Silicon

macOS 14+, Xcode, an ARM64 Qt macOS SDK, Xmake and Homebrew FFmpeg.

```bash
brew install ffmpeg xmake
cd ScreenRecorder
export QT_ROOT_DIR="$HOME/Qt/6.8.3/macos"
xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR"
xmake build -y screenrecorder
xmake run screenrecorder
```

Grant Screen Recording permission to ScreenRecorder or the development
launching application in System Settings > Privacy & Security. If prompted,
grant microphone permission when using microphone recording. Launch the
installed app for normal permission attribution. Apple Silicon and Intel
Mac builds use the same source; ensure the external FFmpeg matches the CPU.

### Linux (x64)

Ubuntu 24.04 example (install equivalent packages on other distros):

```bash
sudo apt install ffmpeg pulseaudio-utils pkg-config libx11-dev libxrandr-dev libxfixes-dev libgl-dev libegl-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-pipewire xdg-desktop-portal
cd ScreenRecorder
export QT_ROOT_DIR="$HOME/Qt/6.8.3/gcc_64"
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR"
xmake build -y screenrecorder
xmake run screenrecorder
```

Use a working PulseAudio server or PipeWire's PulseAudio compatibility
server for audio. Wayland additionally requires a desktop-appropriate
ScreenCast portal backend, running PipeWire, and the GStreamer PipeWire
plugin. Configure the appropriate portal for your desktop environment.

## Usage

- Open the app (or press Ctrl+Alt+R; Cmd+Option+R on macOS).
- Choose a screen, region or application window. On Wayland, the compositor
  selects the source and region selection operates on its preview.
- Configure FPS (10–120), maximum resolution (native/720p/1080p/1440p/2160p),
  bitrate (500–100,000 kbps), encoding speed/quality, cursor, microphone and
  system audio. Lower resolution preserves aspect ratio without upscaling.
- Record. A background tray/menu bar icon shows recording status, and the
  menu/tooltip displays media duration. The GUI can be reopened to control it.
- Press Ctrl+Alt+P (Cmd+Option+P on macOS) to pause/resume, or Ctrl+Alt+R
  to stop. Both shortcuts are configurable in settings.
- Output is a timestamped MP4 in the selected directory. No automatic
  clipboard copies. Open Recordings or Play Last to inspect the result.

Close/Hide destroys the QML interface while leaving the shortcut agent
running. Enable login startup in Settings if desired. Commands sent to the
same per-user instance:

```bash
screenrecorder --background
screenrecorder --show
screenrecorder --toggle
screenrecorder --pause
screenrecorder --stop
```

If native shortcut registration is unavailable, assign those commands to
the desktop's own custom shortcuts (including Wayland). A conflicting
binding produces a visible settings error.

## Implementation

| Platform | Capture | Audio |
|---|---|---|
| Windows | DXGI/NVENC direct GPU path for compatible unscaled monitor/region capture; gdigrab fallback and HWND window capture | DirectShow mic; native WASAPI shared loopback |
| macOS | ScreenCaptureKit with native output scaling, selected display/window and cursor configuration | AVFoundation mic, ScreenCaptureKit system audio |
| Linux X11 | FFmpeg x11grab with XRandR monitor geometry or window ID | PulseAudio/PipeWire source and default sink monitor |
| Linux Wayland | ScreenCast portal with one granted PipeWire source; bounded GStreamer capture | PulseAudio/PipeWire source and default sink monitor |

Hardware H.264 encoding is probed at the actual output resolution and
selected automatically where working. Candidates: NVENC, QSV, AMF, Media
Foundation (Windows); VideoToolbox (macOS); NVENC, VAAPI, QSV (Linux).
Software H.264 via libx264 is the fallback; software-only can be forced.

Native raw capture paths use bounded latest-frame mailboxes, with at most
one frame retained by the mailbox and limited socket/encoder queues. Audio
has a 0.5-second mailbox and bounded queues. Windows validates the loopback
socket's client process; Unix uses a user-only local socket in a private
temporary directory. No screen or audio data is transmitted to remote hosts.

Audio inputs use wall-clock timestamps and resampling to correct drift;
microphone and system audio are downmixed/resampled and mixed into a stereo
AAC stream. Video uses an even-dimension H.264 output at the configured FPS.
Actual hardware performance, A/V synchronization and device behavior
depend on the driver and capture backend; verify with a real recording.

Pause finishes the current MP4 segment, then resume starts another with
identical encoding configuration. Stop remuxes multiple segments without
re-encoding, and publishes a single MP4 (single-segment recordings are
renamed directly). Only completed segments are used. Free space is monitored.
No crash recovery is implemented; errors during final joining report the
location of completed segments if they cannot be safely merged.

### Platform limitations

- Protected content may appear black. Windows/X11 minimized or obscured
  windows are limited by the native device. Window resizing during a
  recording may end the capture or scale the new content to fixed output.
- Hardware encoding depends on the installed GPU, drivers and FFmpeg
  build. Not all devices support every encoder setting. VideoToolbox and
  some hardware encoders may ignore quality presets or treat bitrate as
  a target rather than an exact rate.
- Windows direct GPU-frame delivery is attempted for compatible NVENC
  monitor/region captures only. GDI fallback and most other paths involve
  CPU-visible frames. Mac/Wayland raw transports also copy frames.
  The app does not claim universal zero-copy capture.
- ScreenCaptureKit window system audio may be scoped to the selected
  application's content. A macOS permission denial or protected source
  cannot be bypassed.
- Wayland source types/cursor modes are controlled by the compositor.
  Some portals permit monitor but not window capture, or cannot honor
  a requested cursor mode. No capture proceeds without permission.
  The portal session is retained during pause; the encoder and output
  transport stop, but the portal may still indicate an active share.
  Global shortcuts rely on desktop custom command bindings on Wayland.
- Frame limits: maximum 8192 in either dimension and 128 MiB for one
  BGRA source frame. Recording FPS may drop if encoding/capture cannot
  sustain the selected frame rate. No simultaneous monitor recording.

## Tests

```bash
xmake build -y screenrecorder-tests
xmake build -y screenrecorder-media-tests
xmake run screenrecorder-tests
xmake run screenrecorder-media-tests
xmake run screenrecorder --smoke-test
```

The C++ media test exercises FFmpeg's real encoder, two-source audio mix,
MP4 segmentation and remux, and byte-bounded PCM mailbox. With FFmpeg and
ffprobe installed you can also run the standalone integration experiment
without Qt:

```bash
python3 Tests/ffmpeg_pipeline.py
```

On Linux headless CI use `xvfb-run -a` for the GUI smoke test.
To test a real screen capture, pause and resume without capturing your own
desktop, run:

```bash
xvfb-run -a -s "-screen 0 1024x768x24" xmake run screenrecorder --capture-smoke
python3 Tests/raw_transport.py
```

The capture smoke test uses an ephemeral output directory. The socket test
is for Linux/macOS; it skips if Unix-domain sockets are unavailable.
On macOS/Windows set QT_QPA_PLATFORM=offscreen and QT_QUICK_BACKEND=software
for the smoke test. These smoke tests do not replace real capture validation.

### Manual desktop test

1. Record 10 seconds of a monitor with cursor included and microphone
   disabled. Stop and play the MP4.
2. Record a small region with cursor disabled, checking mixed-DPI displays.
3. Record a selected application window, then pause for 5 seconds, resume
   for 5 seconds, and stop. Verify the pause gap is absent.
4. Enable microphone and system audio simultaneously, play an audible
   tone, make a visible clap/movement, and verify audio/video sync.
5. Repeat in software-only mode; compare CPU/GPU usage and FPS.
6. Hide the GUI, test both global shortcuts and verify near-zero idle CPU.
7. Check cancellation, unavailable devices, permissions and disconnected
   monitors. Confirm MP4s are saved to the configured directory only.

## Benchmarks

Install Python psutil: `python3 -m pip install psutil`.

```bash
python3 Benchmarks/record.py --resolution 1920x1080 --fps 60 --seconds 10 --encoder libx264 --encoder h264_nvenc
python3 Benchmarks/monitor.py --pid 12345 --seconds 15
```

The first measures synthetic encoding throughput and peak memory. Substitute
`h264_videotoolbox`, `h264_qsv` or your installed encoder as appropriate.
The second samples the real app and child encoder's CPU/RSS, both idle and
while recording. Run comparable tests with the same source, quality, FPS,
encoder and resolution in other screen recorders. GPU usage is best
measured through the operating system's GPU utilization monitor.

## Installers / portability / updates

The supplied suite build.yml adds Windows x64 (.exe), Linux x64 (.deb), and
macOS ARM64 (.pkg). Windows and macOS packages bundle FFmpeg; the Linux
.deb declares FFmpeg/GStreamer dependencies. Use `apt install ./file.deb`
to resolve them. Mac packages are ad-hoc signed but not notarized.

To package locally from ScreenRecorder after a Release build:

- Windows: `pwsh -File Packaging/package-windows.ps1` (Inno Setup 6).
  Set FFMPEG_BINARY to the actual executable if FFmpeg is a PATH shim.
- macOS: `bash Packaging/package-macos.sh` (FFmpeg and Python 3 on PATH).
- Linux x64: `bash Packaging/package-linux.sh` (patchelf, dpkg-deb).

Use PORTABLE=1 for optional portable archives; launch with --portable to
store settings next to the executable. On Linux the portable bundle still
requires its system media and portal dependencies.

Automatic update checks are available once a trusted HTTPS release manifest
URL is set (example in Packaging/release-example.json). Payload SHA-256 is
checked before launch of the installer. No server/feed is bundled and updates
are not silently installed.
