#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly launcher="$project_root/bin/uuyc-linux-controller"
readonly fixture_bin="$project_root/tests/fixtures/bin"

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

export PATH="$fixture_bin:$PATH"
export XDG_DATA_HOME="$test_root/data"
export XDG_STATE_HOME="$test_root/state"
export XDG_CACHE_HOME="$test_root/cache"
export UUYC_CONTROLLER_RUNTIME_DIR="$project_root/lib/uuyc-linux-controller"
export UUYC_FAKE_TRACE="$test_root/fake-tools.log"
export UUYC_HWDECODE_ASSUME_HOST_READY=1
export UUYC_DISABLE_AUTOSTART_WATCHER=1
export UUYC_DISABLE_SLEEP_WATCHER=1
export UUYC_DISABLE_UPDATE_WATCHER=1
export UUYC_DISABLE_KEYBOARD_BRIDGE=1
export UUYC_TRAY_PROXY_BIN="$fixture_bin/uuyc-tray-proxy"
export UUYC_UPDATE_NO_RESTART=1
export DISPLAY=:99
export XDG_SESSION_TYPE=x11

nvdec_rows="$test_root/nvdec-rows.tsv"
printf '0\tMock NVIDIA GPU\t0000:01:00.0\tH264-8bit-420,H265-8bit-420,H265-10bit-420\t8192x8192\n' \
    >"$nvdec_rows"
export UUYC_NVDEC_PROBE_ROWS_FILE="$nvdec_rows"

"$launcher" --list-decoders >"$test_root/decoders.txt"
grep -q 'cpu' "$test_root/decoders.txt"
grep -q 'nvidia:0' "$test_root/decoders.txt"
test "$("$project_root/lib/uuyc-linux-controller/uuyc-decoder-selector" --best-id)" = \
    "nvidia:0"
if "$launcher" --decoder unknown >"$test_root/unknown.out" 2>"$test_root/unknown.err"; then
    printf '未知解码设备被意外接受。\n' >&2
    exit 1
fi
grep -q '不可选择或未知设备' "$test_root/unknown.err"

"$launcher" --accept-eula --setup-only

prefix="$XDG_DATA_HOME/uuyc-linux-controller/wineprefix"
install_dir="$prefix/drive_c/Program Files/Netease/GameViewer"
cache_dir="$XDG_CACHE_HOME/uuyc-linux-controller"
state_dir="$XDG_STATE_HOME/uuyc-linux-controller"

test -f "$XDG_DATA_HOME/uuyc-linux-controller/eula-accepted"
test -f "$prefix/system.reg"
test -f "$install_dir/GameViewer.exe"
test -f "$install_dir/bin/GameViewerServer.exe"
test -f "$install_dir/bin/GameViewerHealthd.exe"
cmp -s \
    "$project_root/lib/uuyc-linux-controller/uuyc-update-blocker.exe" \
    "$install_dir/bin/Upgrade.exe"
test -f "$install_dir/bin/Upgrade.uuyc-original.exe"
cmp -s \
    "$project_root/lib/uuyc-linux-controller/wevtapi.dll" \
    "$install_dir/bin/wevtapi.dll"
test -f "$install_dir/bin/.webview-ready"
test -f "$cache_dir/UURemote_Setup.exe"
grep -q '^version=4.34.0.8979$' "$cache_dir/installer-metadata"
grep -q '^INSTALL$' "$UUYC_FAKE_TRACE"
grep -q '^WEBVIEW$' "$UUYC_FAKE_TRACE"
test -f "$state_dir/setup.log"

install_count=$(grep -c '^INSTALL$' "$UUYC_FAKE_TRACE")
webview_count=$(grep -c '^WEBVIEW$' "$UUYC_FAKE_TRACE")

"$launcher" --setup-only
"$launcher" --repair --setup-only

test "$(grep -c '^INSTALL$' "$UUYC_FAKE_TRACE")" -eq "$install_count"
test "$(grep -c '^WEBVIEW$' "$UUYC_FAKE_TRACE")" -eq "$webview_count"

streamer="$install_dir/bin/streamer.dll"
streamer_original="$install_dir/bin/streamer.uuyc-original.dll"
decoder_cache="$install_dir/config/streamer/decoder_codec_capability_cache.json"
decoder_cache_backup="$XDG_DATA_HOME/uuyc-linux-controller/decoder-cache-before-hwdecode.json"
export UUYC_HWDECODE_STREAMER_ORIGINAL_SHA
UUYC_HWDECODE_STREAMER_ORIGINAL_SHA=$(sha256sum "$streamer")
UUYC_HWDECODE_STREAMER_ORIGINAL_SHA=${UUYC_HWDECODE_STREAMER_ORIGINAL_SHA%% *}
patched_fixture="$test_root/patched-streamer.dll"
cp "$streamer" "$patched_fixture"
printf '\262\041\220' |
    dd of="$patched_fixture" bs=1 seek=$((0x94dc03)) conv=notrunc status=none
