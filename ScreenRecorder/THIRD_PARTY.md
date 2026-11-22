# Third-party components

- Qt 6: LGPL/GPL/commercial terms, https://www.qt.io/licensing/
- FFmpeg: bundled or installed as a separate executable, from upstream or
  the build system's package manager. FFmpeg/libx264-enabled builds may be
  GPL-licensed. See https://ffmpeg.org/legal.html and
  https://ffmpeg.org/download.html; FFmpeg source:
  https://git.ffmpeg.org/ffmpeg.git. The FFmpeg executable is invoked as a
  separate process; this source does not statically link FFmpeg into Qt.
- GStreamer (Linux portal): LGPL, https://gstreamer.freedesktop.org/
- PipeWire (Linux): MIT, https://pipewire.org/
- Native Apple and Windows frameworks are provided by the corresponding OS.

These build scripts are for the user's local/personal builds. If redistributing
binary packages, preserve required license notices and supply corresponding
source and other materials for the exact distributed dependency versions as
required by their licenses. FFmpeg builds vary in included codecs and terms.
