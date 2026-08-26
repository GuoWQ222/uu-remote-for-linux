#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
release_date=$(sed -n -E \
    "s/^## \\[$version\\] - ([0-9]{4}-[0-9]{2}-[0-9]{2})$/\\1/p" \
    "$project_root/CHANGELOG.md" | head -n1)
[[ $release_date =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]
readonly version release_date
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
grep -Fq 'libc6 (>= 2.34)' <<<"$depends"
grep -Fq 'pkexec' <<<"$depends"
grep -Fq 'libx11-6' <<<"$depends"
grep -Fq 'libxtst6' <<<"$depends"
grep -Fq 'xdg-desktop-portal' <<<"$depends"
grep -Fq 'xdg-desktop-portal-gnome' <<<"$depends"
grep -Fq 'libpipewire-0.3-0' <<<"$depends"
grep -Fq 'libegl1' <<<"$depends"
if grep -Eq '(^|, )(bash|coreutils|findutils|policykit-1|tar|util-linux)(,|$)' \
    <<<"$depends"; then
    printf 'Debian Depends 不应包含 Essential 包或过时的 policykit-1。\n' >&2
    exit 1
fi
if grep -Eqi 'mutter|gir1\.2-mutter|libmutter' <<<"$depends"; then
    printf '通用安装包不应依赖定制 Mutter 软件包。\n' >&2
    exit 1
fi

tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT
dpkg-deb -x "$package" "$tmp_dir/package"
dpkg-deb -e "$package" "$tmp_dir/control"
expected_installed_size=$(
    find "$tmp_dir/package/usr" -printf '%y\t%s\t%D:%i\t%n\n' | \
        LC_ALL=C awk -F '\t' '
            $1 == "f" || $1 == "l" {
                if ($4 <= 1 || !seen[$3]++)
                    total += int(($2 + 1023) / 1024)
                next
            }
            { total += 1 }
            END { print total }
        '
)
[[ $(dpkg-deb -f "$package" Installed-Size) == "$expected_installed_size" ]]
if find "$tmp_dir/package" -iname '*mutter*' -o -iname '*keyring*' | \
    grep -q .; then
    printf '通用安装包不应包含 Mutter 或 APT keyring 载荷。\n' >&2
    exit 1
fi
[[ -x $tmp_dir/package/usr/bin/uu-remote-for-linux ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-input-bridge ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-second-screen-bridge ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-x11-virtual-display ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-virtual-displayd ]]
[[ -x $tmp_dir/package/usr/bin/uu-remote-virtual-displayctl ]]
[[ -r $tmp_dir/package/usr/lib/systemd/user/uu-remote-virtual-display.service ]]
grep -Fq 'ExecStart=/usr/lib/uu-remote-for-linux/uu-remote-virtual-displayd' \
    "$tmp_dir/package/usr/lib/systemd/user/uu-remote-virtual-display.service"
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-pipewire-capture ]]
[[ -x $tmp_dir/package/usr/lib/uu-remote-for-linux/uu-remote-encoder-policy ]]
[[ $(stat -c %a \
    "$tmp_dir/package/usr/lib/uu-remote-for-linux/hwdecode/wine/x86_64-unix/nvcuda.dll.so") == 644 ]]
[[ -r $tmp_dir/package/usr/share/doc/uu-remote-for-linux/changelog.gz ]]
[[ -r $tmp_dir/package/usr/share/man/man1/uu-remote-for-linux.1.gz ]]
[[ -r $tmp_dir/package/usr/share/man/man1/uu-remote-virtual-displayctl.1.gz ]]
[[ -r $tmp_dir/package/usr/share/doc/uu-remote-for-linux/copyright ]]
grep -Fq 'Copyright: 2026 Wenqi Guo' \
    "$tmp_dir/package/usr/share/doc/uu-remote-for-linux/copyright"
[[ ! -e $tmp_dir/package/usr/lib/uu-remote-for-linux/hwdecode/licenses ]]
[[ -r $tmp_dir/package/usr/share/doc/uu-remote-for-linux/licenses/DXVK-Zlib.txt ]]
[[ -s $tmp_dir/control/md5sums ]]
(
    cd "$tmp_dir/package"
    md5sum --strict -c "$tmp_dir/control/md5sums" >/dev/null
)
gzip -t "$tmp_dir/package/usr/share/doc/uu-remote-for-linux/changelog.gz"
gzip -t "$tmp_dir/package/usr/share/man/man1/uu-remote-for-linux.1.gz"
gzip -dc "$tmp_dir/package/usr/share/man/man1/uu-remote-for-linux.1.gz" \
    >"$tmp_dir/manpage"
grep -Fq "uu-remote-for-linux $version" "$tmp_dir/manpage"
grep -Fq "\"$release_date\"" "$tmp_dir/manpage"
gzip -dc "$tmp_dir/package/usr/share/doc/uu-remote-for-linux/changelog.gz" \
    >"$tmp_dir/debian-changelog"
[[ $(dpkg-parsechangelog -l"$tmp_dir/debian-changelog" -SVersion) == \
    "$version" ]]

for relay in nvcuda.dll.so nvcuvid.dll.so nvencodeapi64.dll.so; do
    relay_path="$tmp_dir/package/usr/lib/uu-remote-for-linux/hwdecode/wine/x86_64-unix/$relay"
    file "$relay_path" | grep -Fq 'stripped'
    readelf -d "$relay_path" | grep -Eq 'BIND_NOW|FLAGS_1.*NOW'
    if readelf -S "$relay_path" | grep -q '\.comment'; then
        printf '%s 不应保留 .comment 段。\n' "$relay" >&2
        exit 1
    fi
done
for native_binary in \
    uu-remote-frame-helper.so \
    uu-remote-nvdec-probe \
    uu-remote-pipewire-capture \
    uu-remote-pipewire-cursor; do
    if readelf -S \
        "$tmp_dir/package/usr/lib/uu-remote-for-linux/$native_binary" | \
        grep -q '\.comment'; then
        printf '%s 不应保留 .comment 段。\n' "$native_binary" >&2
        exit 1
    fi
done

if command -v lintian >/dev/null 2>&1; then
    lintian_output=$(lintian \
        --fail-on error,warning,info \
        --display-info \
        --display-experimental \
        "$package" 2>&1) || {
        printf '%s\n' "$lintian_output" >&2
        exit 1
    }
    if [[ -n $lintian_output ]]; then
        printf '%s\n' "$lintian_output" >&2
        printf 'Lintian 仍报告未解决的包问题。\n' >&2
        exit 1
    fi
fi

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
