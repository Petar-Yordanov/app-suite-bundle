#!/usr/bin/env bash
set -euo pipefail
exe="$(find build -type f -name screenshottool -print -quit)"
test -n "$exe"
mkdir -p artifacts
bundle="$PWD/dist/ScreenshotTool"
mkdir -p "$bundle/lib" "$bundle/plugins" "$bundle/qml"
cp "$exe" "$bundle/screenshottool"
for folder in platforms iconengines platformthemes xcbglintegrations wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration tls generic; do
  if [[ -d "$QT_ROOT_DIR/plugins/$folder" ]]; then cp -a "$QT_ROOT_DIR/plugins/$folder" "$bundle/plugins/"; fi
done
for module in QtQuick QtQml; do cp -a "$QT_ROOT_DIR/qml/$module" "$bundle/qml/"; done
# File dialogs and Controls are under QtQuick; QtCore/Qt.labs may be imported transitively.
for module in QtCore Qt/labs; do
  if [[ -d "$QT_ROOT_DIR/qml/$module" ]]; then mkdir -p "$bundle/qml/$(dirname "$module")"; cp -a "$QT_ROOT_DIR/qml/$module" "$bundle/qml/$(dirname "$module")/"; fi
done
# Only Qt libraries actually needed by the program, image plugins and QML modules.
while IFS= read -r -d '' binary; do
  while read -r soname path; do
    if [[ -f "$path" && "$path" == "$QT_ROOT_DIR/"* ]]; then
      cp -L -n "$path" "$bundle/lib/$soname" || true
    fi
  done < <(LD_LIBRARY_PATH="$QT_ROOT_DIR/lib" ldd "$binary" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// {print $1, $3}')
done < <(find "$bundle" -type f \( -name 'screenshottool' -o -name '*.so*' \) -not -path "$bundle/lib/*" -print0)
printf '[Paths]\nPrefix = .\nLibraries = lib\nPlugins = plugins\nQmlImports = qml\n' > "$bundle/qt.conf"
patchelf --set-rpath '$ORIGIN/lib' "$bundle/screenshottool"
while IFS= read -r -d '' library; do
  relative="$(realpath --relative-to="$(dirname "$library")" "$bundle/lib")"
  patchelf --set-rpath "\$ORIGIN/$relative" "$library"
done < <(find "$bundle/lib" "$bundle/plugins" "$bundle/qml" -type f -name '*.so*' -print0)
cp README.md THIRD_PARTY.md "$bundle/"
# Deployed runtime smoke test without SDK search paths.
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=offscreen "$bundle/screenshottool" --version
if [[ "$PORTABLE" == 1 ]]; then tar -czf artifacts/ScreenshotTool-linux-x64-portable.tar.gz -C dist ScreenshotTool; fi
root="$PWD/dist/deb-root"
mkdir -p "$root/DEBIAN" "$root/opt" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/doc/screenshottool"
mv "$bundle" "$root/opt/screenshottool"
ln -s /opt/screenshottool/screenshottool "$root/usr/bin/screenshottool"
cp Packaging/org.appsuite.screenshottool.desktop "$root/usr/share/applications/"
cp README.md THIRD_PARTY.md "$root/usr/share/doc/screenshottool/"
printf '%s\n' "Package: screenshottool" "Version: $APP_VERSION" 'Architecture: amd64' 'Maintainer: AppSuite' 'Section: graphics' 'Priority: optional' \
  'Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, libgl1, libegl1, libopengl0, libfontconfig1, libfreetype6, libdbus-1-3, libx11-6, libxrandr2, libxfixes3, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libxcb-randr0, libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libdrm2, libglib2.0-0t64, libssl3t64, libz1, libgomp1, desktop-file-utils' \
  'Description: Lightweight desktop screenshot utility' ' Native Qt Quick clipboard-first screenshot utility.' > "$root/DEBIAN/control"
dpkg-deb --root-owner-group --build "$root" artifacts/ScreenshotTool-linux-x64.deb
dpkg-deb --info artifacts/ScreenshotTool-linux-x64.deb
