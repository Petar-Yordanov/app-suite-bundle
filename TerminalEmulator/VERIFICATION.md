# Verification

Locally verified on Linux with GCC C++20 and warnings as errors:

- Native POSIX PTY subprocess launch with actual terminal descriptors.
- Initial dimensions and resizing.
- Interactive line input and output, normal child exit.
- Working directory query, bounded input queue.
- Windows argument quoting utility (platform-independent).

The workspace does not have Xmake, the Qt 6 development SDK or libvterm headers.
The complete application, libvterm compatibility tests and Qt layout/smoke tests
have not been compiled or executed here. Windows ConPTY and macOS PTY must be
verified on their respective runners. The supplied workflow builds and runs
all three test targets, then tests the deployed application before packaging.
The GUI performance numbers are not measured yet; CI includes a Linux GUI
benchmark and the VT tests print parser throughput on each platform.