printf '\152\041\130' |
    dd of="$patched_fixture" bs=1 seek=$((0x94e4ee)) conv=notrunc status=none
export UUYC_HWDECODE_STREAMER_PATCHED_SHA
UUYC_HWDECODE_STREAMER_PATCHED_SHA=$(sha256sum "$patched_fixture")
UUYC_HWDECODE_STREAMER_PATCHED_SHA=${UUYC_HWDECODE_STREAMER_PATCHED_SHA%% *}

"$launcher" --enable-hwdecode
grep -q '^selection=nvidia:0$' \
    "$XDG_DATA_HOME/uuyc-linux-controller/decoder-selection"
cmp -s \
    "$project_root/lib/uuyc-linux-controller/hwdecode/dxvk/x64/d3d11.dll" \
    "$install_dir/bin/d3d11.dll"
cmp -s \
    "$project_root/lib/uuyc-linux-controller/hwdecode/dxvk/x64/dxgi.dll" \
    "$install_dir/bin/dxgi.dll"
cmp -s \
    "$project_root/lib/uuyc-linux-controller/hwdecode/wine/x86_64-windows/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll"
cmp -s \
    "$project_root/lib/uuyc-linux-controller/hwdecode/wine/x86_64-windows/nvcuvid.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$UUYC_HWDECODE_STREAMER_PATCHED_SHA"
test "$(sha256sum "$streamer_original" | cut -d' ' -f1)" = \
    "$UUYC_HWDECODE_STREAMER_ORIGINAL_SHA"
test ! -e "$decoder_cache"
grep -q '"mock":"original-cache"' "$decoder_cache_backup"
"$launcher" --diagnose | grep -q '硬解桥开关.*已启用'
"$launcher" --diagnose | grep -q 'UU 自动更新开关.*开启'
"$launcher" --diagnose | grep -q '官方更新器保护.*完整'

"$launcher" --check-update
"$launcher" --diagnose | grep -q '安全更新状态.*已是最新版本'

protected_before="$test_root/protected-before.sha256"
protected_after="$test_root/protected-after.sha256"
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uuyc-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" >"$protected_before"
UUYC_FAKE_LATEST_VERSION=4.35.0.9000 "$launcher" --check-update
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uuyc-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" >"$protected_after"
cmp -s "$protected_before" "$protected_after"
"$launcher" --diagnose | grep -q '安全更新状态.*已暂缓 4.35.0.9000'

installer_sha=$(sed -n 's/^sha256=//p' "$cache_dir/installer-metadata")
server_sha=$(sha256sum "$install_dir/bin/GameViewerServer.exe")
server_sha=${server_sha%% *}
test_profile="$test_root/update-compatibility.tsv"
printf '4.34.0.8979\t%s\t%s\t%s\n' \
    "$installer_sha" "$UUYC_HWDECODE_STREAMER_ORIGINAL_SHA" "$server_sha" \
    >"$test_profile"

printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
UUYC_UPDATE_COMPATIBILITY_PROFILES="$test_profile" "$launcher" --check-update
"$launcher" --diagnose | grep -q 'UU 版本.*4.34.0.8979'
"$launcher" --diagnose | grep -q '安全更新状态.*已更新到 4.34.0.8979'
cmp -s \
    "$project_root/lib/uuyc-linux-controller/uuyc-update-blocker.exe" \
    "$install_dir/bin/Upgrade.exe"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$UUYC_HWDECODE_STREAMER_PATCHED_SHA"

printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uuyc-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" >"$protected_before"
if UUYC_FAKE_CORRUPT_STREAMER=1 \
    UUYC_UPDATE_COMPATIBILITY_PROFILES="$test_profile" \
    "$launcher" --check-update; then
    printf '损坏的更新被意外接受。\n' >&2
    exit 1
fi
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uuyc-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" >"$protected_after"
cmp -s "$protected_before" "$protected_after"
"$launcher" --diagnose | grep -q 'UU 版本.*4.33.0.8000'
"$launcher" --diagnose | grep -q '安全更新状态.*更新失败并已回滚'
printf '4.34.0.8979\n' >"$install_dir/bin/.installed-version"

