# Third-party software

- Qt 6: dynamically linked, LGPL/GPL/commercial licensing depending on module
  and distribution. See https://www.qt.io/licensing/ and the matching source
  distribution.
- libVLC 3: public libVLC API, LGPL 2.1 or later, plus the VideoLAN runtime's
  individual plugin and bundled dependency licenses (some GPL). The Windows
  and macOS fetch scripts use the unmodified 3.0.24 release at
  https://download.videolan.org/pub/videolan/vlc/3.0.24/ .
  Matching upstream VLC source:
  https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz .
  Linux uses its distribution's libraries and source packages.
- Xlib: platform-provided Linux X11 library.
- psutil: optional benchmark dependency only.
- Test clips are synthetic and were generated using FFmpeg; FFmpeg is not an
  application dependency.

The bundled runtime includes VideoLAN license notices copied at packaging time.
Before redistributing the installers publicly, ensure the obligations for Qt,
libVLC, every bundled VLC plugin and the corresponding dependency source are
met; upstream source links alone may not satisfy all distribution obligations.
