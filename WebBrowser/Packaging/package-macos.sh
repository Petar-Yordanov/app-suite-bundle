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
deploy_qt() (
  # macdeployqt scans SQL plugins in the SDK even when the app only uses
  # SQLite. On the disposable CI runner, exclude optional server drivers
  # before dependency scanning; restore the SDK before it is cached.
  if [[ "${GITHUB_ACTIONS:-false}" == true ]]; then
    sql_dir="$QT_ROOT_DIR/plugins/sqldrivers"
    parked="$(mktemp -d "$QT_ROOT_DIR/.webbrowser-sql.XXXXXX")"
    restore_drivers() {
      local driver
      for driver in "$parked"/*.dylib; do
        [[ ! -e "$driver" ]] || mv "$driver" "$sql_dir/"
      done
      rmdir "$parked"
    }
    trap restore_drivers EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    for driver in libqsqlodbc.dylib libqsqlpsql.dylib libqsqlmimer.dylib; do
      [[ ! -f "$sql_dir/$driver" ]] || mv "$sql_dir/$driver" "$parked/"
    done
  fi
  "$QT_ROOT_DIR/bin/macdeployqt" dist/WebBrowser.app -qmldir="$PWD/dist/deploy-qml" -libpath="$QT_ROOT_DIR/lib" -always-overwrite -codesign=-
)
deploy_qt
# Browser storage uses SQLite only; don't ship unavailable external drivers.
rm -f dist/WebBrowser.app/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib \
      dist/WebBrowser.app/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib \
      dist/WebBrowser.app/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib
test -f dist/WebBrowser.app/Contents/PlugIns/sqldrivers/libqsqlite.dylib
test -f dist/WebBrowser.app/Contents/PlugIns/platforms/libqcocoa.dylib
# The QtPositioning serial NMEA plugin needs QtSerialPort. Check the
# deployment, not just the SDK, so the installer cannot ship a broken rpath.
if find dist/WebBrowser.app/Contents/PlugIns -iname '*nmea*' -type f | grep -q .; then
  test -d dist/WebBrowser.app/Contents/Frameworks/QtSerialPort.framework
fi
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
# Hosted macOS runners can fail to resolve Chromium's mDNS ICE candidates.
# Permit loopback and use direct host candidates ONLY in the synthetic CI
# peer-to-peer test. Normal launches and local manual diagnostics retain
# Chromium's default WebRTC privacy protections.
if [[ "${GITHUB_ACTIONS:-false}" == true ]]; then
  compat_flags="${QTWEBENGINE_CHROMIUM_FLAGS:-}"
  if [[ "$compat_flags" =~ --disable-features=([^[:space:]]+) ]]; then
    disabled="${BASH_REMATCH[0]}"
    compat_flags="${compat_flags/"$disabled"/"$disabled,WebRtcHideLocalIpsWithMdns"}"
  else
    compat_flags="$compat_flags --disable-features=WebRtcHideLocalIpsWithMdns"
  fi
  env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH \
    QTWEBENGINE_CHROMIUM_FLAGS="$compat_flags --allow-loopback-in-peer-connection" \
    dist/WebBrowser.app/Contents/MacOS/webbrowser "${compat_args[@]}"
else
  env -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH dist/WebBrowser.app/Contents/MacOS/webbrowser "${compat_args[@]}"
fi
productbuild --component dist/WebBrowser.app /Applications artifacts/WebBrowser-macos-arm64.pkg
if [[ "${PORTABLE:-0}" == 1 ]]; then
  ditto -c -k --sequesterRsrc --keepParent dist/WebBrowser.app artifacts/WebBrowser-macos-arm64-portable.zip
fi
