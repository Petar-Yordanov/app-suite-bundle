#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'screenrecorder.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/ScreenRecorder.app
else
  exe="$(find build -type f -name screenrecorder -print -quit)"
  test -n "$exe"
  mkdir -p dist/ScreenRecorder.app/Contents/MacOS
  cp "$exe" dist/ScreenRecorder.app/Contents/MacOS/screenrecorder
  cp Packaging/Info.plist dist/ScreenRecorder.app/Contents/Info.plist
fi
plist=dist/ScreenRecorder.app/Contents/Info.plist
# Install the agent-style application bundle metadata.
cp Packaging/Info.plist "$plist"
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key ${APP_VERSION:-1.0.0}" "$plist"
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/ScreenRecorder.app -qmldir="$PWD/UI" -always-overwrite
rm -f dist/ScreenRecorder.app/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib \
      dist/ScreenRecorder.app/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib \
      dist/ScreenRecorder.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib
test -f dist/ScreenRecorder.app/Contents/PlugIns/platforms/libqcocoa.dylib
python3 Packaging/bundle-ffmpeg-macos.py "$(command -v ffmpeg)" dist/ScreenRecorder.app
mkdir -p dist/ScreenRecorder.app/Contents/Resources
cp README.md THIRD_PARTY.md dist/ScreenRecorder.app/Contents/Resources/
codesign --force --deep --sign - dist/ScreenRecorder.app
codesign --verify --deep --strict dist/ScreenRecorder.app
dist/ScreenRecorder.app/Contents/MacOS/ffmpeg -hide_banner -version
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=cocoa QT_QUICK_BACKEND=software dist/ScreenRecorder.app/Contents/MacOS/screenrecorder --smoke-test
productbuild --component dist/ScreenRecorder.app /Applications artifacts/ScreenRecorder-macos-arm64.pkg
if [[ "${PORTABLE:-0}" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/ScreenRecorder.app artifacts/ScreenRecorder-macos-arm64-portable.zip
fi
