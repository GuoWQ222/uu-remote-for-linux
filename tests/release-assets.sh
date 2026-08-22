#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
readonly x11_package="$project_root/dist/uu-remote-for-linux-x11_${version}_amd64.deb"
readonly wayland_archive="$project_root/dist/uu-remote-for-linux-wayland_${version}_amd64.zip"

for command_name in dpkg-deb sha256sum unzip zip; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'SKIP direct release assets（缺少 %s）\n' "$command_name"
        exit 0
    }
done

"$project_root/packaging/build-deb.sh" >/dev/null
"$project_root/packaging/release/build-assets.sh" >/dev/null
[[ -f $x11_package && ! -L $x11_package ]]
[[ -f $wayland_archive && ! -L $wayland_archive ]]
[[ $(dpkg-deb -f "$x11_package" Package) == uu-remote-for-linux-x11 ]]
[[ $(dpkg-deb -f "$x11_package" Version) == "$version" ]]
[[ $(dpkg-deb -f "$x11_package" Architecture) == amd64 ]]
[[ $(dpkg-deb -f "$x11_package" Provides) == \
    "uu-remote-for-linux (= $version), uu-remote-for-linux-session" ]]
if dpkg-deb -f "$x11_package" Depends | \
    grep -Fq "uu-remote-for-linux (= $version)"; then
    printf 'X11 发行包仍依赖隐藏的共享运行时包。\n' >&2
    exit 1
fi

tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT
unzip -q "$wayland_archive" -d "$tmp_dir"
bundle="$tmp_dir/uu-remote-for-linux-wayland_${version}_amd64"
[[ -d $bundle && ! -L $bundle ]]
[[ -f $bundle/README.txt && -f $bundle/SHA256SUMS ]]
[[ $(find "$bundle" -maxdepth 1 -type f -name '*.deb' | wc -l) -eq 6 ]]
(
    cd "$bundle"
    sha256sum --strict -c SHA256SUMS >/dev/null
)

mapfile -t package_names < <(
    for package in "$bundle"/*.deb; do
        dpkg-deb -f "$package" Package
    done | sort
)
expected_names=(
    gir1.2-mutter-14
    libmutter-14-0
    mutter-common
    mutter-common-bin
    uu-remote-for-linux
    uu-remote-for-linux-wayland
)
[[ ${package_names[*]} == "${expected_names[*]}" ]]
wayland_package="$bundle/uu-remote-for-linux-wayland_${version}_amd64.deb"
wayland_depends=$(dpkg-deb -f "$wayland_package" Depends)
grep -Fq "uu-remote-for-linux (= $version)" <<<"$wayland_depends"
grep -Fq 'base-files (>= 13ubuntu10)' <<<"$wayland_depends"
grep -Fq 'base-files (<< 13ubuntu11~)' <<<"$wayland_depends"
grep -Fq 'gnome-shell (>= 46~)' <<<"$wayland_depends"
grep -Fq 'gnome-shell (<< 47~)' <<<"$wayland_depends"
grep -Fq 'libmutter-14-0 (>= 46.2-1ubuntu0.24.04.16+0uuremote3)' \
    <<<"$wayland_depends"
if find "$bundle" -maxdepth 1 -type f -iname '*keyring*' | grep -q .; then
    printf 'Wayland 离线包不应包含 APT keyring。\n' >&2
    exit 1
fi

first_x11_sha=$(sha256sum "$x11_package")
first_x11_sha=${first_x11_sha%% *}
first_wayland_sha=$(sha256sum "$wayland_archive")
first_wayland_sha=${first_wayland_sha%% *}
"$project_root/packaging/release/build-assets.sh" >/dev/null
[[ $(sha256sum "$x11_package" | cut -d' ' -f1) == "$first_x11_sha" ]]
[[ $(sha256sum "$wayland_archive" | cut -d' ' -f1) == \
    "$first_wayland_sha" ]]
(
    cd "$project_root/dist"
    sha256sum --strict -c SHA256SUMS >/dev/null
)

printf 'X11 单包与 Wayland 离线 ZIP 测试通过。\n'
