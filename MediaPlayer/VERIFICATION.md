# Verification

- Standalone C++20 core tests: 22/22 passed using GCC.
- FFmpeg generated the synthetic fixtures; ffprobe confirmed MPEG-4 video,
  two AAC audio tracks and an embedded SubRip subtitle in the MKV fixture.
- Parsed workflow YAML, Lua, QRC, plist, JSON and benchmark Python successfully;
  Bash packaging scripts passed syntax checks.
- Full Qt and libVLC integration compilation/playback is not verified locally:
  this environment does not have the Qt 6 development SDK, Xmake or libVLC
  development/runtime packages. The integrated CI is configured to acquire
  them and build all three native installer targets.
- The desktop VLC performance comparison harness is supplied, but no comparative
  or native hardware-performance numbers are asserted without executing it.
- Hands-on acceptance: ordinary and high-resolution files, hardware decoding,
  native output on Windows/macOS/X11 and Wayland fallback, seek accuracy,
  audio/subtitle switching, fullscreen controls, association selection,
  playlist behavior, remembered positions, portable profile, and update feed.
