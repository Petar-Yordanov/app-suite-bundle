# Arky 0.7.6 — Windows runtime failure diagnostics

## Confirmed source behavior
`main.cpp` returned exit code 1 when `QQmlApplicationEngine::rootObjects()`
was empty. The old error was only written to stderr, which may be invisible
for a Windows GUI executable.

## Changes
- Add Qt message handler with `%TEMP%/Arky-startup.log`.
- Display a native Windows error message with the QML import/loading failure.
- Record executable, QML resource, QML import paths, and module presence.
- Add `--diagnose` opt-in tracing (`QML_IMPORT_TRACE`).
- Include `applicationDirPath()/qml` as a QML import path if it exists.
- On Windows, inspect the loaded `Qt6Core.dll` path and add its SDK `../qml` folder when available, addressing Xmake cache relocation.
- Prevent `--diagnose` from being treated as an archive filename.

## Verification
- Python QML source check: PASS.
- Native archive engine regression suite: PASS (on Linux).
- Native transfer-planning suite: PASS (on Linux).
- Windows compilation and runtime: NOT EXECUTED in this environment.

The exact missing Qt module or QML property will be visible in the startup log
after the user builds and runs the patched project. Do not assume any
particular module is absent before seeing the log.
