#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
readonly package="$project_root/dist/uu-remote-for-linux_${version}_amd64.deb"

for command_name in dpkg-deb sha256sum; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'SKIP universal release asset（缺少 %s）\n' "$command_name"
        exit 0
    }
done

"$project_root/packaging/build-deb.sh" >/dev/null
"$project_root/packaging/release/build-assets.sh" >/dev/null
[[ -f $package && ! -L $package ]]
[[ ! -e $project_root/dist/uu-remote-for-linux-x11_${version}_amd64.deb ]]
[[ ! -e $project_root/dist/uu-remote-for-linux-wayland_${version}_amd64.deb ]]
[[ ! -e $project_root/dist/uu-remote-for-linux-wayland_${version}_amd64.zip ]]
[[ $(dpkg-deb -f "$package" Package) == uu-remote-for-linux ]]
[[ $(dpkg-deb -f "$package" Version) == "$version" ]]
[[ $(dpkg-deb -f "$package" Architecture) == amd64 ]]

depends=$(dpkg-deb -f "$package" Depends)
grep -Fq 'libx11-6' <<<"$depends"
grep -Fq 'libxtst6' <<<"$depends"
grep -Fq 'xdg-desktop-portal' <<<"$depends"
grep -Fq 'xdg-desktop-portal-gnome' <<<"$depends"
grep -Fq 'libpipewire-0.3-0' <<<"$depends"
grep -Fq 'libegl1' <<<"$depends"
if grep -Eqi 'mutter|gir1\.2-mutter|libmutter' <<<"$depends"; then
    printf '通用安装包不应依赖定制 Mutter 软件包。\n' >&2
    exit 1
fi

tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT
dpkg-deb -x "$package" "$tmp_dir/package"
if find "$tmp_dir/package" -iname '*mutter*' -o -iname '*keyring*' | \
    grep -q .; then
    printf '通用安装包不应包含 Mutter 或 APT keyring 载荷。\n' >&2
    exit 1
fi
[[ -x $tmp_dir/package/usr/bin/uu-remote-for-linux ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-input-bridge ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-pipewire-capture ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-encoder-policy ]]

first_sha=$(sha256sum "$package")
first_sha=${first_sha%% *}
"$project_root/packaging/build-deb.sh" >/dev/null
"$project_root/packaging/release/build-assets.sh" >/dev/null
[[ $(sha256sum "$package" | cut -d' ' -f1) == "$first_sha" ]]
(
    cd "$project_root/dist"
    [[ $(wc -l <SHA256SUMS) -eq 1 ]]
    sha256sum --strict -c SHA256SUMS >/dev/null
)

printf 'X11/Wayland 通用单包测试通过。\n'
