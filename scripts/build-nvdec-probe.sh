#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-nvdec-probe.c"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-nvdec-probe"

[[ -r /usr/include/ffnvcodec/dynlink_cuviddec.h ]] || {
    printf '缺少 NVDEC 开发头文件；Ubuntu/Debian 请安装 libffmpeg-nvenc-dev。\n' >&2
    exit 1
}

build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$source_file" -ldl -o "$build_dir/uu-remote-nvdec-probe"
strip --strip-unneeded "$build_dir/uu-remote-nvdec-probe"
install -Dm0755 "$build_dir/uu-remote-nvdec-probe" "$output_file"

printf '%s\n' "$output_file"