"$launcher" --diagnose | grep -q '4.34.0.8979'
"$launcher"
grep -q '^HEALTHD$' "$UUYC_FAKE_TRACE"
grep -q '^SERVER$' "$UUYC_FAKE_TRACE"
grep -q '^CLIENT$' "$UUYC_FAKE_TRACE"
grep -q '^TRAY_PROXY ' "$UUYC_FAKE_TRACE"
grep -q '^HWDECODE_ENV$' "$UUYC_FAKE_TRACE"
grep -q '^CUDA_DEVICE=0$' "$UUYC_FAKE_TRACE"
grep -q 'SYSTEMD_RUN .*--unit=uuyc-linux-controller-healthd' "$UUYC_FAKE_TRACE"
grep -q 'SYSTEMD_RUN .*--unit=uuyc-linux-controller-server' "$UUYC_FAKE_TRACE"
if grep -q '^FD9_LEAK ' "$UUYC_FAKE_TRACE"; then
    printf '操作锁文件描述符被泄漏给子进程：\n' >&2
    grep '^FD9_LEAK ' "$UUYC_FAKE_TRACE" >&2
    exit 1
fi

exec 8>"$state_dir/operation.lock"
flock 8
if UUYC_OPERATION_LOCK_TIMEOUT=0 "$launcher" --setup-only \
    >"$test_root/lock-contention.out" 2>"$test_root/lock-contention.err"; then
    printf '操作锁竞争时启动器意外成功。\n' >&2
    exit 1
fi
grep -q '另一个 UU 远程安装或启动操作仍在运行' \
    "$test_root/lock-contention.err"
flock -u 8
exec 8>&-

"$launcher" --disable-hwdecode
grep -q '^selection=cpu$' \
    "$XDG_DATA_HOME/uuyc-linux-controller/decoder-selection"
test ! -e "$install_dir/bin/d3d11.dll"
test ! -e "$install_dir/bin/dxgi.dll"
test ! -e "$prefix/drive_c/windows/system32/nvcuda.dll"
test ! -e "$prefix/drive_c/windows/system32/nvcuvid.dll"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$UUYC_HWDECODE_STREAMER_ORIGINAL_SHA"
test ! -e "$streamer_original"
grep -q '"mock":"original-cache"' "$decoder_cache"
test ! -e "$decoder_cache_backup"
"$launcher" --diagnose | grep -q '硬解桥开关.*未启用'

"$launcher" --decoder nvidia:0
"$launcher"
test -e "$install_dir/bin/d3d11.dll"
grep -q '^CUDA_DEVICE=0$' "$UUYC_FAKE_TRACE"
"$launcher" --decoder cpu
"$launcher"
test ! -e "$install_dir/bin/d3d11.dll"
"$launcher" --diagnose | grep -q '实际解码选择.*cpu'

client_count_before=$(grep -c '^CLIENT$' "$UUYC_FAKE_TRACE")
stop_count_before=$(grep -c '^WINESERVER -k$' "$UUYC_FAKE_TRACE")
UUYC_FAKE_ZENITY_CANCEL=1 "$launcher" --select-decoder-and-restart
test "$(grep -c '^CLIENT$' "$UUYC_FAKE_TRACE")" -eq "$client_count_before"
test "$(grep -c '^WINESERVER -k$' "$UUYC_FAKE_TRACE")" -eq \
    "$stop_count_before"
grep -q 'ZENITY .*--list' "$UUYC_FAKE_TRACE"

UUYC_FAKE_ZENITY_CHOICE=nvidia:0 \
    "$launcher" --select-decoder-and-restart
for _attempt in {1..30}; do
    if (( $(grep -c '^CLIENT$' "$UUYC_FAKE_TRACE") > client_count_before )); then
        break
    fi
    sleep 0.1
done
grep -q '^selection=nvidia:0$' \
    "$XDG_DATA_HOME/uuyc-linux-controller/decoder-selection"
test -e "$install_dir/bin/d3d11.dll"
test "$(grep -c '^CLIENT$' "$UUYC_FAKE_TRACE")" -gt "$client_count_before"
test "$(grep -c '^WINESERVER -k$' "$UUYC_FAKE_TRACE")" -gt \
    "$stop_count_before"
grep -q '^CUDA_DEVICE=0$' "$UUYC_FAKE_TRACE"
grep -q \
    'SYSTEMD_RUN .*--unit=uuyc-linux-controller-client .*--property=ExitType=cgroup.*--restart-selected-decoder' \
    "$UUYC_FAKE_TRACE"

"$launcher" --stop
grep -q '^WINESERVER -k$' "$UUYC_FAKE_TRACE"

printf '模拟 Wine 安装状态机检查通过。\n'
