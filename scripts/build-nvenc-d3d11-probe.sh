#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-nvenc-d3d11-probe.c"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe"
readonly nvenc_include_dir="${NVENC_INCLUDE_DIR:-/usr/include/ffnvcodec}"

export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-0}"

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/uu-remote-nvenc-probe.XXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT
[[ -r "$nvenc_include_dir/nvEncodeAPI.h" ]] || {
    printf '缺少 %s/nvEncodeAPI.h；请安装 libffmpeg-nvenc-dev。\n' \
        "$nvenc_include_dir" >&2
    exit 1
}

x86_64-w64-mingw32-gcc \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$nvenc_include_dir" \
    -Wl,--no-insert-timestamp \
    "$source_file" \
    -o "$build_dir/uu-remote-nvenc-d3d11-probe.exe" \
    -ld3d11 -ldxgi -luuid
x86_64-w64-mingw32-strip --strip-unneeded \
    "$build_dir/uu-remote-nvenc-d3d11-probe.exe"
install -Dm0644 "$build_dir/uu-remote-nvenc-d3d11-probe.exe" "$output_file"

printf '%s\n' "$output_file"
sha256sum "$output_file"
