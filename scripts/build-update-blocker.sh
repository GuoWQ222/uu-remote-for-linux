#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-update-blocker.S"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe"

for command_name in \
    x86_64-w64-mingw32-as \
    x86_64-w64-mingw32-ld; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf '缺少 %s。Ubuntu/Debian 请安装：\n' "$command_name" >&2
        printf '  sudo apt install binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev\n' >&2
        exit 1
    fi
done

build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT

x86_64-w64-mingw32-as --64 "$source_file" \
    -o "$build_dir/uu-remote-update-blocker.o"
x86_64-w64-mingw32-ld \
    --no-insert-timestamp \
    --entry=WinMainCRTStartup \
    --subsystem windows \
    -o "$build_dir/uu-remote-update-blocker.exe" \
    "$build_dir/uu-remote-update-blocker.o" \
    -lkernel32

install -Dm0644 "$build_dir/uu-remote-update-blocker.exe" "$output_file"
sha256sum "$output_file"
