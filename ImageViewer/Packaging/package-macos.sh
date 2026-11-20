#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'imageviewer.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/ImageViewer.app
else
  exe="$(find build -type f -name imageviewer -print -quit)"
  test -n "$exe"
  mkdir -p dist/ImageViewer.app/Contents/MacOS
  cp "$exe" dist/ImageViewer.app/Contents/MacOS/imageviewer
  cp Packaging/Info.plist dist/ImageViewer.app/Contents/Info.plist
fi
plist=dist/ImageViewer.app/Contents/Info.plist
# A complete Info.plist defines native image file associations. The bundle contains
# Xmake's actual executable name (imageviewer); all registrations are viewer-only.
cp Packaging/Info.plist "$plist"
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key $APP_VERSION" "$plist"
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/ImageViewer.app -qmldir="$PWD/UI" -always-overwrite
mkdir -p dist/ImageViewer.app/Contents/Resources
cp README.md THIRD_PARTY.md dist/ImageViewer.app/Contents/Resources/
codesign --force --deep --sign - dist/ImageViewer.app
codesign --verify --deep --strict dist/ImageViewer.app
productbuild --component dist/ImageViewer.app /Applications artifacts/ImageViewer-macos-arm64.pkg
if [[ "$PORTABLE" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/ImageViewer.app artifacts/ImageViewer-macos-arm64-portable.zip
fi
