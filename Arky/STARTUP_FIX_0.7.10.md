# Arky 0.7.10 — Qt Quick startup corrections

## Windows-reported startup failure

The QML runtime rejected three inline `contentItem: ColumnLayout` declarations in `src/qml/main.qml` at the original lines 213, 226 and 239. A semicolon may separate QML property assignments, but must not separate sibling QML object declarations.

## Fixes

1. Expanded the Create Folder, Rename, and Move dialog content items into valid multiline QML declarations without object-separating semicolons.
2. Renamed the top-level theme property `border` to `frameColor` and explicitly qualified the color bindings (`win.frameColor`), avoiding potential confusion with Rectangle's `border` grouped property.
3. Added source checks for forbidden sibling-object semicolons and the ambiguous border binding.
4. Updated Xmake project version to 0.7.10.

No archive-engine APIs or backend algorithms were changed in this release.

## Verification in the build environment

- `python3 scripts/source-check.py` — PASS.
- Regression check against the exact 0.7.9 QML — detects 3 invalid sibling-object separators in the original, zero in 0.7.10.
- Archive-engine tests compiled and executed with GCC/libarchive — PASS.
- Transfer-plan tests compiled and executed — PASS.

## Important limitation

A Qt 6 SDK is not installed in this Linux workspace, so the QML engine was not executed here. The actual Windows/MSVC launch still requires verification on the user's machine.

## Windows usage

```powershell
xmake f -p windows -a x64 -m release -y
xmake -r
xmake run Arky
```

If Qt still reports a startup failure, the `%TEMP%\Arky-startup.log` file contains the actionable QML line number and exception.
