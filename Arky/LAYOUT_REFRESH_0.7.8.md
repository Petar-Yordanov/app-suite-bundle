# Arky 0.7.8 layout refresh

This checkpoint redesigns the Qt Quick UI to be closer to a classic archive manager layout such as WinRAR:

- top menu bar
- large icon toolbar
- address/path bar
- compact filter/password row
- file list as the main central workspace
- bottom status/transfer strip
- new archive options moved into a dedicated dialog instead of a permanent sidebar

Files changed:
- src/qml/main.qml
- src/qml/ActionButton.qml

Validation completed in this environment:
- source-check.py passed

Not validated here:
- actual Qt runtime on Windows
- visual polish under the Windows style engine
- exact spacing after build on target machine
