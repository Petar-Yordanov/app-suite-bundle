#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'terminalemulator.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/TerminalEmulator.app
else
  exe="$(find build -type f -name terminalemulator -print -quit)"
  test -n "$exe"
  mkdir -p dist/TerminalEmulator.app/Contents/MacOS
  cp "$exe" dist/TerminalEmulator.app/Contents/MacOS/terminalemulator
fi
cp Packaging/Info.plist dist/TerminalEmulator.app/Contents/Info.plist
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key ${APP_VERSION:-1.0.0}" dist/TerminalEmulator.app/Contents/Info.plist
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/TerminalEmulator.app -qmldir="$PWD/UI" -always-overwrite
rm -f dist/TerminalEmulator.app/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib \
      dist/TerminalEmulator.app/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib \
      dist/TerminalEmulator.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib
test -f dist/TerminalEmulator.app/Contents/PlugIns/platforms/libqcocoa.dylib
mkdir -p dist/TerminalEmulator.app/Contents/Resources
cp README.md THIRD_PARTY.md dist/TerminalEmulator.app/Contents/Resources/
codesign --force --deep --sign - dist/TerminalEmulator.app
codesign --verify --deep --strict dist/TerminalEmulator.app
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=cocoa QT_QUICK_BACKEND=software dist/TerminalEmulator.app/Contents/MacOS/terminalemulator --smoke-test
productbuild --component dist/TerminalEmulator.app /Applications artifacts/TerminalEmulator-macos-arm64.pkg
if [[ "${PORTABLE:-0}" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/TerminalEmulator.app artifacts/TerminalEmulator-macos-arm64-portable.zip
fi
