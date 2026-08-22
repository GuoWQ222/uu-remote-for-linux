#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
readonly arch=amd64
readonly output_dir="$project_root/dist"
readonly base_package="$output_dir/uu-remote-for-linux_${version}_${arch}.deb"
readonly profile_dir="$project_root/third_party/mutter/ubuntu-24.04-amd64/uuremote3"
readonly profile="$profile_dir/profile.conf"

for command_name in awk dpkg-deb sha256sum unzip zip; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf '缺少发行构建工具: %s\n' "$command_name" >&2
        exit 69
    }
done
[[ -f $base_package && ! -L $base_package ]] || {
    printf '缺少基础运行时包；请先运行 make deb。\n' >&2
    exit 66
}
"$project_root/packaging/mutter/verify-bundle.sh"

profile_value() {
    local key=$1 value
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
mkdir -p "$output_dir"

x11_output="$output_dir/uu-remote-for-linux-x11_${version}_${arch}.deb"
x11_stage="$stage_root/x11"
dpkg-deb -R "$base_package" "$x11_stage"
awk -v version="$version" '
    /^Package: / {
        print "Package: uu-remote-for-linux-x11"
        next
    }
    /^Description: / {
        print "Provides: uu-remote-for-linux (= " version "), uu-remote-for-linux-session"
        print "Conflicts: uu-remote-for-linux, uu-remote-for-linux-session"
        print "Replaces: uu-remote-for-linux (<= " version ")"
        print "Description: unofficial UU Remote compatibility wrapper for Linux X11"
        next
    }
    { print }
' "$x11_stage/DEBIAN/control" >"$x11_stage/DEBIAN/control.new"
mv "$x11_stage/DEBIAN/control.new" "$x11_stage/DEBIAN/control"
find "$x11_stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
dpkg-deb --root-owner-group --build "$x11_stage" "$x11_output" >/dev/null

wayland_meta_stage="$stage_root/wayland-meta"
mkdir -p "$wayland_meta_stage/DEBIAN"
install -Dm0644 "$project_root/packaging/release/README.md" \
    "$wayland_meta_stage/usr/share/doc/uu-remote-for-linux-wayland/README.md"
installed_size=$(du -sk "$wayland_meta_stage/usr" | awk '{print $1}')
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@MUTTER_VERSION@/$mutter_version/g" \
    -e "s/@INSTALLED_SIZE@/$installed_size/g" \
    "$project_root/packaging/release/control-wayland.in" \
    >"$wayland_meta_stage/DEBIAN/control"
find "$wayland_meta_stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
wayland_meta="$stage_root/uu-remote-for-linux-wayland_${version}_${arch}.deb"
dpkg-deb --root-owner-group --build "$wayland_meta_stage" "$wayland_meta" \
    >/dev/null

mapfile -t mutter_packages < <(find "$profile_dir/packages" -maxdepth 1 \
    -type f -name "*_${mutter_version}_*.deb" -print | sort)
[[ ${#mutter_packages[@]} -eq 4 ]] || {
    printf 'Wayland 离线包必须恰好包含四个 Mutter 修复包。\n' >&2
    exit 65
}

bundle_name="uu-remote-for-linux-wayland_${version}_${arch}"
bundle_root="$stage_root/bundle/$bundle_name"
mkdir -p "$bundle_root"
install -m0644 "$base_package" "$bundle_root/"
install -m0644 "$wayland_meta" "$bundle_root/"
for package in "${mutter_packages[@]}"; do
    install -m0644 "$package" "$bundle_root/"
done
sed -e "s/@VERSION@/$version/g" \
    "$project_root/packaging/release/README-WAYLAND.txt.in" \
    >"$bundle_root/README.txt"
(
    cd "$bundle_root"
    sha256sum -- *.deb >SHA256SUMS
)
find "$stage_root/bundle" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
wayland_output="$output_dir/${bundle_name}.zip"
rm -f -- "$wayland_output"
(
    cd "$stage_root/bundle"
    mapfile -d '' files < <(find "$bundle_name" -type f -print0 | sort -z)
    zip -X -q "$wayland_output" "${files[@]}"
)

zip_inventory="$stage_root/zip-inventory"
mkdir -p "$zip_inventory"
unzip -q "$wayland_output" -d "$zip_inventory"
(
    cd "$zip_inventory/$bundle_name"
    [[ $(find . -maxdepth 1 -type f -name '*.deb' | wc -l) -eq 6 ]]
    sha256sum --strict -c SHA256SUMS >/dev/null
)

(
    cd "$output_dir"
    sha256sum \
        "$(basename "$x11_output")" \
        "$(basename "$wayland_output")" >SHA256SUMS
)
printf '%s\n' "$x11_output"
printf '%s\n' "$wayland_output"
sha256sum "$x11_output" "$wayland_output"
