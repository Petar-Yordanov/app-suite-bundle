#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'screenshottool.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/ScreenshotTool.app
else
  exe="$(find build -type f -name screenshottool -print -quit)"
  test -n "$exe"
  mkdir -p dist/ScreenshotTool.app/Contents/MacOS
  cp "$exe" dist/ScreenshotTool.app/Contents/MacOS/screenshottool
  cp Packaging/Info.plist dist/ScreenshotTool.app/Contents/Info.plist
fi
plist=dist/ScreenshotTool.app/Contents/Info.plist
# Install the agent-style application bundle metadata.
cp Packaging/Info.plist "$plist"
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key $APP_VERSION" "$plist"
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/ScreenshotTool.app -qmldir="$PWD/UI" -always-overwrite
mkdir -p dist/ScreenshotTool.app/Contents/Resources
cp README.md THIRD_PARTY.md dist/ScreenshotTool.app/Contents/Resources/
codesign --force --deep --sign - dist/ScreenshotTool.app
codesign --verify --deep --strict dist/ScreenshotTool.app
productbuild --component dist/ScreenshotTool.app /Applications artifacts/ScreenshotTool-macos-arm64.pkg
if [[ "$PORTABLE" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/ScreenshotTool.app artifacts/ScreenshotTool-macos-arm64-portable.zip
fi
