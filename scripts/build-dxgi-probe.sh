#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-dxgi-probe.c"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe"

# Keep the checked-in PE artifact reproducible across local and CI builds.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-0}"

build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT

x86_64-w64-mingw32-gcc \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -Wl,--no-insert-timestamp \
    "$source_file" \
    -o "$build_dir/uu-remote-dxgi-probe.exe" \
    -ldxgi -luuid
x86_64-w64-mingw32-strip --strip-unneeded \
    "$build_dir/uu-remote-dxgi-probe.exe"
install -Dm0644 "$build_dir/uu-remote-dxgi-probe.exe" "$output_file"

printf '%s\n' "$output_file"
