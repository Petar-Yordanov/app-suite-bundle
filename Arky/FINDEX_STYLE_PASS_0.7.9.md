# Arky 0.7.9 — Findex-style pass

This pass keeps the archive-manager layout introduced in 0.7.8, but changes the styling direction and window chrome.

## Goals addressed
- Keep Arky layout
- Replace bland default styling with a more product-like UI
- Remove standard window frame/title pane
- Add custom title bar and custom window controls
- Keep actions and archive browser workflow intact

## Main QML changes
- `flags: Qt.Window | Qt.FramelessWindowHint`
- custom title bar with:
  - app mark
  - title / archive context
  - drag-to-move behavior
  - minimize / maximize / close buttons
- custom menu strip replacing `menuBar`
- darker Findex-like palette
- more coherent panels / borders / accents
- preserved archive list and action layout

## Files changed
- `src/qml/main.qml`
- `src/qml/ActionButton.qml`

## Validation done here
- `scripts/source-check.py` passed

## Not runtime-validated here
- exact frameless behavior on Windows
- maximize/restore interactions on target machine
- edge resizing behavior
