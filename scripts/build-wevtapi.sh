#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-wevtapi.S"
readonly def_file="$project_root/src/uu-remote-wevtapi.def"
readonly output_file="$project_root/lib/uu-remote-for-linux/wevtapi.dll"

for command_name in \
    x86_64-w64-mingw32-as \
    x86_64-w64-mingw32-ld \
    x86_64-w64-mingw32-strip; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf '缺少 %s。Ubuntu/Debian 请安装：\n' "$command_name" >&2
        printf '  sudo apt install binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev\n' >&2
        exit 1
    fi
done

build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT

x86_64-w64-mingw32-as --64 "$source_file" -o "$build_dir/uu-remote-wevtapi.o"
x86_64-w64-mingw32-ld \
    --dll \
    --no-insert-timestamp \
    --entry=DllMain \
    --subsystem windows \
    -o "$build_dir/wevtapi.dll" \
    "$build_dir/uu-remote-wevtapi.o" \
    "$def_file" \
    -lkernel32
x86_64-w64-mingw32-strip --strip-unneeded "$build_dir/wevtapi.dll"

install -Dm0644 "$build_dir/wevtapi.dll" "$output_file"
sha256sum "$output_file"
