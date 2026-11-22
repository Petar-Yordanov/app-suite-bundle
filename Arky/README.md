# Arky 0.9.0

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

Arky is a crossplatform archive manager supporting a range of formats.

C++20, Qt 6 Quick/QML, Xmake, libarchive and libzip. Continues Arky 0.8.2,
retaining its Findex-inspired design and streaming backend. The old README,
batch status and test result files are historical, not verification of 0.9.0.

## Build locally

Install Xmake, a C++20 compiler and Qt 6.8.3 (desktop SDK with Qt Quick).
Xmake resolves libarchive and libzip. First-time dependency installation
requires internet access. Adjust Qt SDK paths to your installation.

### Windows 11 x64

Use PowerShell and Visual Studio 2022 C++ desktop tools.

```powershell
cd Arky
$env:QT_ROOT_DIR = "C:\Qt\6.8.3\msvc2022_64"
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR"
xmake build -y
xmake run Arky
```

Run all tests with:

```powershell
pwsh -File scripts/verify-windows.ps1 -QtSdk $env:QT_ROOT_DIR
```

To build the installer (Inno Setup 6 required):

```powershell
pwsh -File Packaging/package-windows.ps1
```

### macOS Apple Silicon

Install Xcode command-line tools, Xmake and a Qt macOS SDK.

```bash
cd Arky
export QT_ROOT_DIR="$HOME/Qt/6.8.3/macos"
xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR"
xmake build -y
xmake run Arky
```

Run `bash Packaging/package-macos.sh` to make the unsigned .pkg.

### Linux x86-64

Install GCC/Clang with C++20 support, Xmake and the Qt desktop SDK.
The workflow installs Qt runtime prerequisites for Ubuntu 24.04; on
other distributions install equivalent XCB, Wayland and OpenGL packages.

```bash
cd Arky
export QT_ROOT_DIR="$HOME/Qt/6.8.3/gcc_64"
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR"
xmake build -y
xmake run Arky
```

Run `bash Packaging/package-linux.sh` to make the Ubuntu 24.04-compatible
.deb (requires patchelf and dpkg-deb).

### Tests on Linux/macOS

```bash
xmake run archive_tests "$PWD/tests/fixtures/password.zip"
xmake run transfer_plan_tests
xmake run mime_tests
xmake run Arky --smoke-test
python3 scripts/source-check.py
```

On headless Linux prefix the GUI and MIME tests with `xvfb-run -a`.
For packaging smoke tests use `QT_QPA_PLATFORM=offscreen` and
`QT_QUICK_BACKEND=software`. These do not replace desktop integration tests.
The fixture password is `arky-fixture-password`; this deliberately encrypted
synthetic archive is test data, not an encryption feature in Arky.

## Archive behavior

- Read formats provided by libarchive, including ZIP, 7z, RAR, TAR,
  compressed TAR, CPIO, CAB and supported compressed streams.
- Create ZIP, 7z, TAR, TAR.GZ, TAR.XZ and TAR.ZST. Compression levels and
  methods are negotiated with libarchive. Configurable XZ/Zstd worker counts
  apply to writers accepting the filter option.
- ZIP modification uses libzip transactions (add, rename, remove, replace,
  create directories). Only ordinary unencrypted single-volume ZIP archives
  are editable. Other formats do not use custom archive reconstruction.
- No encrypted creation, RAR creation or split archive creation.
- Password extraction and multi-volume reading depend on format/backend.
  Numeric split (.001) and common RAR volume names are discovered when
  opening the first volume; the CLI accepts explicit volumes.
- Metadata is incrementally enumerated without retaining decompressed file
  payloads. Sequential/solid formats can require decoding/discarding data
  to reach later headers. ZIP metadata is supplemented by libzip. Unknown
  sizes and compression methods stay unknown.
- Up to eight tabs, directory and tree/list navigation, sortable/searchable
  metadata, selective extraction, integrity checking and explicit open/preview.
