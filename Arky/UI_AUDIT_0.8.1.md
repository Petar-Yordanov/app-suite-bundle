# Arky 0.8.1 — Findex UI audit and follow-up fixes

The 0.8.0 package was re-opened alongside the supplied Findex source ZIP and inspected directly.

## Issues found and fixed

1. **Legacy button was still dark/generic.** `ActionButton.qml`, retained in the QML resources for compatibility, used the old dark theme instead of the new Findex-derived control. It is now a thin compatibility wrapper around `ui/FButton.qml`.
2. **Dropdown scrollbars were still using the Basic style.** The `FComboBox` popup now uses `FScrollBar`; that component now supports horizontal as well as vertical sizing.
3. **Menu positions were hardcoded.** The File, Commands, Tools, Favorites, Options and Help popups now position themselves below their respective actual trigger items, avoiding misalignment under DPI scaling and changed menu widths.
4. **Custom controls lacked keyboard activation.** `FButton`, `FWindowButton`, `FMenuTrigger`, and `FMenuItem` now support Tab focus and Enter/Space activation, with visible focus borders. (Comprehensive keyboard menu navigation remains separate work.)
5. **Archive format selector could remain cramped.** The new-archive settings grid now fills the available dialog width.
6. **Preview scrollbars were still native/default.** Both preview scrollbars now use the shared styled scrollbar.
7. **Incomplete automated design-system auditing.** `scripts/source-check.py` now checks every statically named icon in both themes, that all `Ui.F...` components in main.qml are present in the QRC, and that no unstyled stock Qt Quick controls are instantiated in `main.qml`.

## Checked and found present

- Full Findex theme palette and metric/type tokens (including its original `mode: Light` default).
- Light/dark icon variants and resource declarations for referenced icons.
- Themed buttons, menus, menu items, checkbox, text fields, combo boxes, dialog chrome, file list, status bar, tooltips, busy indicator, scrollbars, title bar controls and edge resize zones.
- Existing archive action bindings remain present in QML.
- Findex MIT license attribution included for reused assets/theme.

## Validation performed

- `python3 scripts/source-check.py`: PASS, 148 QRC resource paths.
- Portable archive-engine regression suite: PASS, including ZIP, 7z, TAR, CPIO, encryption and safety.
- Portable transfer-planning tests: PASS.
- No changes to the C++ archive engine or Windows MIME bridge were necessary in this pass.

## Important limitations

- No Qt6 development kit/MSVC is installed in this container; the QML runtime and Windows frameless-window rendering could not be tested here.
- Source-level checks do not prove QML objects instantiate or that menus/dropdowns operate properly at runtime. The Windows build and GUI launch remain the required target verification.
- Native OS file/folder chooser dialogs intentionally keep OS-native appearance. Findex-style app-owned controls are customized.
- The light/dark theme preference is session-only. Full accessibility, keyboard menu navigation, and pixel-level matching with every Findex view are not claimed.
