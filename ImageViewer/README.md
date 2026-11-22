# Image Viewer — AppSuite

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

A native C++20 / Qt 6 Quick viewer, using Xmake. Local files only; no accounts, analytics, editing, slideshow, or browser engine.

## Build

Install Qt **6.8.3 or later** with Qt Quick, Quick Controls, Quick Dialogs, SVG and the Qt Image Formats plugins (especially TIFF and WebP), Xmake 3.x, and a native C++20 compiler. Xmake fetches the pinned LibRaw, libheif (libde265 and AOM decoder enabled) and stb dependencies automatically. This requires network access for the first build. Use an SDK matching the compiler and architecture, not a Qt runtime-only installation.

The source ZIP contains both `ImageViewer/` and `.github/workflows/build-imageviewer.yml`. Extract it **at the repository root, preserving all directories**. Do not copy only individual C++ files and do not extract the ZIP into an already-existing `ImageViewer` folder.

```text
repository/
  .github/workflows/build-imageviewer.yml
  ImageViewer/
    xmake.lua
    App/
    Core/
    UI/
    Tests/
    Benchmarks/
    Packaging/
```

### Windows 11 (MSVC 2022, PowerShell)

```powershell
cd ImageViewer
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=C:/Qt/6.8.3/msvc2022_64"
xmake build -y imageviewer
xmake run imageviewer
```

### macOS (native Apple silicon)

```bash
cd ImageViewer
xmake f -c -y -p macosx -a arm64 -m release --qt="$HOME/Qt/6.8.3/macos"
xmake build -y imageviewer
xmake run imageviewer
```

### Linux (x86-64)

```bash
cd ImageViewer
xmake f -c -y -p linux -a x86_64 -m release --qt="$HOME/Qt/6.8.3/gcc_64"
xmake build -y imageviewer
xmake run imageviewer
```

Install the desktop/X11 dependencies needed by Qt, e.g. Ubuntu 24.04 `libgl-dev libegl-dev libxcb-cursor0 libxkbcommon-x11-0`. Install `nasm` if required by the native codec build. Other architectures are source targets where architecture-matched Qt and codec packages are available: Windows ARM64 (`-a arm64` and MSVC ARM64 kit), Intel macOS (`-a x86_64`) and Linux ARM64 (`-a arm64`). These extra targets are not in the CI matrix and have not been verified.

Run a file directly with `xmake run imageviewer /path/to/image.jpg`. The executable supports `--formats`, `--portable`, `--benchmark`, and a local file path (Unicode paths are supported through Qt file APIs).

## CI and installation

The included GitHub Actions workflow builds exactly three jobs: Windows x64, Linux x64, macOS ARM64. Each builds and runs metadata tests and real Qt/QML decoder integration tests before packaging. Download the native installer directly from the Actions artifacts: `.exe`, `.deb`, or `.pkg` — no nested ZIP. Manual workflow dispatch can additionally produce portable packages. Qt runtime/plugins and the decoder libraries are included in the installers; no Qt development SDK is required on the destination system. Linux targets Ubuntu 24.04-compatible systems; the `.deb` declares system libraries. macOS packages are ad-hoc signed, not notarized; Windows installers are not Authenticode signed. Use only trusted builds.

Default-app registration: Windows installer registers the supported extensions and capabilities without taking over associations. Select Image Viewer in Windows Settings → Apps → Default apps. macOS registers a viewer role via Info.plist; choose Get Info → Open with → Change All in Finder. Linux installs a MIME-aware `.desktop` file; choose Image Viewer using Open With or the desktop default-app settings. Registration never alters an existing default automatically.

Portable packages can be extracted to a writable directory and launched with `--portable`. That stores an `ImageViewerData` directory alongside the executable (alongside the `.app` on macOS), rather than writing into the application bundle. Normal installs use the OS app-local data directory. A portable package does not alter system file associations automatically.

## Controls

| Action | Default |
| --- | --- |
| Open file | Ctrl+O (Command+O on macOS) |
| Previous / next | Left / Right |
| Zoom | Mouse wheel, + / - |
| Pan | Drag with left mouse button |
| Fit / decoded 1:1 | F / 1 |
| Rotate clockwise / counterclockwise | R / Shift+R |
| Metadata | I |
| Fullscreen | F11 or double click; Esc exits |
| Play/pause animation | Space |
| Move image to trash | Delete (confirmation required) |
| Reload | Ctrl+R |

Global OS shortcuts aren't needed for an image viewer. All viewer commands listed in Settings can be reassigned or disabled. Drop a file onto the window, open it through the file dialog or the OS association. Directory traversal is case-insensitive filename order with no wraparound. The current directory is watched for changes. Trash failure does not fall back to permanent deletion. Raw file contents are never modified by zoom, rotation, or metadata viewing.

