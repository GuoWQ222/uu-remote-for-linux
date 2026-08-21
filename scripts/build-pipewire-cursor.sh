#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-pipewire-cursor.c"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor"
readonly compiler="${CC:-cc}"

command -v "$compiler" >/dev/null 2>&1 || {
    printf '缺少 C 编译器：%s\n' "$compiler" >&2
    exit 1
}
command -v pkg-config >/dev/null 2>&1 || {
    printf '缺少 pkg-config。\n' >&2
    exit 1
}
pkg-config --exists libpipewire-0.3 libspa-0.2 || {
    printf '缺少 libpipewire-0.3-dev。\n' >&2
    exit 1
}

temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/uu-remote-pw-cursor.XXXXXX")
temporary="$temporary_dir/uu-remote-pipewire-cursor"
trap 'rm -rf -- "$temporary_dir"' EXIT

# shellcheck disable=SC2046
"$compiler" \
    -std=c11 \
    -O2 \
    -Wall \
    -Wextra \
    -Werror \
    -Wl,--as-needed \
    $(pkg-config --cflags libpipewire-0.3 libspa-0.2) \
    -o "$temporary" \
    "$source_file" \
    $(pkg-config --libs libpipewire-0.3 libspa-0.2)
strip --strip-unneeded "$temporary"
install -Dm0755 "$temporary" "$output_file"
printf '%s\n' "$output_file"
sha256sum "$output_file"
