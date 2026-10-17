#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly archive="$project_root/third_party/sources/nvcuda-uu-remote-v0.8.tar.xz"
readonly destination="$project_root/lib/uu-remote-for-linux/hwdecode/wine"

for command_name in meson ninja strip readelf; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf '缺少 nvcuda 构建命令：%s\n' "$command_name" >&2
        exit 1
    }
done

build_root=$(mktemp -d "${TMPDIR:-/tmp}/uu-remote-nvcuda.XXXXXX")
trap 'rm -rf -- "$build_root"' EXIT
tar -xf "$archive" -C "$build_root"
source_root="$build_root/nvcuda-uu-remote-v0.8"

meson setup \
    --cross-file "$source_root/build-wine64.txt" \
    --buildtype release \
    -Dfakedll=true \
    -Dc_link_args="['-m64','-mwindows','-Wl,-z,relro,-z,now']" \
    --prefix=/usr \
    "$build_root/build" "$source_root"
ninja -C "$build_root/build"
DESTDIR="$build_root/stage" meson install -C "$build_root/build"

unix_relay="$build_root/stage/usr/lib/wine/x86_64-unix/nvcuda.dll.so"
windows_relay="$build_root/stage/usr/lib/wine/x86_64-windows/nvcuda.dll"
strip --strip-unneeded --remove-section=.comment "$unix_relay"
# Capture before grep -q so pipefail cannot report SIGPIPE as a missing flag.
unix_relay_dynamic=$(readelf -d "$unix_relay")
grep -Eq 'BIND_NOW|FLAGS_1.*NOW' <<<"$unix_relay_dynamic" || {
    printf 'nvcuda Unix relay 缺少 BIND_NOW。\n' >&2
    exit 1
}
install -Dm0644 "$unix_relay" \
    "$destination/x86_64-unix/nvcuda.dll.so"
install -Dm0644 "$windows_relay" \
    "$destination/x86_64-windows/nvcuda.dll"

sha256sum \
    "$destination/x86_64-unix/nvcuda.dll.so" \
    "$destination/x86_64-windows/nvcuda.dll"
