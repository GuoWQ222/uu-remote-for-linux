#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly launcher="$project_root/bin/uu-remote-for-linux"
readonly fixture_bin="$project_root/tests/fixtures/bin"

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

export PATH="$fixture_bin:$PATH"
export XDG_DATA_HOME="$test_root/data"
export XDG_STATE_HOME="$test_root/state"
export XDG_CACHE_HOME="$test_root/cache"
export UU_REMOTE_RUNTIME_DIR="$project_root/lib/uu-remote-for-linux"
export UU_REMOTE_FAKE_TRACE="$test_root/fake-tools.log"
export UU_REMOTE_HWDECODE_ASSUME_HOST_READY=1
export UU_REMOTE_HWENCODE_ASSUME_HOST_READY=1
export UU_REMOTE_DISABLE_AUTO_HWENCODE=1
export UU_REMOTE_DISABLE_AUTOSTART_WATCHER=1
export UU_REMOTE_DISABLE_SLEEP_WATCHER=1
export UU_REMOTE_DISABLE_UPDATE_WATCHER=1
export UU_REMOTE_DISABLE_KEYBOARD_BRIDGE=1
export UU_REMOTE_DISABLE_INPUT_BRIDGE=1
export UU_REMOTE_DISABLE_FOCUS_STABILIZER=1
export UU_REMOTE_TRAY_PROXY_BIN="$fixture_bin/uu-remote-tray-proxy"
export UU_REMOTE_UPDATE_NO_RESTART=1
export HTTP_PROXY=http://should-not-reach-wine.invalid:8888
export HTTPS_PROXY=http://should-not-reach-wine.invalid:8888
export DISPLAY=:99
export XDG_SESSION_TYPE=x11

"$launcher" --diagnose | grep -q '桌面后端.*x11'
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test-0 \
    "$launcher" --diagnose | grep -q '桌面后端.*wayland-xwayland'

nvdec_rows="$test_root/nvdec-rows.tsv"
printf '0\tMock NVIDIA GPU\t0000:01:00.0\tH264-8bit-420,H265-8bit-420,H265-10bit-420\t8192x8192\n' \
    >"$nvdec_rows"
export UU_REMOTE_NVDEC_PROBE_ROWS_FILE="$nvdec_rows"

"$launcher" --list-decoders >"$test_root/decoders.txt"
grep -q 'cpu' "$test_root/decoders.txt"
grep -q 'nvidia:0' "$test_root/decoders.txt"
test "$("$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector" --best-id)" = \
    "nvidia:0"
if "$launcher" --decoder unknown >"$test_root/unknown.out" 2>"$test_root/unknown.err"; then
    printf '未知解码设备被意外接受。\n' >&2
    exit 1
fi
grep -q '不可选择或未知设备' "$test_root/unknown.err"

headless_root="$test_root/headless"
if env -u DISPLAY -u WAYLAND_DISPLAY \
    XDG_DATA_HOME="$headless_root/data" \
    XDG_STATE_HOME="$headless_root/state" \
    XDG_CACHE_HOME="$headless_root/cache" \
    "$launcher" --setup-only \
    >"$test_root/headless.out" 2>"$test_root/headless.err"; then
    printf '无图形环境下首次设置被意外自动接受。\n' >&2
    exit 1
fi
grep -q '首次设置前请阅读' "$test_root/headless.err"
test ! -e "$headless_root/data/uu-remote-for-linux/eula-accepted"

UU_REMOTE_FAKE_ZENITY_CANCEL=1 "$launcher"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/eula-accepted"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/wineprefix/system.reg"
if grep -q '^INSTALL$\|^CLIENT$' "$UU_REMOTE_FAKE_TRACE"; then
    printf '拒绝 EULA 后仍执行了安装或启动。\n' >&2
    exit 1
fi

"$launcher"
grep -q '^EULA_FETCH$' "$UU_REMOTE_FAKE_TRACE"
grep -q \
    'CURL .*--doh-url https://dns.alidns.com/dns-query .*--resolve dns.alidns.com:443:223.5.5.5' \
    "$UU_REMOTE_FAKE_TRACE"
grep -q 'ZENITY .*--text-info' "$UU_REMOTE_FAKE_TRACE"
grep -q 'ZENITY .*--checkbox=' "$UU_REMOTE_FAKE_TRACE"
grep -q 'ZENITY .*--ok-label=接受并继续' "$UU_REMOTE_FAKE_TRACE"
grep -q '^CLIENT$' "$UU_REMOTE_FAKE_TRACE"

