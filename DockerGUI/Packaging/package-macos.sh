#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'dockergui.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/DockerGUI.app
else
  exe="$(find build -type f -name dockergui -print -quit)"
  test -n "$exe"
  mkdir -p dist/DockerGUI.app/Contents/MacOS
  cp "$exe" dist/DockerGUI.app/Contents/MacOS/dockergui
fi
cp Packaging/Info.plist dist/DockerGUI.app/Contents/Info.plist
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key ${APP_VERSION:-1.0.0}" dist/DockerGUI.app/Contents/Info.plist
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/DockerGUI.app -qmldir="$PWD/UI" -always-overwrite
# These optional SQL drivers are not used by DockerGUI and depend on
# external database client libraries that are not shipped with the Qt SDK.
rm -f dist/DockerGUI.app/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib \
      dist/DockerGUI.app/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib \
      dist/DockerGUI.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib
test -f dist/DockerGUI.app/Contents/PlugIns/platforms/libqcocoa.dylib
mkdir -p dist/DockerGUI.app/Contents/Resources
cp README.md THIRD_PARTY.md dist/DockerGUI.app/Contents/Resources/
codesign --force --deep --sign - dist/DockerGUI.app
codesign --verify --deep --strict dist/DockerGUI.app
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=cocoa QT_QUICK_BACKEND=software dist/DockerGUI.app/Contents/MacOS/dockergui --smoke-test
productbuild --component dist/DockerGUI.app /Applications artifacts/DockerGUI-macos-arm64.pkg
if [[ "${PORTABLE:-0}" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/DockerGUI.app artifacts/DockerGUI-macos-arm64-portable.zip
fi
