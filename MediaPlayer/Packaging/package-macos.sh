#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
arch="$(uname -m)"
[[ "$arch" != x86_64 ]] || arch=x64
: "${VLC_SDK_ROOT:?Set VLC_SDK_ROOT to VLC.app/Contents/MacOS}"
: "${VLC_INCLUDE_DIR:?Set VLC_INCLUDE_DIR to the official libVLC headers}"
bundle="$(find build -type d -iname 'mediaplayer.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/MediaPlayer.app
else
  exe="$(find build -type f -name mediaplayer -print -quit)"
  test -n "$exe"
  mkdir -p dist/MediaPlayer.app/Contents/MacOS
  cp "$exe" dist/MediaPlayer.app/Contents/MacOS/mediaplayer
  cp Packaging/Info.plist dist/MediaPlayer.app/Contents/Info.plist
fi
plist=dist/MediaPlayer.app/Contents/Info.plist
# Register video file types via LaunchServices.
cp Packaging/Info.plist "$plist"
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key $APP_VERSION" "$plist"
done
"$QT_ROOT_DIR/bin/macdeployqt" dist/MediaPlayer.app -qmldir="$PWD/UI" -always-overwrite
# Keep the upstream relative dylib/plugin layout. Deploy Qt first so its
# deployment tool doesn't rewrite the vendor runtime's install names.
ditto "$VLC_SDK_ROOT/lib" dist/MediaPlayer.app/Contents/MacOS/lib
ditto "$VLC_SDK_ROOT/plugins" dist/MediaPlayer.app/Contents/MacOS/plugins
mkdir -p dist/MediaPlayer.app/Contents/Resources
mkdir -p dist/MediaPlayer.app/Contents/Resources/VLC-licenses
for notice in "$VLC_INCLUDE_DIR"/../COPYING* "$VLC_INCLUDE_DIR"/../AUTHORS* "$VLC_INCLUDE_DIR"/../THANKS*; do
  if [[ -f "$notice" ]]; then cp "$notice" dist/MediaPlayer.app/Contents/Resources/VLC-licenses/; fi
done
cp README.md THIRD_PARTY.md dist/MediaPlayer.app/Contents/Resources/
codesign --force --deep --sign - dist/MediaPlayer.app
codesign --verify --deep --strict dist/MediaPlayer.app
env -u VLC_SDK_ROOT -u VLC_PLUGIN_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH MEDIAPLAYER_BUNDLED_ONLY=1 QT_QPA_PLATFORM=offscreen "$PWD/dist/MediaPlayer.app/Contents/MacOS/mediaplayer" --check-engine
productbuild --component dist/MediaPlayer.app /Applications artifacts/MediaPlayer-macos-$arch.pkg
if [[ "$PORTABLE" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/MediaPlayer.app artifacts/MediaPlayer-macos-$arch-portable.zip
fi
