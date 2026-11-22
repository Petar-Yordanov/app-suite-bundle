#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
arch="$(uname -m)"
[[ "$arch" != x86_64 ]] || arch=x64
bundle="$(find build -type d -iname 'Arky.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/Arky.app
else
  exe="$(find build -type f -name Arky -print -quit)"
  test -n "$exe"
  mkdir -p dist/Arky.app/Contents/MacOS
  cp "$exe" dist/Arky.app/Contents/MacOS/Arky
fi
cli="$(find build -type f -name arky-cli -print -quit)"
test -n "$cli"
cp "$cli" dist/Arky.app/Contents/MacOS/arky-cli
cp Packaging/Info.plist dist/Arky.app/Contents/Info.plist
"$QT_ROOT_DIR/bin/macdeployqt" dist/Arky.app -qmldir="$PWD/src/qml" -always-overwrite -executable="$PWD/dist/Arky.app/Contents/MacOS/arky-cli"
rm -f dist/Arky.app/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib \
      dist/Arky.app/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib \
      dist/Arky.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib
test -f dist/Arky.app/Contents/PlugIns/platforms/libqcocoa.dylib
mkdir -p dist/Arky.app/Contents/Resources
cp README.md FINDEX_LICENSE.txt dist/Arky.app/Contents/Resources/
codesign --force --deep --sign - dist/Arky.app
codesign --verify --deep --strict dist/Arky.app
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=cocoa QT_QUICK_BACKEND=software "$PWD/dist/Arky.app/Contents/MacOS/Arky" --smoke-test
"$PWD/dist/Arky.app/Contents/MacOS/arky-cli" --help
productbuild --component dist/Arky.app /Applications "artifacts/Arky-macos-$arch.pkg"
if [[ "${PORTABLE:-0}" == 1 ]]; then ditto -c -k --sequesterRsrc --keepParent dist/Arky.app "artifacts/Arky-macos-$arch-portable.zip"; fi
