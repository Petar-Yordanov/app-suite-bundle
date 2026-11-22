#!/usr/bin/env bash
set -euo pipefail
mkdir -p dist artifacts
bundle="$(find build -type d -iname 'webbrowser.app' -print -quit)"
if [[ -n "$bundle" ]]; then
  ditto "$bundle" dist/WebBrowser.app
else
  exe="$(find build -type f -name webbrowser -print -quit)"
  test -n "$exe"
  mkdir -p dist/WebBrowser.app/Contents/MacOS
  cp "$exe" dist/WebBrowser.app/Contents/MacOS/webbrowser
fi
cp Packaging/Info.plist dist/WebBrowser.app/Contents/Info.plist
for key in CFBundleShortVersionString CFBundleVersion; do
  /usr/libexec/PlistBuddy -c "Set :$key ${APP_VERSION:-1.0.0}" dist/WebBrowser.app/Contents/Info.plist
done
mkdir -p dist/deploy-qml
cp UI/Main.qml UI/DesktopPage.qml dist/deploy-qml/
mkdir -p dist/WebBrowser.app/Contents/Resources
cp README.md THIRD_PARTY.md COMPATIBILITY.md REQUIREMENTS.md dist/WebBrowser.app/Contents/Resources/
"$QT_ROOT_DIR/bin/macdeployqt" dist/WebBrowser.app -qmldir="$PWD/dist/deploy-qml" -always-overwrite -codesign=-
helper="$(find dist/WebBrowser.app/Contents/Frameworks -name QtWebEngineProcess.app -type d -print -quit)"
test -n "$helper"
entitlements="$helper/Contents/Resources/QtWebEngineProcess.entitlements"
if [[ ! -f "$entitlements" ]]; then entitlements=Packaging/WebEngineProcess.entitlements; fi
codesign --force --sign - --entitlements "$entitlements" "$helper"
framework="$(find dist/WebBrowser.app/Contents/Frameworks -name QtWebEngineCore.framework -type d -print -quit)"
test -n "$framework"
codesign --force --sign - "$framework"
codesign --force --sign - dist/WebBrowser.app
codesign --verify --deep --strict dist/WebBrowser.app
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH dist/WebBrowser.app/Contents/MacOS/webbrowser --smoke-test
compat_args=(--compatibility-smoke)
if [[ "${WEBBROWSER_REQUIRE_CODECS:-0}" == 1 ]]; then compat_args+=(--require-codecs); fi
env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH dist/WebBrowser.app/Contents/MacOS/webbrowser "${compat_args[@]}"
productbuild --component dist/WebBrowser.app /Applications artifacts/WebBrowser-macos-arm64.pkg
if [[ "${PORTABLE:-0}" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/WebBrowser.app artifacts/WebBrowser-macos-arm64-portable.zip
fi
