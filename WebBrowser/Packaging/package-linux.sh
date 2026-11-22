#!/usr/bin/env bash
set -euo pipefail
exe="$(find build -type f -name webbrowser -print -quit)"
test -n "$exe"
mkdir -p artifacts
bundle="$PWD/dist/WebBrowser"
mkdir -p "$bundle/lib" "$bundle/plugins" "$bundle/qml" "$bundle/libexec" "$bundle/resources" "$bundle/translations"
cp "$exe" "$bundle/webbrowser"
cp "$QT_ROOT_DIR/libexec/QtWebEngineProcess" "$bundle/libexec/"
cp -a "$QT_ROOT_DIR/resources/." "$bundle/resources/"
cp -a "$QT_ROOT_DIR/translations/qtwebengine_locales" "$bundle/translations/"
for folder in platforms platformthemes xcbglintegrations wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration tls generic iconengines imageformats sqldrivers position; do
  if [[ -d "$QT_ROOT_DIR/plugins/$folder" ]]; then cp -a "$QT_ROOT_DIR/plugins/$folder" "$bundle/plugins/"; fi
done
for module in QtQuick QtQml QtCore Qt/labs QtWebEngine QtWebChannel QtPositioning QtQuickControls2 QtQuickDialogs2; do
  if [[ -d "$QT_ROOT_DIR/qml/$module" ]]; then
    mkdir -p "$bundle/qml/$(dirname "$module")"
    cp -a "$QT_ROOT_DIR/qml/$module" "$bundle/qml/$(dirname "$module")/"
  fi
done
while IFS= read -r -d '' binary; do
  while read -r soname path; do
    if [[ -f "$path" && "$path" == "$QT_ROOT_DIR/"* ]]; then
      cp -L -n "$path" "$bundle/lib/$soname" || true
    fi
  done < <(LD_LIBRARY_PATH="$QT_ROOT_DIR/lib" ldd "$binary" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// {print $1, $3}')
done < <(find "$bundle" -type f \( -name 'webbrowser' -o -name 'QtWebEngineProcess' -o -name '*.so*' \) -not -path "$bundle/lib/*" -print0)
printf '[Paths]\nPrefix = .\nLibraries = lib\nPlugins = plugins\nQmlImports = qml\nLibraryExecutables = libexec\nData = .\nTranslations = translations\n' > "$bundle/qt.conf"
printf '[Paths]\nPrefix = ..\nLibraries = lib\nPlugins = plugins\nQmlImports = qml\nLibraryExecutables = libexec\nData = .\nTranslations = translations\n' > "$bundle/libexec/qt.conf"
patchelf --set-rpath '$ORIGIN/lib' "$bundle/webbrowser"
patchelf --set-rpath '$ORIGIN/../lib' "$bundle/libexec/QtWebEngineProcess"
while IFS= read -r -d '' library; do
  relative="$(realpath --relative-to="$(dirname "$library")" "$bundle/lib")"
  patchelf --set-rpath "\$ORIGIN/$relative" "$library"
done < <(find "$bundle/lib" "$bundle/plugins" "$bundle/qml" -type f -name '*.so*' -print0)
cp README.md THIRD_PARTY.md COMPATIBILITY.md REQUIREMENTS.md "$bundle/"
xvfb-run -a env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 "$bundle/webbrowser" --smoke-test
compat_args=(--compatibility-smoke)
if [[ "${WEBBROWSER_REQUIRE_CODECS:-0}" == 1 ]]; then compat_args+=(--require-codecs); fi
xvfb-run -a env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 "$bundle/webbrowser" "${compat_args[@]}"
if [[ "${PORTABLE:-0}" == 1 ]]; then tar -czf artifacts/WebBrowser-linux-x64-portable.tar.gz -C dist WebBrowser; fi
root="$PWD/dist/deb-root"
mkdir -p "$root/DEBIAN" "$root/opt" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/doc/webbrowser"
mv "$bundle" "$root/opt/webbrowser"
ln -s /opt/webbrowser/webbrowser "$root/usr/bin/webbrowser"
cp Packaging/org.appsuite.webbrowser.desktop "$root/usr/share/applications/"
cp README.md THIRD_PARTY.md COMPATIBILITY.md "$root/usr/share/doc/webbrowser/"
printf '%s\n' "Package: webbrowser" "Version: ${APP_VERSION:-1.0.0}" 'Architecture: amd64' 'Maintainer: AppSuite' 'Section: utils' 'Priority: optional' \
  'Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, libgl1, libegl1, libopengl0, libfontconfig1, libfreetype6, libdbus-1-3, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libxcb-randr0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libdrm2, libglib2.0-0t64, libssl3t64, libz1' \
  'Recommends: libnss3, libnspr4, libasound2t64, libgbm1, libxcomposite1, libxdamage1, libatk1.0-0t64, libatk-bridge2.0-0t64, libxshmfence1, libxrandr2, libxkbcommon0, libxss1' \
  'Description: Native Qt WebEngine browser' ' Lightweight browser interface with native Chromium rendering.' > "$root/DEBIAN/control"
dpkg-deb --root-owner-group --build "$root" artifacts/WebBrowser-linux-x64.deb
dpkg-deb --info artifacts/WebBrowser-linux-x64.deb
