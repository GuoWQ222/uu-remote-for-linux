#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly archive="$project_root/third_party/sources/nvenc-nvcuvid-v0.5.tar.xz"
readonly overlay="$project_root/third_party/nvenc/uu-remote-nvenc-bridge.h"
readonly bridge_patch="$project_root/third_party/nvenc/nvencodeapi-uu-remote.patch"
readonly destination="$project_root/lib/uu-remote-for-linux/hwdecode/wine"

build_root=$(mktemp -d "${TMPDIR:-/tmp}/uu-remote-nvencode.XXXXXX")
trap 'rm -rf -- "$build_root"' EXIT
tar -xf "$archive" -C "$build_root"
source_root="$build_root/nvenc-nvcuvid-v0.5"
install -m 0644 "$overlay" "$source_root/dlls/nvencodeapi/uu-remote-nvenc-bridge.h"
patch -d "$source_root" -p1 --forward --batch <"$bridge_patch"

meson setup \
    --cross-file "$source_root/build-wine64.txt" \
    --buildtype release \
    -Dfakedll=true \
    --prefix=/usr \
    "$build_root/build" "$source_root"
ninja -C "$build_root/build"
DESTDIR="$build_root/stage" meson install -C "$build_root/build"

install -Dm0644 \
    "$build_root/stage/usr/lib/wine/x86_64-unix/nvencodeapi64.dll.so" \
    "$destination/x86_64-unix/nvencodeapi64.dll.so"
install -Dm0644 \
    "$build_root/stage/usr/lib/wine/x86_64-windows/nvencodeapi64.dll" \
    "$destination/x86_64-windows/nvencodeapi64.dll"

sha256sum \
    "$destination/x86_64-unix/nvencodeapi64.dll.so" \
    "$destination/x86_64-windows/nvencodeapi64.dll"
