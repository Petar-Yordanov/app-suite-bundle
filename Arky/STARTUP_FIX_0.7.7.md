# Arky 0.7.7 — QML startup fix

The Windows 0.7.6 startup log identified a concrete QML error:

```
qrc:/qml/main.qml:415:37: Invalid property assignment: "implicitHeight" is a read-only property
```

## Changes

- Removed `implicitHeight: 84` from a `Flow` in `src/qml/main.qml`. Qt's positioner calculates its implicit height automatically, including when toolbar buttons wrap across rows.
- Added a source-level guard that rejects explicit `implicitHeight` / `implicitWidth` assignments on Qt Quick positioners (Flow/Row/Column/Grid).
- Updated startup error dialogs to show warnings and errors, rather than a wall of QML import DEBUG messages. The full log remains on disk.
- Bumped the version to 0.7.7.

## Verification

The portable archive and transfer tests were run on Linux with system libarchive 3.7.4. The Qt Quick GUI has **not** been launched here: this container has no Qt 6 SDK. The reported QML property assignment is corrected, but Windows runtime verification still depends on the next launch.

## Windows commands

```powershell
xmake f -p windows -a x64 -m release -y
xmake -r
xmake run Arky
```
