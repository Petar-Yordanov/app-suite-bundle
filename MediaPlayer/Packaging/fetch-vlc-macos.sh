#!/usr/bin/env bash
set -euo pipefail
version=3.0.24
arch="${VLC_ARCH:-$(uname -m)}"
case "$arch" in arm64) arch=arm64;; x86_64|intel|intel64) arch=intel64;; *) echo "Unsupported macOS architecture: $arch" >&2; exit 1;; esac
base="https://download.videolan.org/pub/videolan/vlc/$version"
root="$PWD/.deps/vlc"
mkdir -p "$root"
download() {
  local url="$1" output="$2" expected actual
  curl --fail --location --retry 3 --output "$output.sha256" "$url.sha256"
  expected="$(awk 'NR==1 {print tolower($1)}' "$output.sha256")"
  [[ "$expected" =~ ^[0-9a-f]{64}$ ]]
  curl --fail --location --retry 3 --output "$output" "$url"
  actual="$(shasum -a 256 "$output" | awk '{print $1}')"
  [[ "$expected" == "$actual" ]] || { echo "VLC checksum mismatch" >&2; exit 1; }
}
download "$base/macosx/vlc-$version-$arch.dmg" "$root/vlc.dmg"
download "$base/vlc-$version.tar.xz" "$root/vlc-$version.tar.xz"
tar -xf "$root/vlc-$version.tar.xz" -C "$root"
mount="$(mktemp -d)"
cleanup() { hdiutil detach "$mount" >/dev/null 2>&1 || true; rmdir "$mount" 2>/dev/null || true; }
trap cleanup EXIT
hdiutil attach -nobrowse -readonly -mountpoint "$mount" "$root/vlc.dmg"
ditto "$mount/VLC.app" "$root/VLC.app"
runtime="$root/VLC.app/Contents/MacOS"
include="$root/vlc-$version/include"
[[ -f "$runtime/lib/libvlc.dylib" || -f "$runtime/lib/libvlc.5.dylib" ]]
test -d "$runtime/plugins"
test -f "$include/vlc/vlc.h"
if [[ -n "${GITHUB_ENV:-}" ]]; then printf 'VLC_SDK_ROOT=%s\nVLC_INCLUDE_DIR=%s\n' "$runtime" "$include" >> "$GITHUB_ENV"; fi
printf 'VLC_SDK_ROOT=%s\nVLC_INCLUDE_DIR=%s\n' "$runtime" "$include"
