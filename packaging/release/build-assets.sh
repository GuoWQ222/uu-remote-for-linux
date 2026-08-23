#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
readonly arch=amd64
readonly output_dir="$project_root/dist"
readonly base_package="$output_dir/uu-remote-for-linux_${version}_${arch}.deb"

for command_name in dpkg-deb sha256sum; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf '缺少发行构建工具: %s\n' "$command_name" >&2
        exit 69
    }
done
[[ -f $base_package && ! -L $base_package ]] || {
    printf '缺少基础运行时包；请先运行 make deb。\n' >&2
    exit 66
}
mkdir -p "$output_dir"

# X11 and Wayland use the same payload. The launcher selects XTest or the
# Portal/DMA-BUF path from the actual user session at runtime, where the
# session environment is reliable. Remove same-version session wrappers so a
# release can expose exactly one unambiguous download.
rm -f -- \
    "$output_dir/uu-remote-for-linux-x11_${version}_${arch}.deb" \
    "$output_dir/uu-remote-for-linux-wayland_${version}_${arch}.deb" \
    "$output_dir/uu-remote-for-linux-wayland_${version}_${arch}.zip"

[[ $(dpkg-deb -f "$base_package" Package) == uu-remote-for-linux ]]
[[ $(dpkg-deb -f "$base_package" Version) == "$version" ]]
[[ $(dpkg-deb -f "$base_package" Architecture) == "$arch" ]]

(
    cd "$output_dir"
    sha256sum "$(basename "$base_package")" >SHA256SUMS
)
printf '%s\n' "$base_package"
sha256sum "$base_package"
