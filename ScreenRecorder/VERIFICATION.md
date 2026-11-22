# Verification

Executed in this development environment:

- Standalone GCC C++20 tests: PASS (geometry, HiDPI clipping, resolution,
  configuration bounds, state guards and progress format).
- Real FFmpeg 7.1.5 integration experiment: PASS (H.264, two AAC inputs
  mixed into stereo, segments joined without re-encoding, MP4 decode,
  total duration and audio/video duration agreement).
- Synthetic 640x360, 30 FPS, two-second software benchmark: executed.
- Raw Unix socket transport test: skipped here because local socket
  creation is prohibited in this execution environment. Run
  `python3 Tests/raw_transport.py` on Linux/macOS.

Xmake/Qt development SDK and GStreamer development headers are not available
in this environment. Full Windows/macOS/Linux builds, native screen/audio
capture, real GPU encoder tests, QML runtime, portal and installer execution
have not been verified here.

The combined workflow builds the application and C++ regression/media
tests on the three target platforms and runs QML smoke tests before packaging.
Follow the manual desktop test in README.md to validate capture permission,
device behavior, actual A/V synchronization and performance.
