# Verification

This source delivery includes a real Docker backend, rather than simulated
container data, with the following verification performed locally:

* C++20 HTTP response parser compiled with GCC, all tests passing
  (`-Wall -Wextra -Werror`).
* Python GUI-PID benchmark syntax checked and RSS/CPU sampling exercised on a
  non-GUI test process (not a DockerGUI performance measurement).
* Combined GitHub Actions workflow parsed as YAML; the existing Windows x64,
  Linux x64 and macOS ARM64 matrix is preserved.
* Bash packaging scripts validated with `bash -n`; QRC, macOS plist and release
  manifest validated as XML/plist/JSON respectively.

Not yet verified locally:

* Compiling or running the native Qt application, Qt model/command tests, or
  deployed installers. The execution environment lacks the Qt 6 development
  SDK and Xmake.
* Real Docker integration tests, runtime service startup, interactive terminals
  and registry sign-in. No Docker CLI/engine is installed here.
* GUI startup, idle RAM/CPU and actual rendering performance versus Docker
  Desktop. The included benchmark collects these metrics on a built binary;
  any comparison must use the same machine and engine.

GitHub CI tests the builds on all three platforms and executes the real Docker
integration suite and GUI process benchmark on Linux. Native runtime behavior
on macOS/Windows, and registry/terminal scenarios, also need testing against
the user's actual installed runtime.