prefix="$XDG_DATA_HOME/uu-remote-for-linux/wineprefix"
install_dir="$prefix/drive_c/Program Files/Netease/GameViewer"
cache_dir="$XDG_CACHE_HOME/uu-remote-for-linux"
state_dir="$XDG_STATE_HOME/uu-remote-for-linux"
grep -Fq '"mode": "none"' \
    "$state_dir/system-proxy-bridge-status.json"
grep -Fq '"source": "forced"' \
    "$state_dir/system-proxy-bridge-status.json"

# Package upgrades can inherit a live update bridge created before versioned
# status files existed.  The launcher must replace that bridge and continue to
# the client instead of exiting because the missing file makes sed fail under
# `set -e -o pipefail`.
rm -f -- "$state_dir/update-bridge-status"
client_unit_count_before_bridge_migration=$(grep -c \
    'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client ' \
    "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_DISABLE_UPDATE_WATCHER=0 \
UU_REMOTE_FAKE_ACTIVE_UNITS=uu-remote-for-linux-update.service \
UU_REMOTE_FAKE_SYSTEMD_RUN_NOEXEC=1 \
    "$launcher"
test "$(grep -c 'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client ' \
    "$UU_REMOTE_FAKE_TRACE")" -eq \
    "$((client_unit_count_before_bridge_migration + 1))"
grep -q \
    'SYSTEMCTL --user stop uu-remote-for-linux-update.service' \
    "$UU_REMOTE_FAKE_TRACE"
grep -q \
    'SYSTEMD_RUN .*--unit=uu-remote-for-linux-update .*update-bridge-status' \
    "$UU_REMOTE_FAKE_TRACE"

test -f "$XDG_DATA_HOME/uu-remote-for-linux/eula-accepted"
test -f "$prefix/system.reg"
test -f "$install_dir/GameViewer.exe"
test -f "$install_dir/bin/GameViewerServer.exe"
test -f "$install_dir/bin/GameViewerHealthd.exe"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$install_dir/bin/Upgrade.exe"
test -f "$install_dir/bin/Upgrade.uu-remote-original.exe"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/wevtapi.dll" \
    "$install_dir/bin/wevtapi.dll"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" \
    "$prefix/drive_c/uu-remote-input-hook.dll"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe" \
    "$prefix/drive_c/uu-remote-input-injector.exe"
test -f "$install_dir/bin/.webview-ready"
test -f "$cache_dir/UURemote_Setup.exe"
grep -q '^version=4.34.0.8979$' "$cache_dir/installer-metadata"
grep -q '^INSTALL$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^WEBVIEW$' "$UU_REMOTE_FAKE_TRACE"
test -f "$state_dir/setup.log"
grep -q '^REG_ADD_GAMEVIEWER_USEXIM=N$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^REG_ADD_GAMEVIEWER_USETAKEFOCUS=Y$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^REG_ADD_GAMEVIEWERSERVER_VERSION=win7$' "$UU_REMOTE_FAKE_TRACE"
"$launcher" --diagnose | grep -q \
    'Wine 服务端兼容.*已隔离 Windows 内核驱动'
"$launcher" --diagnose | grep -q \
    'Wine 本地输入法.*已禁用（远端输入法接管）'
"$launcher" --diagnose | grep -q \
    'Wine 主控焦点协议.*应用级启用'

printf '%s\r\n' \
    '[hook]' \
    'pid=947' \
    'version=10' \
    'status_bits=127' \
    '[window_state]' \
    'mode=wndproc-arbiter' \
    'subclassed=4' \
    'transitions=5674' \
    'storms_detected=1' \
    'storms_resolved=1' \
    'blocked_activations=26' \
    'modal_latches=1' \
    'post_modal_handoffs=1' \
    >"$prefix/drive_c/uu-remote-focus-hook-status.ini"
"$launcher" --diagnose | grep -q \
    '主控焦点稳定.*生效中（v10；顶层窗口 4，争用 1/1，阻止抢焦 26，接管切换 1/1）'

printf '%s\r\n' \
    '[hook]' \
    'pid=948' \
    'version=8' \
    'status_bits=15' \
    'preloaded=1' \
    '[calls]' \
    'addresses=2' \
    'info=0' \
    'if_table2=1' \
    '[patched]' \
    'addresses=1' \
    'info=0' \
    'if_table2=1' \
    >"$prefix/drive_c/uu-remote-wol-hook-status.ini"
"$launcher" --diagnose |
    grep -q 'WOL 进程实测.*v8，地址 1/2，旧接口 0/0，网卡表 1/1'

# New UU releases must retain WOL through the generic Win32 adapter and
# PowerShell bridges even when their GameViewerServer.exe hash has no legacy
# binary-patch profile.  The unknown server fixture must never be modified.
export UU_REMOTE_WOL_DETECT_ROW=$'eth-test\tMock Wired\t58:11:22:76:19:64\t10.6.15.254\tmagic\tenabled'
export UU_REMOTE_WOL_NATIVE_IP=10.6.12.133
export UU_REMOTE_WOL_REFERENCE_IP=10.6.12.133
wol_server="$install_dir/bin/GameViewerServer.exe"
wol_server_sha_before=$(sha256sum "$wol_server" | cut -d' ' -f1)
"$launcher" --enable-wol
test "$(sha256sum "$wol_server" | cut -d' ' -f1)" = \
    "$wol_server_sha_before"
test -f "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
grep -q '^version=2$' "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
grep -q '^server_mode=win32-adapter-mapping$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
grep -q '^enabled=1$' "$prefix/drive_c/uu-remote-wol-bridge.ini"
grep -q '^gateway=10.6.15.254$' \
    "$prefix/drive_c/uu-remote-wol-bridge.ini"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe" \
    "$install_dir/bin/powershell.exe"
"$launcher" --diagnose >"$test_root/wol-generic-diagnose.txt"
grep -q 'UU WOL 兼容桥.*已启用' "$test_root/wol-generic-diagnose.txt"
grep -q 'UU WOL 服务端兼容.*通用 Win32 网卡映射' \
    "$test_root/wol-generic-diagnose.txt"
grep -q 'WOL Win32 网卡映射.*完整' \
    "$test_root/wol-generic-diagnose.txt"
"$launcher" --disable-wol
test "$(sha256sum "$wol_server" | cut -d' ' -f1)" = \
    "$wol_server_sha_before"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
test ! -e "$prefix/drive_c/uu-remote-wol-bridge.ini"
test ! -e "$install_dir/bin/powershell.exe"

# A future binary without a packaged row gets a local, hash-bound profile.
# The fixture deliberately uses a different function prologue to verify that
# the bridge consumes the generated before-bytes instead of a hardcoded value.
truncate -s 8192 "$wol_server"
printf '\125\110\211' |
    dd of="$wol_server" bs=1 seek=4096 conv=notrunc status=none
wol_adaptive_original_sha=$(sha256sum "$wol_server" | cut -d' ' -f1)
empty_wol_profiles="$test_root/empty-wol-profiles.tsv"
: >"$empty_wol_profiles"
UU_REMOTE_WOL_COMPATIBILITY_PROFILES="$empty_wol_profiles" \
UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
    "$launcher" --enable-wol
test "$(od -An -tx1 -j 4096 -N 3 "$wol_server" | tr -d ' \n')" = b001c3
test -s "$XDG_DATA_HOME/uu-remote-for-linux/wol-generated.tsv"
grep -q $'^4.34.0.8979\t.*\t.*\t0x1000\t554889\tb001c3$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-generated.tsv"
grep -q '^server_mode=adaptive-binary-patch$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
"$launcher" --diagnose >"$test_root/wol-adaptive-diagnose.txt"
grep -q 'UU WOL 服务端兼容.*自适应档案补丁完整' \
    "$test_root/wol-adaptive-diagnose.txt"
"$launcher" --disable-wol
test "$(sha256sum "$wol_server" | cut -d' ' -f1)" = \
    "$wol_adaptive_original_sha"
test ! -e "$install_dir/bin/GameViewerServer.uu-remote-wol-original.exe"
# Keep WOL enabled across the following failed and successful UU updates. The
# transaction must restore this binary before install, preserve it on rollback,
# then safely fall back when the next mock server has no unique PE profile.
"$launcher" --enable-wol
grep -q '^server_mode=adaptive-binary-patch$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
# Keep the deterministic physical-adapter fixture active while WOL remains
# enabled. Later setup and update transactions intentionally regenerate the
# mapping; falling through to the runner's real network would make this test
# depend on whether CI happens to expose a Wake-on-LAN-capable Ethernet NIC.

install_count=$(grep -c '^INSTALL$' "$UU_REMOTE_FAKE_TRACE")
webview_count=$(grep -c '^WEBVIEW$' "$UU_REMOTE_FAKE_TRACE")

"$launcher" --setup-only
"$launcher" --repair --setup-only

test "$(grep -c '^INSTALL$' "$UU_REMOTE_FAKE_TRACE")" -eq "$install_count"
test "$(grep -c '^WEBVIEW$' "$UU_REMOTE_FAKE_TRACE")" -eq "$webview_count"

encoder_cache="$install_dir/config/streamer/encoder_codec_capability_cache.json"
encoder_cache_backup="$XDG_DATA_HOME/uu-remote-for-linux/encoder-cache-before-hwencode.json"
"$launcher" --enable-hwencode
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-enabled"
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-manifest"
test -e "$state_dir/hwencode-probe-status"
grep -q 'base_slots=1' "$state_dir/setup.log"
grep -q 'long_slots=4' "$state_dir/setup.log"
grep -q 'wrapper_vtable_rva=0x3280' "$state_dir/setup.log"
grep -q 'wrapper_slot_rva=0x3298' "$state_dir/setup.log"
grep -q 'wrapper_ctor_refs=1' "$state_dir/setup.log"
grep -q 'wrapper_rtti=none' "$state_dir/setup.log"
grep -q 'encoder_accessor_rva=0x1400' "$state_dir/setup.log"
grep -q 'encoder_path_candidates=1' "$state_dir/setup.log"
grep -q 'nvenc_v8_impl=0' "$state_dir/setup.log"
grep -q 'nvenc_v11_impl=1' "$state_dir/setup.log"
grep -q 'impl_33=software' "$state_dir/setup.log"
grep -q 'impl_switch_candidates=1' "$state_dir/setup.log"
grep -q '^HWENCODE_PROBE$' "$UU_REMOTE_FAKE_TRACE"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/hwdecode/wine/x86_64-windows/nvencodeapi64.dll" \
    "$prefix/drive_c/windows/system32/nvEncodeAPI64.dll"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe" \
    "$install_dir/bin/uu-remote-nvenc-d3d11-probe.exe"
python3 - "$encoder_cache" <<'PY'
import json
import sys

rows = json.load(open(sys.argv[1], encoding="utf-8"))["encoder_capabilities"]
assert not any(row.get("codec_impl") == 6 for row in rows)
assert not any(row.get("codec_impl") in {1, 33} for row in rows)
assert {row.get("video_codec") for row in rows if row.get("codec_impl") == 0} >= {1, 2}
assert all(
    row.get("adapter_id") == 1012 and row.get("device_id") == 8578
    for row in rows
    if row.get("codec_impl") == 0
)
PY
test -e "$encoder_cache_backup"
"$launcher" --diagnose | grep -q \
    'NVENC 编码策略.*已启用（NVENC/HEVC 深层验证通过）'
"$launcher" --disable-hwencode
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-disabled"
test ! -e "$prefix/drive_c/windows/system32/nvEncodeAPI64.dll"
test ! -e "$encoder_cache_backup"
python3 - "$encoder_cache" <<'PY'
import json
import sys

rows = json.load(open(sys.argv[1], encoding="utf-8"))["encoder_capabilities"]
assert any(row.get("codec_impl") == 6 for row in rows)
PY

streamer="$install_dir/bin/streamer.dll"
streamer_original="$install_dir/bin/streamer.uu-remote-original.dll"
decoder_cache="$install_dir/config/streamer/decoder_codec_capability_cache.json"
decoder_cache_backup="$XDG_DATA_HOME/uu-remote-for-linux/decoder-cache-before-hwdecode.json"
export UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA
UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA=$(sha256sum "$streamer")
UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA=${UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA%% *}
patched_fixture="$test_root/patched-streamer.dll"
cp "$streamer" "$patched_fixture"
printf '\262\041\220' |
    dd of="$patched_fixture" bs=1 seek=$((0x94dc03)) conv=notrunc status=none
printf '\152\041\130' |
    dd of="$patched_fixture" bs=1 seek=$((0x94e4ee)) conv=notrunc status=none
export UU_REMOTE_HWDECODE_STREAMER_PATCHED_SHA
UU_REMOTE_HWDECODE_STREAMER_PATCHED_SHA=$(sha256sum "$patched_fixture")
UU_REMOTE_HWDECODE_STREAMER_PATCHED_SHA=${UU_REMOTE_HWDECODE_STREAMER_PATCHED_SHA%% *}

"$launcher" --enable-hwdecode
grep -q '^selection=nvidia:0$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/decoder-selection"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/hwdecode/dxvk/x64/d3d11.dll" \
    "$install_dir/bin/d3d11.dll"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/hwdecode/dxvk/x64/dxgi.dll" \
    "$install_dir/bin/dxgi.dll"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/hwdecode/wine/x86_64-windows/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/hwdecode/wine/x86_64-windows/nvcuvid.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$UU_REMOTE_HWDECODE_STREAMER_PATCHED_SHA"
test "$(sha256sum "$streamer_original" | cut -d' ' -f1)" = \
    "$UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA"
test ! -e "$decoder_cache"
grep -q '"mock":"original-cache"' "$decoder_cache_backup"
"$launcher" --diagnose | grep -q '硬解桥开关.*已启用'
"$launcher" --diagnose | grep -q 'UU 内置更新开关.*开启'
"$launcher" --diagnose | grep -q 'Linux 自动更新.*每次启动检查'
"$launcher" --diagnose | grep -q '官方更新器保护.*完整'
"$launcher" --diagnose | grep -q 'Win64 输入钩子.*完整'

"$launcher" --check-update
"$launcher" --diagnose | grep -q '安全更新状态.*已是最新版本'

assert_update_check_failure() {
    local failure=$1 expected=$2
    local stdout_file="$test_root/update-${failure}.out"
    local stderr_file="$test_root/update-${failure}.err"

    if UU_REMOTE_FAKE_CURL_FAILURE="$failure" \
        "$launcher" --check-update >"$stdout_file" 2>"$stderr_file"; then
        printf '模拟更新检查故障 %s 被意外接受。\n' "$failure" >&2
        exit 1
    fi
    grep -Fq "$expected" "$stderr_file"
    grep -Fq "$expected" "$state_dir/update-status"
}

assert_update_check_failure \
    dns 'DNS 解析失败：无法解析网易官方域名 api.nrd.nie.163.com（curl 错误 6）'
assert_update_check_failure \
    dns-timeout 'DNS 解析超时：未能解析网易官方域名 api.nrd.nie.163.com（curl 错误 28）'
assert_update_check_failure \
    proxy '代理解析失败：无法解析当前代理服务器（curl 错误 5）'
assert_update_check_failure \
    connect '连接失败：无法连接网易官方接口 api.nrd.nie.163.com（curl 错误 7）'
assert_update_check_failure \
    tls 'TLS 证书校验失败：系统不信任网易官方接口返回的证书链（curl 错误 60）'
assert_update_check_failure \
    http '网易官方接口返回 HTTP 503（curl 错误 22）'
assert_update_check_failure \
    reset '接收网易官方接口响应失败或连接被重置（curl 错误 56）'
assert_update_check_failure \
    version-format '网易官方接口已响应，但下载文件名无法识别版本（文件名：UURemote_Setup_latest_gwqd.exe）'

protected_before="$test_root/protected-before.sha256"
protected_after="$test_root/protected-after.sha256"
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uu-remote-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll" \
    "$install_dir/bin/GameViewerServer.exe" \
    "$install_dir/bin/GameViewerServer.uu-remote-wol-original.exe" \
    "$prefix/drive_c/uu-remote-input-hook.dll" >"$protected_before"
if UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
    UU_REMOTE_FAKE_INSTALLER_VERSION=4.34.0.8979 \
    "$launcher" --check-update; then
    printf '版本核验失败的通用更新被意外接受。\n' >&2
    exit 1
fi
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uu-remote-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll" \
    "$install_dir/bin/GameViewerServer.exe" \
    "$install_dir/bin/GameViewerServer.uu-remote-wol-original.exe" \
    "$prefix/drive_c/uu-remote-input-hook.dll" >"$protected_after"
cmp -s "$protected_before" "$protected_after"
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-enabled"
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-manifest"

UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.35.0.9000 \
    "$launcher" --check-update
"$launcher" --diagnose | grep -q 'UU 版本.*4.35.0.9000'
"$launcher" --diagnose | grep -q '安全更新状态.*已更新到 4.35.0.9000'
grep -q '^server_mode=win32-adapter-mapping$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-enabled"
test -s "$XDG_DATA_HOME/uu-remote-for-linux/wol-generated.tsv"
test ! -e "$install_dir/bin/GameViewerServer.uu-remote-wol-original.exe"
"$launcher" --diagnose | grep -q \
    'UU WOL 服务端兼容.*通用 Win32 网卡映射'
test ! -e "$install_dir/bin/d3d11.dll"
test ! -e "$install_dir/bin/dxgi.dll"
test ! -e "$streamer_original"
grep -q '^selection=nvidia:0$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/decoder-selection"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$install_dir/bin/Upgrade.exe"
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll" \
    "$prefix/drive_c/uu-remote-input-hook.dll"

handoff_clients_before=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_UPDATE_NO_RESTART=0 \
UU_REMOTE_UPSTREAM_UPDATE_EXIT_GRACE=0 \
UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.35.0.9000 \
    "$launcher" --upstream-update-handoff
for _attempt in {1..50}; do
    handoff_clients_after=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
    ((handoff_clients_after > handoff_clients_before)) && break
    sleep 0.1
done
((handoff_clients_after > handoff_clients_before))
grep -q 'TRAY_PROXY --quiesce-controller .*--grace-seconds 0' \
    "$UU_REMOTE_FAKE_TRACE"

# The built-in updater handoff also has to restore the existing client when
# its isolated official-version check fails.  It still reports the failed
# check to its caller after the restart succeeds.
handoff_clients_before=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
if UU_REMOTE_UPDATE_NO_RESTART=0 \
    UU_REMOTE_UPSTREAM_UPDATE_EXIT_GRACE=0 \
    UU_REMOTE_FAKE_CURL_FAILURE=dns \
    "$launcher" --upstream-update-handoff \
    >"$test_root/handoff-update-dns.out" \
    2>"$test_root/handoff-update-dns.err"; then
    printf '模拟 DNS 失败的内置更新交接被意外报告为成功。\n' >&2
    exit 1
fi
for _attempt in {1..50}; do
    handoff_clients_after=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
    ((handoff_clients_after > handoff_clients_before)) && break
    sleep 0.1
done
((handoff_clients_after > handoff_clients_before))
grep -Fq \
    'DNS 解析失败：无法解析网易官方域名 api.nrd.nie.163.com' \
    "$test_root/handoff-update-dns.err"

installer_sha=$(sed -n 's/^sha256=//p' "$cache_dir/installer-metadata")
server_sha=$(sha256sum "$install_dir/bin/GameViewerServer.exe")
server_sha=${server_sha%% *}
test_profile="$test_root/update-compatibility.tsv"
printf '4.34.0.8979\t%s\t%s\t%s\n' \
    "$installer_sha" "$UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA" "$server_sha" \
    >"$test_profile"

printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
UU_REMOTE_UPDATE_COMPATIBILITY_PROFILES="$test_profile" "$launcher" --check-update
"$launcher" --diagnose | grep -q 'UU 版本.*4.34.0.8979'
"$launcher" --diagnose | grep -q '安全更新状态.*已更新到 4.34.0.8979'
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$install_dir/bin/Upgrade.exe"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$UU_REMOTE_HWDECODE_STREAMER_PATCHED_SHA"

printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uu-remote-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll" >"$protected_before"
if UU_REMOTE_FAKE_CORRUPT_STREAMER=1 \
    UU_REMOTE_UPDATE_COMPATIBILITY_PROFILES="$test_profile" \
    "$launcher" --check-update; then
    printf '损坏的更新被意外接受。\n' >&2
    exit 1
fi
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uu-remote-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll" >"$protected_after"
cmp -s "$protected_before" "$protected_after"
"$launcher" --diagnose | grep -q 'UU 版本.*4.33.0.8000'
"$launcher" --diagnose | grep -q '安全更新状态.*更新失败并已回滚'
printf '4.34.0.8979\n' >"$install_dir/bin/.installed-version"

"$launcher" --diagnose | grep -q '4.34.0.8979'
"$launcher"
grep -q '^HEALTHD$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^SERVER$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^CLIENT$' "$UU_REMOTE_FAKE_TRACE"
grep -q \
    'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client .*--property=ExitType=cgroup.*GameViewer.exe' \
    "$UU_REMOTE_FAKE_TRACE"
if grep -q '^PROXY_ENV_LEAK=' "$UU_REMOTE_FAKE_TRACE"; then
    printf 'Wine 运行进程仍继承了 Linux 代理环境变量：\n' >&2
    grep '^PROXY_ENV_LEAK=' "$UU_REMOTE_FAKE_TRACE" >&2
    exit 1
fi
grep -q '^TRAY_PROXY ' "$UU_REMOTE_FAKE_TRACE"
grep -q '^HWDECODE_ENV$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^CUDA_DEVICE=0$' "$UU_REMOTE_FAKE_TRACE"

# A live focus hook turns --show into a one-shot authorization and then starts
# a second GameViewer instance. UU's single-instance IPC must perform the real
# Qt show so WebView2 resumes together with the native window.
printf '%s\r\n' \
    '[hook]' \
    'pid=949' \
    'version=12' \
    'status_bits=63' \
    '[worker]' \
    'heartbeats=10' \
    >"$prefix/drive_c/uu-remote-focus-hook-status.ini"
show_request="$prefix/drive_c/uu-remote-home-show.request"
(
    for _attempt in {1..100}; do
        if [[ -e $show_request ]]; then
            rm -f -- "$show_request"
            exit 0
        fi
        sleep 0.02
    done
    exit 1
) &
show_consumer_pid=$!
client_count_before_show=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
"$launcher" --show
wait "$show_consumer_pid"
test ! -e "$show_request"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -eq \
    "$((client_count_before_show + 1))"

# A fresh worker status is not sufficient proof that the Qt UI thread is
# alive. Two real heartbeat timeouts must prevent another single-instance
# activation from being sent into the hard-stalled controller.
printf '%s\r\n' \
    '[hook]' \
    'pid=949' \
    'version=18' \
    'status_bits=1023' \
    '[window_state]' \
    'subclassed=1' \
    '[ui_health]' \
    'pings_sent=12' \
    'pings_acked=10' \
    'consecutive_timeouts=2' \
    'hard_stalls_detected=1' \
    'last_ack_age_ms=25000' \
    '[worker]' \
    'heartbeats=20' \
    >"$prefix/drive_c/uu-remote-focus-hook-status.ini"
client_count_before_show=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
show_output=$("$launcher" --show)
grep -Fq '主界面心跳停止' <<<"$show_output"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -eq \
    "$client_count_before_show"
rm -f -- "$prefix/drive_c/uu-remote-focus-hook-status.ini"

grep -q 'SYSTEMD_RUN .*--unit=uu-remote-for-linux-healthd' "$UU_REMOTE_FAKE_TRACE"
grep -q 'SYSTEMD_RUN .*--unit=uu-remote-for-linux-server' "$UU_REMOTE_FAKE_TRACE"
if grep -q -- '--watch-module GameViewer.exe Qt5Core.dll' \
    "$UU_REMOTE_FAKE_TRACE"; then
    printf '已禁用的主控焦点稳定器仍被启动。\n' >&2
    exit 1
fi
if grep -q '^FD9_LEAK ' "$UU_REMOTE_FAKE_TRACE"; then
    printf '操作锁文件描述符被泄漏给子进程：\n' >&2
    grep '^FD9_LEAK ' "$UU_REMOTE_FAKE_TRACE" >&2
    exit 1
fi

exec 8>"$state_dir/operation.lock"
flock 8
if UU_REMOTE_OPERATION_LOCK_TIMEOUT=0 "$launcher" --setup-only \
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
    "$XDG_DATA_HOME/uu-remote-for-linux/decoder-selection"
test ! -e "$install_dir/bin/d3d11.dll"
test ! -e "$install_dir/bin/dxgi.dll"
test ! -e "$prefix/drive_c/windows/system32/nvcuda.dll"
test ! -e "$prefix/drive_c/windows/system32/nvcuvid.dll"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$UU_REMOTE_HWDECODE_STREAMER_ORIGINAL_SHA"
test ! -e "$streamer_original"
grep -q '"mock":"original-cache"' "$decoder_cache"
test ! -e "$decoder_cache_backup"
"$launcher" --diagnose | grep -q '硬解桥开关.*未启用'

"$launcher" --decoder nvidia:0
"$launcher"
test -e "$install_dir/bin/d3d11.dll"
grep -q '^CUDA_DEVICE=0$' "$UU_REMOTE_FAKE_TRACE"
"$launcher" --decoder cpu
"$launcher"
test ! -e "$install_dir/bin/d3d11.dll"
"$launcher" --diagnose | grep -q '实际解码选择.*cpu'

client_count_before=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
stop_count_before=$(grep -c '^WINESERVER -k$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_ZENITY_CANCEL=1 "$launcher" --select-decoder-and-restart
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -eq "$client_count_before"
test "$(grep -c '^WINESERVER -k$' "$UU_REMOTE_FAKE_TRACE")" -eq \
    "$stop_count_before"
grep -q 'ZENITY .*--list' "$UU_REMOTE_FAKE_TRACE"

UU_REMOTE_FAKE_ZENITY_CHOICE=nvidia:0 \
    "$launcher" --select-decoder-and-restart
for _attempt in {1..30}; do
    if (( $(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE") > client_count_before )); then
        break
    fi
    sleep 0.1
done
grep -q '^selection=nvidia:0$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/decoder-selection"
test -e "$install_dir/bin/d3d11.dll"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -gt "$client_count_before"
test "$(grep -c '^WINESERVER -k$' "$UU_REMOTE_FAKE_TRACE")" -gt \
    "$stop_count_before"
grep -q '^CUDA_DEVICE=0$' "$UU_REMOTE_FAKE_TRACE"
grep -q \
    'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client .*--property=ExitType=cgroup.*--restart-selected-decoder' \
    "$UU_REMOTE_FAKE_TRACE"

# A failed startup update check must not abort the graphical launcher.  The
# update checker uses die() for command-line failures, so it has to remain
# isolated from the shell that continues into the current client launch.
client_count_before_failed_startup_update=$(grep -c '^CLIENT$' \
    "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_CURL_FAILURE=dns \
    "$launcher" \
    >"$test_root/startup-update-dns.out" \
    2>"$test_root/startup-update-dns.err"
grep -Fq \
    '启动时自动更新未完成；继续打开当前已安装版本' \
    "$test_root/startup-update-dns.err"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -gt \
    "$client_count_before_failed_startup_update"

# A normal graphical launch checks the official version before starting. An
# unrecognizable newer binary is installed with the generic bridges, while the
# requested NVIDIA choice is retained for a future safe profile.
printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
client_count_before_startup_update=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.35.0.9000 \
    "$launcher"
"$launcher" --diagnose | grep -q 'UU 版本.*4.35.0.9000'
test ! -e "$install_dir/bin/d3d11.dll"
grep -q '^selection=nvidia:0$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/decoder-selection"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -gt \
    "$client_count_before_startup_update"

# A subsequent official version whose NVDEC data flow remains recognizable
# must not wait for a package release. The update transaction derives a local
# hash-bound profile, validates it with the DXGI and official codec probes,
# persists it, and restores the requested NVIDIA bridge before returning.
empty_hwdecode_profiles="$test_root/empty-hwdecode-profiles.tsv"
: >"$empty_hwdecode_profiles"
UU_REMOTE_HWDECODE_COMPATIBILITY_PROFILES="$empty_hwdecode_profiles" \
UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
UU_REMOTE_FAKE_LATEST_VERSION=4.36.0.9300 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.36.0.9300 \
    "$launcher" --check-update
"$launcher" --diagnose | grep -q 'UU 版本.*4.36.0.9300'
"$launcher" --diagnose | grep -q '实际解码选择.*nvidia:0'
"$launcher" --diagnose | grep -q '硬解桥注入.*完整'
test -e "$install_dir/bin/d3d11.dll"
test -e "$streamer_original"
grep -q $'^4.36.0.9300\t' \
    "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-generated.tsv"
grep -q 'NVDEC 自动补丁运行验证通过' "$state_dir/setup.log"
grep -q '^HWDECODE_DXGI_PROBE$' "$UU_REMOTE_FAKE_TRACE"
grep -q '^HWDECODE_CODEC_PROBE$' "$UU_REMOTE_FAKE_TRACE"

"$launcher" --stop
grep -q '^WINESERVER -k$' "$UU_REMOTE_FAKE_TRACE"

tray_exit_trace="$test_root/tray-exit-stop.log"
UU_REMOTE_FAKE_TRACE="$tray_exit_trace" \
UU_REMOTE_TRAY_EXIT_HELPER=1 \
    "$launcher" --stop
grep -q \
    '^SYSTEMCTL --user stop uu-remote-for-linux-tray.service$' \
    "$tray_exit_trace"
if grep -q \
    'SYSTEMCTL --user stop .*uu-remote-for-linux-tray-exit.service' \
    "$tray_exit_trace"; then
    printf '托盘退出助手尝试停止自身。\n' >&2
    exit 1
fi
grep -q '^WINESERVER -k$' "$tray_exit_trace"

printf '模拟 Wine 安装状态机检查通过。\n'
