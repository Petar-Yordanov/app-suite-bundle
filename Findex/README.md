# Findex

Native C++/Qt Quick file manager. Build with `xmake f -y -m release`,
`xmake build -y Findex` and `xmake run Findex`. A separately installed Qt SDK
can be selected with `--qt=/path/to/Qt`; Linux requires `libglib2.0-dev`.
Run `xmake build -y FindexCoreTests` and `xmake run FindexCoreTests` for the
core tests. `xmake run Findex --smoke-test` loads the GUI using temporary settings.

The suite workflow builds Windows x64, Ubuntu 24.04 x64 and macOS ARM64.
Installers contain the Qt runtime; no development SDK is required to run them.
Launch with a directory path to open it or `--portable` for local settings.
Select Findex as a directory handler where the desktop allows it; it does not
replace system shells or modify their protected defaults.

## App Suite integration

The repository's `.github/workflows/findex.yml` builds the three desktop
installers independently and publishes them to the rolling release.
From the repository root use `python tools/dev.py Findex` to discover Qt,
configure, build and run locally. On Linux install `libglib2.0-dev` and
`pkg-config`. Use Qt 6.10.2 or newer.
Findex uses the installed Arky CLI for creating ZIP archives and extracting
supported formats; install Arky separately. Its More menu can launch the
suite's Terminal Emulator in the current local directory. Local deletion
asks the native trash API to move files; it does not fall back to permanent
deletion. Container deletions remain remote operations.

The administrator operation dialog also supports recursive copy and explicit
permanent directory-tree removal. Removal requires a checkbox and the full
source path typed again. Roots, top-level volume directories (for recursive
removal), symlinks/reparse points, nested volume boundaries and special files
are rejected in recursive operations. Only use these operations on trusted,
stable directories: preflight inspection is not a guarantee against concurrent
hostile tree mutations. Do not use the helper as a privileged service.

The More menu also exposes the updater. CI packages use the default release
manifest; local builds may enter a trusted HTTPS URL. Opening the verified
installer requires explicit action. macOS Finder replacement and Windows
Explorer replacement are not guaranteed by the folder association registration.
