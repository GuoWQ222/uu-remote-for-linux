#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
readonly output_dir="$project_root/dist"
readonly profile="$project_root/third_party/mutter/ubuntu-24.04-amd64/uuremote3/profile.conf"

usage() {
    printf '用法: %s --archive-keyring PUBLIC_KEY.gpg\n' "$0" >&2
    exit 64
}

keyring=
while (($#)); do
    case $1 in
        --archive-keyring)
            (($# >= 2)) || usage
            keyring=$2
            shift 2
            ;;
        *) usage ;;
    esac
done
[[ -n $keyring && -f $keyring && ! -L $keyring ]] || usage
command -v dpkg-deb >/dev/null 2>&1 || {
    printf '缺少 dpkg-deb。\n' >&2
    exit 69
}
command -v gpg >/dev/null 2>&1 || {
    printf '缺少 gpg。\n' >&2
    exit 69
}
gpg --batch --show-keys "$keyring" >/dev/null 2>&1 || {
    printf 'APT archive keyring 不是有效的 OpenPGP 公钥。\n' >&2
    exit 65
}

profile_value() {
    local key=$1
    local value
    value=$(sed -n -E "s/^${key}=([^[:space:]]+)$/\\1/p" "$profile")
    [[ -n $value && $(printf '%s\n' "$value" | wc -l) -eq 1 ]] || {
        printf 'Mutter profile 缺少唯一字段: %s\n' "$key" >&2
        exit 65
    }
    printf '%s\n' "$value"
}

mutter_version=$(profile_value target_version)
readonly mutter_version
release_date=$(sed -n -E \
    "s/^## \\[$version\\] - ([0-9]{4}-[0-9]{2}-[0-9]{2})$/\\1/p" \
    "$project_root/CHANGELOG.md" | head -n1)
[[ $release_date =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || {
    printf 'CHANGELOG.md 缺少版本 %s 的发布日期。\n' "$version" >&2
    exit 65
}
source_date_epoch=${SOURCE_DATE_EPOCH:-$(date -u -d "$release_date 00:00:00Z" +%s)}
[[ $source_date_epoch =~ ^[0-9]+$ ]] || {
    printf 'SOURCE_DATE_EPOCH 必须是非负整数。\n' >&2
    exit 65
}
export SOURCE_DATE_EPOCH=$source_date_epoch

stage_root=$(mktemp -d)
trap 'rm -rf -- "$stage_root"' EXIT

build_control_only_package() {
    local package=$1
    local template=$2
    local description_file=$3
    local stage="$stage_root/$package"
    local output="$output_dir/${package}_${version}_amd64.deb"
    mkdir -p "$stage/DEBIAN"
    install -Dm0644 "$description_file" \
        "$stage/usr/share/doc/$package/README"
    local installed_size
    installed_size=$(du -sk "$stage/usr" | awk '{print $1}')
    sed \
        -e "s/@VERSION@/$version/g" \
        -e "s/@MUTTER_VERSION@/$mutter_version/g" \
        -e "s/@INSTALLED_SIZE@/$installed_size/g" \
        "$template" >"$stage/DEBIAN/control"
    find "$stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    dpkg-deb --root-owner-group --build "$stage" "$output" >/dev/null
    printf '%s\n' "$output"
}

mkdir -p "$output_dir"
build_control_only_package \
    uu-remote-for-linux-x11 \
    "$project_root/packaging/apt/control-x11.in" \
    "$project_root/packaging/apt/README.md"
build_control_only_package \
    uu-remote-for-linux-wayland \
    "$project_root/packaging/apt/control-wayland.in" \
    "$project_root/packaging/apt/README.md"

keyring_stage="$stage_root/uu-remote-for-linux-archive-keyring"
mkdir -p "$keyring_stage/DEBIAN"
install -Dm0644 "$keyring" \
    "$keyring_stage/usr/share/keyrings/uu-remote-for-linux-archive-keyring.gpg"
install -Dm0644 "$project_root/packaging/apt/uu-remote-for-linux.sources.in" \
    "$keyring_stage/etc/apt/sources.list.d/uu-remote-for-linux.sources"
install -Dm0644 "$project_root/packaging/apt/README.md" \
    "$keyring_stage/usr/share/doc/uu-remote-for-linux-archive-keyring/README"
keyring_installed_size=$(du -sk "$keyring_stage/usr" "$keyring_stage/etc" | \
    awk '{sum += $1} END {print sum}')
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@INSTALLED_SIZE@/$keyring_installed_size/g" \
    "$project_root/packaging/apt/control-keyring.in" \
    >"$keyring_stage/DEBIAN/control"
find "$keyring_stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
keyring_output="$output_dir/uu-remote-for-linux-archive-keyring_${version}_all.deb"
dpkg-deb --root-owner-group --build "$keyring_stage" "$keyring_output" >/dev/null
printf '%s\n' "$keyring_output"

sha256sum \
    "$output_dir/uu-remote-for-linux-x11_${version}_amd64.deb" \
    "$output_dir/uu-remote-for-linux-wayland_${version}_amd64.deb" \
    "$keyring_output"
