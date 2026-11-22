#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'systemmonitor.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/SystemMonitor.app
else
  exe="$(find build -type f -name systemmonitor -print -quit)"
  test -n "$exe"
  mkdir -p dist/SystemMonitor.app/Contents/MacOS
  cp "$exe" dist/SystemMonitor.app/Contents/MacOS/systemmonitor
fi
cp Packaging/Info.plist dist/SystemMonitor.app/Contents/Info.plist
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key ${APP_VERSION:-1.0.0}" dist/SystemMonitor.app/Contents/Info.plist
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/SystemMonitor.app -qmldir="$PWD/UI" -always-overwrite
mkdir -p dist/SystemMonitor.app/Contents/Resources
cp README.md THIRD_PARTY.md dist/SystemMonitor.app/Contents/Resources/
codesign --force --deep --sign - dist/SystemMonitor.app
codesign --verify --deep --strict dist/SystemMonitor.app
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software dist/SystemMonitor.app/Contents/MacOS/systemmonitor --smoke-test
productbuild --component dist/SystemMonitor.app /Applications artifacts/SystemMonitor-macos-arm64.pkg
if [[ "${PORTABLE:-0}" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/SystemMonitor.app artifacts/SystemMonitor-macos-arm64-portable.zip
fi
