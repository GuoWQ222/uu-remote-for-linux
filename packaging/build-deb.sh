#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
release_date=$(sed -n -E \
    "s/^## \\[$version\\] - ([0-9]{4}-[0-9]{2}-[0-9]{2})$/\\1/p" \
    "$project_root/CHANGELOG.md" | head -n1)
[[ $release_date =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || {
    printf 'CHANGELOG.md 缺少版本 %s 的有效发布日期。\n' "$version" >&2
    exit 1
}
build_source_date_epoch=${SOURCE_DATE_EPOCH:-}
if [[ -z $build_source_date_epoch ]]; then
    build_source_date_epoch=$(date -u -d "$release_date 00:00:00Z" +%s)
fi
[[ $build_source_date_epoch =~ ^[0-9]+$ ]] || {
    printf 'SOURCE_DATE_EPOCH 必须是非负整数。\n' >&2
    exit 1
}
export SOURCE_DATE_EPOCH=$build_source_date_epoch
debian_date=$(LC_ALL=C date -u -d "@$build_source_date_epoch" \
    '+%a, %d %b %Y %H:%M:%S +0000')
readonly release_date build_source_date_epoch debian_date
readonly arch="amd64"
readonly artifact_name="uu-remote-for-linux"
readonly output_dir="$project_root/dist"
readonly output_file="$output_dir/${artifact_name}_${version}_${arch}.deb"

[[ -r "$project_root/lib/uu-remote-for-linux/wevtapi.dll" ]] || {
    printf '缺少预构建的 wevtapi.dll；请先运行 make shim。\n' >&2
    exit 1
}
[[ -r "$project_root/lib/uu-remote-for-linux/hwdecode/dxvk/x64/d3d11.dll" ]] || {
    printf '缺少预构建的硬件解码桥；请检查源码包完整性。\n' >&2
    exit 1
}
[[ -x "$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-nvdec-probe" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-tray-proxy" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-autostart-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-sleep-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-wol-profiler" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-runtime-profiler" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-capture" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe" ]] || {
    printf '缺少原生托盘、解码设备选择器、NVDEC 探测器或系统设置/更新兼容桥。\n' >&2
    exit 1
}
stage_dir=$(mktemp -d)
trap 'rm -rf -- "$stage_dir"' EXIT
chmod 0755 "$stage_dir"

install -Dm0755 "$project_root/bin/uu-remote-for-linux" \
    "$stage_dir/usr/bin/uu-remote-for-linux"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/wevtapi.dll" \
    "$stage_dir/usr/lib/uu-remote-for-linux/wevtapi.dll"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/package-version" \
    "$stage_dir/usr/lib/uu-remote-for-linux/package-version"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-decoder-selector"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-nvdec-probe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-nvdec-probe"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-tray-proxy" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-tray-proxy"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-autostart-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-autostart-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-sleep-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-sleep-bridge"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-wol-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-wol-configure"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-profiler" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-wol-profiler"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-update-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-runtime-profiler" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-runtime-profiler"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-encoder-policy"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-keyboard-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-input-bridge"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-frame-helper.so"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-capture" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-pipewire-capture"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-pipewire-cursor"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-update-blocker.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe"
install -Dm0644 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe"
mkdir -p "$stage_dir/usr/lib/uu-remote-for-linux/hwdecode"
cp -a "$project_root/lib/uu-remote-for-linux/hwdecode/." \
    "$stage_dir/usr/lib/uu-remote-for-linux/hwdecode/"
install -d "$stage_dir/usr/share/doc/uu-remote-for-linux/licenses"
for license_file in "$project_root"/lib/uu-remote-for-linux/hwdecode/licenses/*; do
    install -m0644 "$license_file" \
        "$stage_dir/usr/share/doc/uu-remote-for-linux/licenses/${license_file##*/}"
done
rm -rf -- "$stage_dir/usr/lib/uu-remote-for-linux/hwdecode/licenses"
chmod 0644 "$stage_dir"/usr/lib/uu-remote-for-linux/hwdecode/wine/x86_64-unix/*.so
install -Dm0644 "$project_root/share/applications/uu-remote-for-linux.desktop" \
    "$stage_dir/usr/share/applications/uu-remote-for-linux.desktop"
install -Dm0644 \
    "$project_root/share/icons/hicolor/256x256/apps/uu-remote-for-linux.png" \
    "$stage_dir/usr/share/icons/hicolor/256x256/apps/uu-remote-for-linux.png"
install -Dm0644 \
    "$project_root/share/metainfo/io.github.guowq222.uu_remote_for_linux.metainfo.xml" \
    "$stage_dir/usr/share/metainfo/io.github.guowq222.uu_remote_for_linux.metainfo.xml"
install -Dm0644 "$project_root/README.md" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/README.md"
install -Dm0644 "$project_root/README.zh-CN.md" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/README.zh-CN.md"
install -Dm0644 "$project_root/NOTICE.md" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/NOTICE.md"
install -Dm0644 "$project_root/COMPATIBILITY.md" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/COMPATIBILITY.md"
install -Dm0644 "$project_root/packaging/debian/copyright" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/copyright"
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@DEBIAN_DATE@/$debian_date/g" \
    "$project_root/packaging/debian/changelog.in" > \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/changelog"
chmod 0644 "$stage_dir/usr/share/doc/uu-remote-for-linux/changelog"
gzip -9n "$stage_dir/usr/share/doc/uu-remote-for-linux/changelog"
install -d "$stage_dir/usr/share/man/man1"
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@RELEASE_DATE@/$release_date/g" \
    "$project_root/share/man/man1/uu-remote-for-linux.1" > \
    "$stage_dir/usr/share/man/man1/uu-remote-for-linux.1"
chmod 0644 "$stage_dir/usr/share/man/man1/uu-remote-for-linux.1"
gzip -9n "$stage_dir/usr/share/man/man1/uu-remote-for-linux.1"
install -Dm0644 "$project_root/third_party/HWDECODE.md" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/HWDECODE.md"
install -Dm0644 "$project_root/third_party/sources/nvcuda-uu-remote-v0.8.tar.xz" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/source/nvcuda-uu-remote-v0.8.tar.xz"
install -Dm0644 "$project_root/third_party/sources/nvenc-nvcuvid-v0.5.tar.xz" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/source/nvenc-nvcuvid-v0.5.tar.xz"

mkdir -p "$stage_dir/DEBIAN"
installed_size=$(du -sk "$stage_dir/usr" | awk '{print $1}')
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@ARCH@/$arch/g" \
    -e "s/@INSTALLED_SIZE@/$installed_size/g" \
    "$project_root/packaging/debian/control.in" >"$stage_dir/DEBIAN/control"
install -Dm0755 "$project_root/packaging/debian/postinst" \
    "$stage_dir/DEBIAN/postinst"
install -Dm0755 "$project_root/packaging/debian/postrm" \
    "$stage_dir/DEBIAN/postrm"
(
    cd "$stage_dir"
    find usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum
) >"$stage_dir/DEBIAN/md5sums"
chmod 0644 "$stage_dir/DEBIAN/md5sums"

# dpkg-deb only clamps mtimes newer than SOURCE_DATE_EPOCH. When a release is
# prepared before its UTC midnight (for example, an Asia-local release date),
# freshly staged files would otherwise retain per-build timestamps. Normalize
# the complete archive tree explicitly so repeated builds remain byte-identical.
find "$stage_dir" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +

mkdir -p "$output_dir"
dpkg-deb --root-owner-group --build "$stage_dir" "$output_file"
printf '%s\n' "$output_file"
sha256sum "$output_file"
