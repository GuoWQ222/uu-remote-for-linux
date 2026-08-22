#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly user_prefix="${UU_REMOTE_USER_INSTALL_PREFIX:-$HOME/.local}"
readonly launcher_target="$user_prefix/bin/uu-remote-for-linux"
readonly runtime_target="$user_prefix/lib/uu-remote-for-linux"
readonly desktop_target="$user_prefix/share/applications/uu-remote-for-linux.desktop"
readonly metainfo_target="$user_prefix/share/metainfo/io.github.guowq222.uu_remote_for_linux.metainfo.xml"
readonly icon_target="$user_prefix/share/icons/hicolor/256x256/apps/uu-remote-for-linux.png"
readonly legacy_icon_target="$user_prefix/share/icons/hicolor/scalable/apps/uu-remote-for-linux.svg"

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
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/update-compatibility.tsv" &&
    -r "$project_root/lib/uu-remote-for-linux/wol-compatibility.tsv" &&
    -r "$project_root/lib/uu-remote-for-linux/hwdecode-compatibility.tsv" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so" &&
    -x "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" &&
    -r "$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe" ]] || {
    printf '缺少原生托盘、解码设备选择器、NVDEC 探测器或系统设置/更新兼容桥。\n' >&2
    exit 1
}

install -Dm0755 "$project_root/bin/uu-remote-for-linux" "$launcher_target"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/wevtapi.dll" \
    "$runtime_target/wevtapi.dll"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/package-version" \
    "$runtime_target/package-version"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector" \
    "$runtime_target/uu-remote-decoder-selector"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-nvdec-probe" \
    "$runtime_target/uu-remote-nvdec-probe"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-tray-proxy" \
    "$runtime_target/uu-remote-tray-proxy"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-autostart-bridge" \
    "$runtime_target/uu-remote-autostart-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-sleep-bridge" \
    "$runtime_target/uu-remote-sleep-bridge"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge" \
    "$runtime_target/uu-remote-system-proxy-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge" \
    "$runtime_target/uu-remote-wol-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure" \
    "$runtime_target/uu-remote-wol-configure"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-wol-profiler" \
    "$runtime_target/uu-remote-wol-profiler"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge" \
    "$runtime_target/uu-remote-update-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler" \
    "$runtime_target/uu-remote-hwdecode-profiler"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy" \
    "$runtime_target/uu-remote-encoder-policy"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge" \
    "$runtime_target/uu-remote-keyboard-bridge"
install -Dm0755 "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge" \
    "$runtime_target/uu-remote-input-bridge"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so" \
    "$runtime_target/uu-remote-frame-helper.so"
install -Dm0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor" \
    "$runtime_target/uu-remote-pipewire-cursor"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" \
    "$runtime_target/uu-remote-input-hook.dll"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" \
    "$runtime_target/uu-remote-input-injector.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe" \
    "$runtime_target/uu-remote-powershell-bridge.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$runtime_target/uu-remote-update-blocker.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe" \
    "$runtime_target/uu-remote-dxgi-probe.exe"
install -Dm0644 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe" \
    "$runtime_target/uu-remote-nvenc-d3d11-probe.exe"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/update-compatibility.tsv" \
    "$runtime_target/update-compatibility.tsv"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/wol-compatibility.tsv" \
    "$runtime_target/wol-compatibility.tsv"
install -Dm0644 "$project_root/lib/uu-remote-for-linux/hwdecode-compatibility.tsv" \
    "$runtime_target/hwdecode-compatibility.tsv"
mkdir -p "$runtime_target/hwdecode"
cp -a "$project_root/lib/uu-remote-for-linux/hwdecode/." \
    "$runtime_target/hwdecode/"
install -Dm0644 "$project_root/share/applications/uu-remote-for-linux.desktop" \
    "$desktop_target"
install -Dm0644 \
    "$project_root/share/metainfo/io.github.guowq222.uu_remote_for_linux.metainfo.xml" \
    "$metainfo_target"
install -Dm0644 \
    "$project_root/share/icons/hicolor/256x256/apps/uu-remote-for-linux.png" \
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
