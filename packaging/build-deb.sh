#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
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
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/update-compatibility.tsv" ]] || {
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
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-wol-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-wol-configure"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-update-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-keyboard-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-input-bridge"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$stage_dir/usr/lib/uu-remote-for-linux/uu-remote-update-blocker.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/update-compatibility.tsv" \
    "$stage_dir/usr/lib/uu-remote-for-linux/update-compatibility.tsv"
mkdir -p "$stage_dir/usr/lib/uu-remote-for-linux/hwdecode"
cp -a "$project_root/lib/uu-remote-for-linux/hwdecode/." \
    "$stage_dir/usr/lib/uu-remote-for-linux/hwdecode/"
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
install -Dm0644 "$project_root/LICENSE" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/copyright"
install -Dm0644 "$project_root/third_party/HWDECODE.md" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/HWDECODE.md"
install -Dm0644 "$project_root/third_party/sources/nvcuda-uu-remote-v0.6.tar.xz" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/source/nvcuda-uu-remote-v0.6.tar.xz"
install -Dm0644 "$project_root/third_party/sources/nvenc-nvcuvid-v0.5.tar.xz" \
    "$stage_dir/usr/share/doc/uu-remote-for-linux/source/nvenc-nvcuvid-v0.5.tar.xz"

mkdir -p "$stage_dir/DEBIAN"
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@ARCH@/$arch/g" \
    "$project_root/packaging/debian/control.in" >"$stage_dir/DEBIAN/control"
install -Dm0755 "$project_root/packaging/debian/postinst" \
    "$stage_dir/DEBIAN/postinst"
install -Dm0755 "$project_root/packaging/debian/postrm" \
    "$stage_dir/DEBIAN/postrm"

mkdir -p "$output_dir"
dpkg-deb --root-owner-group --build "$stage_dir" "$output_file"
printf '%s\n' "$output_file"
sha256sum "$output_file"