- A bounded sequential operation queue runs background jobs. Modification
  controls are disabled during another job, avoiding stale selections.
- Conflicts prompt for skipping, renaming, overwriting or cancelling.
  Full successful extraction can prompt to move the source to Trash.
  Selected, nested, failed, cancelled, warning-producing and multi-volume
  operations are excluded from automatic source deletion.
- Protective limits: 2 million entries, 64 GiB per decoded entry, 256 GiB
  per operation and an expansion-ratio guard. Metadata memory scales with
  retained entries; payload memory does not.

## Desktop integration and limitations

Installers register file associations; the user selects the actual default.
File-manager integration uses the headless CLI. Windows virtual file drag
streams suitable selections without staging them. On macOS/Linux, requesting
a drag-out stages selected files as local URLs; a subsequent drag transfers
them. Native macOS file promises and special Wayland compositor integration
are not implemented. Test desktop drag/drop on the actual OS.

Opening a nested archive extracts just that archive and caches it for its tab.
External file opening stages only the requested file; staged files are kept
for seven days and cleaned on subsequent launches to avoid disrupting an
external application. Clipboard/export staging is disk-backed and deferred
until explicitly requested.

Extraction validates relative paths, checks destination ancestors, blocks
unsafe symlinks/reparse points and checks available space. Safe relative
symlinks, permissions and timestamps are preserved where supported; hardlinks,
special files, advanced ACLs and xattrs are not fully supported. Preflight can
require a second scan of sequential archives. Use destinations not concurrently
modified by untrusted processes. Unsupported encrypted formats report errors.

iOS is a future phase. Source supports other desktop architectures where
compatible SDKs are available; CI builds only Windows x64, Linux x64 and
macOS ARM64. Portable bundles can be produced with PORTABLE=1 before packaging.
Launch with `Arky --portable` for local settings.

For updates, configure a trusted HTTPS manifest URL in Settings (see
release-example.json). Updates are checked automatically once configured.
Artifacts are downloaded and checked against the manifest SHA-256 before
the installer can be launched. No hosted feed or account is included.

## CLI for the file manager

```bash
arky-cli list "archive.zip"
arky-cli open "archive.zip"
arky-cli create "archive.zip" "folder" "file.txt" --level 6
arky-cli create "archive.tar.zst" "folder" --level 3 --threads 4
arky-cli extract "archive.zip" "output" --conflict rename
arky-cli extract "archive.zip" "output" --entry "folder/file.txt"
arky-cli test "archive.zip"
arky-cli add "archive.zip" "another-file.txt"
arky-cli rename "archive.zip" "another-file.txt" "renamed.txt"
arky-cli remove "archive.zip" "renamed.txt"
```

During development use `xmake run arky-cli` instead of `arky-cli`.
The open alias lists without a GUI; use Arky for interactive opening.
Exit code 0 means success, 1 operation failure, 2 invalid arguments.
Ctrl+C requests cancellation. Default conflict handling is fail.
For protected archives use --password-stdin and supply a line on stdin.

## Benchmarking

Install Python 3, psutil and bsdtar. Use a trusted archive and real desktop,
with equally warmed caches and equivalent workloads.

```bash
python3 benchmarks/compare.py --cli /path/to/arky-cli --reference bsdtar \
  --archive /path/to/large.zip --gui /path/to/Arky
```

Measures listing/extraction wall time, sampled peak resident memory and
extraction throughput. GUI mode records startup, first batch, complete
listing and maximum event-loop gap. Repeat with --reverse to help detect
cache-order bias. On macOS use the executable inside Arky.app/Contents/MacOS.
No performance numbers are claimed until measured on a real machine.

## CI

The combined .github/workflows/build.yml retains existing suite builds and
adds Arky for Windows x64, Linux x64 and macOS ARM64. It runs automated tests
and uploads the direct .exe, .deb or .pkg. Installers are unsigned/notarized.
See VERIFICATION.md for actual verification status.
