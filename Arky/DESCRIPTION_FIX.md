# Arky 0.8.2 — product description fix

Description (verbatim):

> Arky is a crossplatform archive manager supporting a range of formats.

Updated locations:
- `README.md` (already updated in previous package)
- `Help > About Arky` (a real dialog replaces the old temporary Findex UI message)
- `src/app/main.cpp` (Qt application display name and version)
- `src/app/arky.rc` (new Windows Explorer executable FileDescription and version metadata)
- `xmake.lua` (compiles resource for Windows)
- `scripts/source-check.py` (regression guard for description consistency)

Windows-only file metadata is not runtime-verified here; validate after compiling with MSVC.
