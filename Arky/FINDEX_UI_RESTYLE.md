# Arky 0.8.0 — Findex component and visual system

## Reference

Styling derived from the user-provided `findex-docker-navigation-fix(1).zip` source,
particularly `AppTheme.qml`, `Metrics.qml`, `Typography.qml`, and the foundation
components / icon resources. Findex's MIT license is included as
`FINDEX_LICENSE.txt` for attribution and reuse.

## Visual changes

- Preserves Arky's traditional archive-manager structure: custom title bar,
  menu strip, action toolbar, address bar, filters, file details table, and footer.
- Uses Findex's **light theme by default**, matching the source's `mode: "Light"`.
  Dark appearance is available from **Options**.
- Uses Findex's actual color tokens and compact radius/spacing/typography scales.
- Bundles both light and dark monochrome Findex PNG icon sets; removes emojis
  from the window controls, action toolbar, file rows, and empty state.
- Adds shared, Findex-inspired QML controls in `src/qml/ui/`:
  `FButton`, `FIcon`, `FWindowButton`, `FMenuTrigger`, `FMenu`, `FMenuItem`,
  `FMenuSeparator`, `FComboBox`, `FCheckBox`, `FTextField`, `FTextArea`,
  `FDialog`, `FToolTip`, `FScrollBar`, and `FBusyIndicator`.
- All archive operations continue to invoke the existing C++ controller.
- Adds Findex-style native edge resize areas to the frameless window.
- Uses Findex-inspired compact table rows, headings, selections and hover colors.
- Dialogs use a unified custom header, controls, and action footer rather than
  Basic's unstyled buttons.
- Toolbar and file-entry hints use the same small themed tooltip component.

## Files changed

- `src/qml/main.qml`
- `src/qml/qml.qrc`
- `src/qml/theme/*` (from Findex source)
- `src/qml/ui/*` (shared themed controls)
- `src/qml/assets/icons/{light,dark}/*` (from Findex source)
- `scripts/source-check.py`
- `xmake.lua` (version update)

## Verification in this environment

- `scripts/source-check.py`: PASS (resources, QML structure, bindings and imports).
- C++ archive engine tests: PASS (GCC and system libarchive).
- Portable transfer-planning tests: PASS.
- Qt Quick/Windows build and GUI launch: **not executed** (Qt SDK unavailable).

## Build on Windows

In a Visual Studio x64 Native Tools shell:

```powershell
xmake f -p windows -a x64 -m release -y
xmake -r
xmake run Arky
```

Only the interface was restyled; the compression engine, format capabilities and
native MIME implementation were not modified by this pass.

### Scope and remaining limitations

This is a Findex-aligned Qt Quick design pass, not a pixel-for-pixel clone of its
entire file manager or a claim of a successful Windows rendering test. Actual
Qt 6.9.1 QML import/runtime diagnostics will be available after launch.