## Codecs, metadata and limits

- Native Qt readers/plugins: JPEG, PNG, BMP, TIFF, GIF, WebP, SVG, ICO, ICNS, TGA, PNM, WBMP and any additional installed image reader. Content detection handles images with missing/misleading suffixes where possible.
- libheif: standalone HEIC/HEIF primary image and AVIF, with alpha; libde265 and AOM decoder backends. HEIF image transforms, ICC and common SDR NCLX profiles are honored.
- LibRaw: camera RAW, including DNG, CR2/CR3, NEF, ARW, RAF, RW2, ORF and other models supported by LibRaw. Camera white balance, oriented demosaiced RGB preview and camera EXIF fields.
- stb: merged RGB PSD composites (8-bit and supported 16-bit composites). Layer editing, PSB, and complex unsupported PSD color modes are outside scope. Missing merged composite data may prevent viewing.
- GIF and animated WebP play one decoded frame at a time using Qt's image readers; playback is paused when hidden/minimized. Animated HEIF/AVIF sequences, APNG and every camera-specific RAW variant are not guaranteed; the viewer can show available primary/first images.
- Embedded ICC profiles are mapped to SDR sRGB for rendering; standard JPEG/TIFF/PNG/WebP EXIF orientation is honored, with an independent metadata parser fallback. Qt/LibRaw/libheif metadata and bounded TIFF/EXIF record extraction supply camera make/model, lens, date, aperture, exposure, focal length, ISO and other supported text fields. This is not a full EXIF editor or an HDR/wide-gamut desktop color-management system.
- Image files up to 2 GiB are supported where the decoder and OS allow. Display images are capped at 8,192 pixels per edge and 32 megapixels. Where possible the Qt decoder scales before allocating pixels. Non-scalable full decodes (HEIF/RAW/PSD included) are limited to ~100 megapixels, and Qt has a 512 MiB image allocation guard. An oversized image may be displayed as a clearly labeled scaled preview or rejected instead of exhausting RAM. `1:1` refers to the displayed decoded pixels, not original pixels if reduced. Large images are not tiled for arbitrary-resolution inspection.

## Implementation / performance

`Core/Decoder` performs format detection, pixel decode, metadata and color preparation. `Core/Metadata` is a bounded standalone TIFF/EXIF parser with JPEG/PNG/WebP/PSD container support. `App/Controller` owns a single background worker with latest-request-wins foreground scheduling and an invalidation generation; stale results never overwrite newer selections. Directory enumeration is asynchronous. Preload prioritizes the next and previous images, skips unusually expensive formats/files, and keeps a 128 MiB pixel LRU budget limited to nearby images. File-size and modification fingerprints invalidate stale entries. Image buffers use Qt implicit sharing (and external decoder buffer ownership when feasible); no temporary PNG or base64 is created during viewing. QML `Image` uses Qt Quick scene-graph accelerated transforms without re-decoding during zoom/pan/rotation. A single animation frame is decoded per job and no frame cache is retained. Idle workers expire; file watcher is event-driven and there is no idle redraw timer.

The 128 MiB cache limit is not a whole-process RAM guarantee: Qt, active decoded images, GPU textures, decoder scratch buffers and a directory filename list have additional cost. Common small images should occupy far less than the maximum. There is intentionally no fabricated performance claim.

## Updates

Optional update feed is configured in Settings; check/download runs automatically on startup (after 1.5 s) and every six hours. A trusted HTTPS JSON manifest identifies the newer version and per-platform package URLs and SHA-256 checksums. The package is streamed, size-limited and verified before exposing an explicit install button. No feed means no outbound requests. `Packaging/release-example.json` shows the structure, not a live server. Publish real release packages and hashes yourself, and set the trusted URL. HTTPS and a checksum from the same feed are not a substitute for code signing. No automatic installer execution or administrative privilege escalation.

## Tests

```bash
xmake build -y imageviewer-metadata-tests
xmake run imageviewer-metadata-tests
xmake build -y imageviewer-tests
xmake run imageviewer-tests
xmake build -y imageviewer-bench
xmake run imageviewer-bench
```

For Linux CI, run `xvfb-run -a xmake run imageviewer-tests` with `QT_QUICK_BACKEND=software`; use an isolated test desktop. Pure C++ metadata tests can also run without Qt or Xmake: `g++ -std=c++20 -O2 Core/Metadata.cpp Tests/MetadataTests.cpp -o metadata-tests && ./metadata-tests` from `ImageViewer`.

See `VERIFICATION.md` for executed tests and the like-for-like benchmarking procedure. All advanced codec fixtures are generated/synthetic, including HEIC, AVIF, merged PSD and Bayer DNG. No private photos or network fixtures are needed.
