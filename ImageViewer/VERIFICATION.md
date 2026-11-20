# Verification and benchmarking

## Executed locally

- The standalone C++20 metadata tests passed **20,052 assertions**, including malformed/truncated inputs and randomized byte buffers. They also passed with AddressSanitizer and UndefinedBehaviorSanitizer enabled (`ASAN_OPTIONS=detect_leaks=0`).
- Independent decoders verified the synthetic GIF, animated WebP, AVIF and PSD fixtures. ImageMagick also decoded the HEIC fixture and the installed LibRaw C API successfully unpacked and processed the synthetic Bayer DNG fixture. These checks establish fixture validity, not that the application's Qt/LibRaw/libheif integration has passed.
- CI YAML, shell script syntax, Info.plist, the example update manifest and the benchmark Python syntax were checked locally.

The local environment did not have Qt 6 development headers or Xmake, and SDK installation was unavailable. Consequently, the complete application has not yet been compiled, its GUI has not been exercised, and native installers and real application performance figures are **not yet verified**. The GitHub workflow builds the application and runs the native decoder/controller/QML integration tests before packaging on each runner.

Standalone metadata reproduction (from the project directory):

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror Core/Metadata.cpp Tests/MetadataTests.cpp -o /tmp/imageviewer-metadata-tests
/tmp/imageviewer-metadata-tests
```

## Decode performance

```bash
xmake build -y imageviewer-bench
xmake run imageviewer-bench /path/to/photo.jpg /path/to/large.png
```

The benchmark reports per-image decoded memory and timing across repeated decodes. Use the same source files and machine for comparisons. A built-in synthetic input is used when no paths are supplied.

## End-to-end viewer comparison

Install `psutil` into a Python environment, outside the application:

```bash
python3 -m pip install psutil
python3 Benchmarks/compare_viewers.py /path/to/photo.jpg --runs 5 --idle-seconds 5 \
  --viewer-json '["Image Viewer","/path/to/imageviewer","--benchmark","{image}"]' \
  --viewer-json '["Gwenview","/usr/bin/gwenview","{image}"]'
```

On Windows, use an installed viewer executable (e.g. IrfanView) and forward-slash paths in the JSON strings; on macOS, use the executable inside each application bundle. Close existing viewer instances and use an isolated test desktop. Launchers and single-instance process handoffs can invalidate the measurement. The script measures the process and its descendants, then terminates only those launched processes.

Compare median time to first visible image, sampled idle RSS, maximum sampled idle RSS and idle CPU (percentage of one core). For this viewer, first image timing uses a render signal; other viewers require manual observation, so human response time makes these startup figures approximate rather than directly equivalent. RSS can count shared pages more than once and excludes GPU memory. Peak sampled idle RSS is not peak startup memory. Repeat cold and warm filesystem-cache tests separately, with identical images, display resolution, and comparable cache settings. Test JPEG, PNG, HEIF, RAW and a large high-resolution image. Do not compare these measurements across unlike hardware or decoder configurations.

No measured performance comparison is claimed until native binaries have been run.

## Native acceptance checklist

- Run all native CI jobs, install each resulting package in a clean supported OS, and verify launch without a development Qt SDK.
- Test default app selection, drag-and-drop and Unicode paths; verify trash moves are reversible and failed trash operations never permanently delete a file.
- Exercise real camera RAWs, ICC-tagged/oriented images, transparent HEIF, GIF and animated WebP loops, rapid directory navigation, zoom/pan and large-image preview limits.
- Verify theme and shortcut changes persist, portable settings remain beside the app, and the cache remains bounded after sustained navigation.
- Exercise startup time and settled idle metrics on real hardware, including the GPU-backed default renderer.
- Configure a trusted test update feed, check valid and invalid package hashes, and verify that no install occurs without user action.
