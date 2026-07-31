#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly user_prefix="${UUYC_USER_INSTALL_PREFIX:-$HOME/.local}"
readonly launcher_target="$user_prefix/bin/uuyc-linux-controller"
readonly runtime_target="$user_prefix/lib/uuyc-linux-controller"
readonly desktop_target="$user_prefix/share/applications/uuyc-linux-controller.desktop"
readonly metainfo_target="$user_prefix/share/metainfo/io.github.guowq222.uuyc_linux_controller.metainfo.xml"
readonly icon_target="$user_prefix/share/icons/hicolor/256x256/apps/uuyc-linux-controller.png"
readonly legacy_icon_target="$user_prefix/share/icons/hicolor/scalable/apps/uuyc-linux-controller.svg"

[[ -r "$project_root/lib/uuyc-linux-controller/wevtapi.dll" ]] || {
    printf '缺少预构建的 wevtapi.dll；请先运行 make shim。\n' >&2
    exit 1
}
[[ -r "$project_root/lib/uuyc-linux-controller/hwdecode/dxvk/x64/d3d11.dll" ]] || {
    printf '缺少预构建的硬件解码桥；请检查源码包完整性。\n' >&2
    exit 1
}
[[ -x "$project_root/lib/uuyc-linux-controller/uuyc-decoder-selector" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-nvdec-probe" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-tray-proxy" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-autostart-bridge" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-sleep-bridge" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-wol-bridge" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-wol-configure" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-update-bridge" &&
    -r "$project_root/lib/uuyc-linux-controller/uuyc-update-blocker.exe" &&
    -r "$project_root/lib/uuyc-linux-controller/update-compatibility.tsv" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-keyboard-bridge" &&
    -x "$project_root/lib/uuyc-linux-controller/uuyc-input-bridge" &&
    -r "$project_root/lib/uuyc-linux-controller/uuyc-input-hook.dll" &&
    -r "$project_root/lib/uuyc-linux-controller/uuyc-input-injector.exe" ]] || {
    printf '缺少原生托盘、解码设备选择器、NVDEC 探测器或系统设置/更新兼容桥。\n' >&2
    exit 1
}

install -Dm0755 "$project_root/bin/uuyc-linux-controller" "$launcher_target"
install -Dm0644 "$project_root/lib/uuyc-linux-controller/wevtapi.dll" \
    "$runtime_target/wevtapi.dll"
install -Dm0644 "$project_root/lib/uuyc-linux-controller/package-version" \
    "$runtime_target/package-version"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-decoder-selector" \
    "$runtime_target/uuyc-decoder-selector"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-nvdec-probe" \
    "$runtime_target/uuyc-nvdec-probe"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-tray-proxy" \
    "$runtime_target/uuyc-tray-proxy"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-autostart-bridge" \
    "$runtime_target/uuyc-autostart-bridge"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-sleep-bridge" \
    "$runtime_target/uuyc-sleep-bridge"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-wol-bridge" \
    "$runtime_target/uuyc-wol-bridge"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-wol-configure" \
    "$runtime_target/uuyc-wol-configure"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-update-bridge" \
    "$runtime_target/uuyc-update-bridge"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-keyboard-bridge" \
    "$runtime_target/uuyc-keyboard-bridge"
install -Dm0755 "$project_root/lib/uuyc-linux-controller/uuyc-input-bridge" \
    "$runtime_target/uuyc-input-bridge"
install -Dm0644 "$project_root/lib/uuyc-linux-controller/uuyc-input-hook.dll" \
    "$runtime_target/uuyc-input-hook.dll"
install -Dm0644 "$project_root/lib/uuyc-linux-controller/uuyc-input-injector.exe" \
    "$runtime_target/uuyc-input-injector.exe"
install -Dm0644 "$project_root/lib/uuyc-linux-controller/uuyc-update-blocker.exe" \
    "$runtime_target/uuyc-update-blocker.exe"
install -Dm0644 "$project_root/lib/uuyc-linux-controller/update-compatibility.tsv" \
    "$runtime_target/update-compatibility.tsv"
mkdir -p "$runtime_target/hwdecode"
cp -a "$project_root/lib/uuyc-linux-controller/hwdecode/." \
    "$runtime_target/hwdecode/"
install -Dm0644 "$project_root/share/applications/uuyc-linux-controller.desktop" \
    "$desktop_target"
install -Dm0644 \
    "$project_root/share/metainfo/io.github.guowq222.uuyc_linux_controller.metainfo.xml" \
    "$metainfo_target"
install -Dm0644 \
    "$project_root/share/icons/hicolor/256x256/apps/uuyc-linux-controller.png" \
    "$icon_target"
rm -f -- "$legacy_icon_target"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$user_prefix/share/applications" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t "$user_prefix/share/icons/hicolor" \
        >/dev/null 2>&1 || true
fi

printf '安装完成：%s\n' "$launcher_target"
printf '首次设置：%s --accept-eula --setup-only\n' "$launcher_target"
