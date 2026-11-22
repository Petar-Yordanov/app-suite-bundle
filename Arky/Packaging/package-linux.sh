#!/usr/bin/env bash
set -euo pipefail
exe="$(find build -type f -name Arky -print -quit)"
cli="$(find build -type f -name arky-cli -print -quit)"
test -n "$exe" && test -n "$cli"
mkdir -p artifacts
arch="$(dpkg --print-architecture)"
[[ "$arch" != amd64 ]] || arch=x64
bundle="$PWD/dist/Arky"
mkdir -p "$bundle/lib" "$bundle/plugins" "$bundle/qml"
cp "$exe" "$bundle/Arky"
cp "$cli" "$bundle/arky-cli"
for folder in platforms iconengines platformthemes xcbglintegrations wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration tls generic; do
  if [[ -d "$QT_ROOT_DIR/plugins/$folder" ]]; then cp -a "$QT_ROOT_DIR/plugins/$folder" "$bundle/plugins/"; fi
done
for module in QtQuick QtQml QtCore Qt/labs; do
  if [[ -d "$QT_ROOT_DIR/qml/$module" ]]; then
    mkdir -p "$bundle/qml/$(dirname "$module")"
    cp -a "$QT_ROOT_DIR/qml/$module" "$bundle/qml/$(dirname "$module")/"
  fi
done
while IFS= read -r -d '' binary; do
  while read -r soname path; do
    if [[ -f "$path" && ( "$path" == "$QT_ROOT_DIR/"* || "$path" == "$HOME/.xmake/"* ) ]]; then
      cp -L -n "$path" "$bundle/lib/$soname" || true
    fi
  done < <(LD_LIBRARY_PATH="$QT_ROOT_DIR/lib" ldd "$binary" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// {print $1, $3}')
done < <(find "$bundle" -type f \( -name Arky -o -name arky-cli -o -name '*.so*' \) -not -path "$bundle/lib/*" -print0)
printf '[Paths]\nPrefix = .\nLibraries = lib\nPlugins = plugins\nQmlImports = qml\n' > "$bundle/qt.conf"
patchelf --set-rpath '$ORIGIN/lib' "$bundle/Arky"
patchelf --set-rpath '$ORIGIN/lib' "$bundle/arky-cli"
while IFS= read -r -d '' library; do
  relative="$(realpath --relative-to="$(dirname "$library")" "$bundle/lib")"
  patchelf --set-rpath "\$ORIGIN/$relative" "$library"
done < <(find "$bundle/lib" "$bundle/plugins" "$bundle/qml" -type f -name '*.so*' -print0)
cp README.md FINDEX_LICENSE.txt "$bundle/"
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QML_IMPORT_PATH QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$bundle/Arky" --smoke-test
"$bundle/arky-cli" --help
if [[ "${PORTABLE:-0}" == 1 ]]; then tar -czf "artifacts/Arky-linux-$arch-portable.tar.gz" -C dist Arky; fi
root="$PWD/dist/deb-root"
mkdir -p "$root/DEBIAN" "$root/opt" "$root/usr/bin" "$root/usr/share/applications" "$root/usr/share/doc/arky"
mv "$bundle" "$root/opt/arky"
ln -s /opt/arky/Arky "$root/usr/bin/arky"
ln -s /opt/arky/arky-cli "$root/usr/bin/arky-cli"
cp Packaging/org.appsuite.arky.desktop "$root/usr/share/applications/"
cp README.md FINDEX_LICENSE.txt "$root/usr/share/doc/arky/"
printf '%s\n' "Package: arky" "Version: ${ARKY_VERSION:-0.9.0}" "Architecture: $(dpkg --print-architecture)" 'Maintainer: AppSuite' 'Section: utils' 'Priority: optional' \
'Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, libgl1, libegl1, libopengl0, libfontconfig1, libfreetype6, libdbus-1-3, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libxcb-randr0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libdrm2, libglib2.0-0t64, libssl3t64, libz1, liblzma5, libzstd1, libbz2-1.0, libgomp1, desktop-file-utils' \
'Description: Lightweight archive manager' ' Qt Quick frontend with libarchive and transactional ZIP editing.' > "$root/DEBIAN/control"
dpkg-deb --root-owner-group --build "$root" "artifacts/Arky-linux-$arch.deb"
