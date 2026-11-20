# Verification

- Local standalone C++20 geometry tests: nine cases passed with GCC.
- Local Lua, workflow YAML, QRC, plist, release JSON and benchmark Python parsed successfully; Linux and macOS packaging shell scripts passed Bash syntax checks.
- The application and native platform implementations have not been compiled in this workspace because the Qt 6 development SDK and Xmake are unavailable here.
- GitHub Actions is configured to build all four targets, run automated tests and timing benchmarks, and produce Windows x64, Linux x64 and macOS ARM64 installers. No successful CI run is claimed until the workflow executes in the repository.
- Manual acceptance checks: real desktop global shortcut conflicts/rebinding, tray or CLI activation, startup after login, selected window capture, mixed-DPI monitors, region cancellation, clipboard paste after closing preview, no image file unless explicitly saved, pointer capture, permissions, Wayland compositor behavior, portable profile, and update manifest download.

No measured native capture, startup, RSS or CPU results are asserted before running the relevant benchmarks.
