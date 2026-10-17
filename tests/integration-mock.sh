#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly launcher="$project_root/bin/uu-remote-for-linux"
readonly fixture_bin="$project_root/tests/fixtures/bin"
package_version=$(<"$project_root/lib/uu-remote-for-linux/package-version")
readonly package_version_pattern=${package_version//./\\.}

test_root=$(mktemp -d)
cleanup() {
    if [[ ${UU_REMOTE_KEEP_TEST_ROOT:-0} == 1 ]]; then
        printf '保留集成测试目录：%s\n' "$test_root" >&2
    else
        rm -rf -- "$test_root"
    fi
}
trap cleanup EXIT

export PATH="$fixture_bin:$PATH"
export XDG_DATA_HOME="$test_root/data"
export XDG_STATE_HOME="$test_root/state"
export XDG_CACHE_HOME="$test_root/cache"
export XDG_CONFIG_HOME="$test_root/config"
export UU_REMOTE_RUNTIME_DIR="$project_root/lib/uu-remote-for-linux"
export UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler"
export UU_REMOTE_RUNTIME_PROFILER_BIN="$fixture_bin/runtime-profiler"
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

case "$XDG_CONFIG_HOME" in
    "$test_root"/*) ;;
    *)
        printf '集成测试配置目录未隔离：%s\n' "$XDG_CONFIG_HOME" >&2
        exit 1
        ;;
esac

"$launcher" --diagnose | grep -q '桌面后端.*x11'
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test-0 \
    "$launcher" --diagnose | grep -q '桌面后端.*wayland-xwayland'
UU_REMOTE_NVIDIA_SMI_BIN="$fixture_bin/nvidia-smi-active" \
    "$launcher" --diagnose | grep -q \
    '最近被控编码器.*NVIDIA NVENC（实时编码，GPU ENC 37%）'

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
grep -q 'CURL --proto =https --proto-redir =https ' \
    "$UU_REMOTE_FAKE_TRACE"
grep -q 'OSSLSIGNCODE verify -in .* -CAfile /etc/ssl/certs/ca-certificates.crt' \
    "$UU_REMOTE_FAKE_TRACE"
grep -q 'ZENITY .*--text-info' "$UU_REMOTE_FAKE_TRACE"
grep -q 'ZENITY .*--checkbox=' "$UU_REMOTE_FAKE_TRACE"
grep -q 'ZENITY .*--ok-label=接受并继续' "$UU_REMOTE_FAKE_TRACE"
grep -q '^CLIENT$' "$UU_REMOTE_FAKE_TRACE"

prefix="$XDG_DATA_HOME/uu-remote-for-linux/wineprefix"
install_dir="$prefix/drive_c/Program Files/Netease/GameViewer"
cache_dir="$XDG_CACHE_HOME/uu-remote-for-linux"
state_dir="$XDG_STATE_HOME/uu-remote-for-linux"
installer_before=$(sha256sum "$cache_dir/UURemote_Setup.exe")
installer_before=${installer_before%% *}
for signature_failure in UU_REMOTE_FAKE_SIGNATURE_FAILURE \
    UU_REMOTE_FAKE_SIGNATURE_WRONG_PUBLISHER; do
    if env "$signature_failure=1" "$launcher" --refresh-installer \
        >"$test_root/${signature_failure}.out" \
        2>"$test_root/${signature_failure}.err"; then
        printf '无效安装包签名被意外接受：%s\n' "$signature_failure" >&2
        exit 1
    fi
    grep -q 'Authenticode 证书链或网易发布者身份验证失败' \
        "$test_root/${signature_failure}.err"
    test "$(sha256sum "$cache_dir/UURemote_Setup.exe" | cut -d' ' -f1)" = \
        "$installer_before"
done
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
test -s "$XDG_DATA_HOME/uu-remote-for-linux/streamer-compatibility.json"
"$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy" \
    match-streamer-profile \
    "$install_dir/bin/streamer.dll" \
    4.34.0.8979 \
    "$XDG_DATA_HOME/uu-remote-for-linux/streamer-compatibility.json" \
    >/dev/null
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
rm -f -- "$XDG_DATA_HOME/uu-remote-for-linux/wol-generated.tsv"
UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
    "$launcher" --disable-wol
test "$(sha256sum "$wol_server" | cut -d' ' -f1)" = \
    "$wol_adaptive_original_sha"
test ! -e "$install_dir/bin/GameViewerServer.uu-remote-wol-original.exe"
grep -q $'^4.34.0.8979\t' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-generated.tsv"
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
grep -q $'\tluid=4045620583\t' "$state_dir/hwencode-probe-status"
grep -q 'base_slots=1' "$state_dir/setup.log"
grep -q 'long_slots=4' "$state_dir/setup.log"
grep -q 'wrapper_vtable_rva=0x3280' "$state_dir/setup.log"
grep -q 'wrapper_slot_rva=0x3298' "$state_dir/setup.log"
grep -q 'gdi_adapter_slot_index=5' "$state_dir/setup.log"
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
assert {
    row.get("video_codec")
    for row in rows
    if row.get("codec_impl") == 0
    and row.get("width", 0) >= 1080
    and row.get("height", 0) >= 1920
} >= {1, 2}
assert {
    row.get("video_codec")
    for row in rows
    if row.get("codec_impl") == 0
    and row.get("width", 0) >= 1920
    and row.get("width") == row.get("height")
} >= {1, 2}
assert all(
    row.get("adapter_id") == 4045620583 and row.get("device_id") == 8578
    for row in rows
    if row.get("codec_impl") == 0
)
PY
test -e "$encoder_cache_backup"
"$launcher" --diagnose | grep -q \
    'NVENC 编码策略.*已启用（NVENC/HEVC 深层验证通过）'
UU_REMOTE_FAKE_SYSTEMD_RUN_NOEXEC=1 "$launcher"
grep -q \
    'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client .*--setenv=UU_REMOTE_NVENC_ADAPTER_LUID=4045620583' \
    "$UU_REMOTE_FAKE_TRACE"
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

if UU_REMOTE_FAKE_HWENCODE_PROBE_FAILURE=1 \
    "$launcher" --enable-hwencode \
        >"$test_root/hwencode-transaction.out" \
        2>"$test_root/hwencode-transaction.err"; then
    printf '深层探测失败的 NVENC 部署被意外接受。\n' >&2
    exit 1
fi
grep -q 'NVENC 部署失败，已精确恢复部署前文件' \
    "$test_root/hwencode-transaction.err"
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-disabled"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-enabled"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-manifest"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/dxvk-hwencode.conf"
test ! -e "$prefix/drive_c/windows/system32/nvEncodeAPI64.dll"
test ! -e "$install_dir/bin/uu-remote-nvenc-d3d11-probe.exe"
test ! -e "$install_dir/bin/d3d11.dll"
test ! -e "$install_dir/bin/dxgi.dll"

streamer="$install_dir/bin/streamer.dll"
streamer_original="$install_dir/bin/streamer.uu-remote-original.dll"
decoder_cache="$install_dir/config/streamer/decoder_codec_capability_cache.json"
decoder_cache_backup="$XDG_DATA_HOME/uu-remote-for-linux/decoder-cache-before-hwdecode.json"
hwdecode_row=$("$UU_REMOTE_HWDECODE_PROFILER_BIN" \
    --derive "$streamer" --version 4.34.0.8979)
IFS=$'\t' read -r _ expected_streamer_original_sha \
    expected_streamer_patched_sha _ <<<"$hwdecode_row"
[[ $expected_streamer_original_sha =~ ^[[:xdigit:]]{64}$ &&
    $expected_streamer_patched_sha =~ ^[[:xdigit:]]{64}$ ]]

if UU_REMOTE_FAKE_INSTALL_FAILURE_SUFFIX=nvcuvid.dll.uu-remote-new \
    "$launcher" --enable-hwdecode \
        >"$test_root/hwdecode-transaction.out" \
        2>"$test_root/hwdecode-transaction.err"; then
    printf '中途写入失败的 NVDEC 部署被意外接受。\n' >&2
    exit 1
fi
grep -q 'NVDEC 部署失败，已精确恢复部署前文件' \
    "$test_root/hwdecode-transaction.err"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$expected_streamer_original_sha"
grep -q '"mock":"original-cache"' "$decoder_cache"
test ! -e "$streamer_original"
test ! -e "$decoder_cache_backup"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-enabled"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-manifest"
test ! -e "$install_dir/bin/d3d11.dll"
test ! -e "$install_dir/bin/dxgi.dll"
test ! -e "$prefix/drive_c/windows/system32/nvcuda.dll"
test ! -e "$prefix/drive_c/windows/system32/nvcuvid.dll"
test ! -e "$install_dir/bin/uu-remote-dxgi-probe.exe"

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
    "$expected_streamer_patched_sha"
test "$(sha256sum "$streamer_original" | cut -d' ' -f1)" = \
    "$expected_streamer_original_sha"
test ! -e "$decoder_cache"
grep -q '"mock":"original-cache"' "$decoder_cache_backup"
rm -f -- "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-generated.tsv"
"$launcher" --disable-hwdecode
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$expected_streamer_original_sha"
test ! -e "$streamer_original"
grep -q $'^4.34.0.8979\t' \
    "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-generated.tsv"
"$launcher" --enable-hwdecode
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$expected_streamer_patched_sha"
"$launcher" --diagnose | grep -q '硬解桥开关.*已启用'
"$launcher" --diagnose | grep -q 'UU 内置更新开关.*开启'
"$launcher" --diagnose | grep -q \
    'Linux 自动更新.*图形启动延迟 60 秒后台检查'
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
    UU_REMOTE_FAKE_INSTALL_SYSTEM32_NVENC=1 \
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
test ! -e "$prefix/drive_c/windows/system32/nvEncodeAPI64.dll"

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
test -e "$install_dir/bin/d3d11.dll"
test -e "$install_dir/bin/dxgi.dll"
test -e "$streamer_original"
grep -q $'^4\.35\.0\.9000\t' \
    "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-generated.tsv"
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

printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
"$launcher" --check-update
"$launcher" --diagnose | grep -q 'UU 版本.*4.34.0.8979'
"$launcher" --diagnose | grep -q '安全更新状态.*已更新到 4.34.0.8979'
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe" \
    "$install_dir/bin/Upgrade.exe"
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$expected_streamer_patched_sha"

# NetEase's release endpoint load-balances between staged releases.  The
# check keeps the newest probed release and downloads exactly that URL.
printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
rm -f -- "$UU_REMOTE_FAKE_TRACE.head-probes"
trace_lines_before_probe=$(wc -l <"$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_HEAD_VERSIONS='4.33.5.8500 4.34.0.8979 4.33.5.8500' \
    "$launcher" --check-update
test "$(<"$UU_REMOTE_FAKE_TRACE.head-probes")" -eq 3
tail -n "+$((trace_lines_before_probe + 1))" "$UU_REMOTE_FAKE_TRACE" |
    grep '^CURL .*--output .* https://a56\.gdl\.netease\.com/UURemote_Setup_4\.34\.0\.8979_' \
    >/dev/null
"$launcher" --diagnose | grep -q 'UU 版本.*4.34.0.8979'
"$launcher" --diagnose | grep -q '安全更新状态.*已更新到 4.34.0.8979'

# Even a resolved URL may land on another release; the transaction then
# targets the verified installer it actually downloaded.
printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
UU_REMOTE_FAKE_LATEST_VERSION=4.33.5.8500 \
UU_REMOTE_FAKE_DOWNLOAD_VERSION=4.34.0.8979 \
    "$launcher" --check-update >"$test_root/update-staged-release.out"
grep -Fq '本次下载返回 4.34.0.8979（版本检查返回 4.33.5.8500）' \
    "$test_root/update-staged-release.out"
"$launcher" --diagnose | grep -q 'UU 版本.*4.34.0.8979'
"$launcher" --diagnose | grep -q '安全更新状态.*已更新到 4.34.0.8979'
test "$(sha256sum "$streamer" | cut -d' ' -f1)" = \
    "$expected_streamer_patched_sha"

sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uu-remote-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll" >"$protected_before"
if UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
    UU_REMOTE_FAKE_DOWNLOAD_VERSION=4.34.0.8979 \
    "$launcher" --check-update 2>"$test_root/update-stale-download.err"; then
    printf '不高于当前版本的官方下载被意外安装。\n' >&2
    exit 1
fi
grep -Fq '不高于当前版本 4.34.0.8979；现有客户端未作修改' \
    "$test_root/update-stale-download.err"
sha256sum \
    "$install_dir/bin/Upgrade.exe" \
    "$install_dir/bin/Upgrade.uu-remote-original.exe" \
    "$install_dir/bin/d3d11.dll" \
    "$install_dir/bin/dxgi.dll" \
    "$install_dir/bin/streamer.dll" \
    "$prefix/drive_c/windows/system32/nvcuda.dll" \
    "$prefix/drive_c/windows/system32/nvcuvid.dll" >"$protected_after"
cmp -s "$protected_before" "$protected_after"
"$launcher" --diagnose | grep -q 'UU 版本.*4.34.0.8979'
"$launcher" --diagnose | grep -q '安全更新状态.*检查失败：官方下载接口本次返回'

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
    UU_REMOTE_FAKE_RUNTIME_INCOMPATIBLE=1 \
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
grep -q '^uu_version=4.33.0.8000$' \
    "$XDG_DATA_HOME/uu-remote-for-linux/update-blocker-manifest"
UU_REMOTE_FAKE_LATEST_VERSION=4.34.0.8979 \
    "$launcher" --auto-update-check \
    >"$test_root/automatic-rollback-retry.out"
grep -q '自动更新已暂缓，保留 4.33.0.8000' \
    "$test_root/automatic-rollback-retry.out"
# A check that lands on another staged release must not bypass the hold on
# the rolled-back version the download actually resolves to.
UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
UU_REMOTE_FAKE_DOWNLOAD_VERSION=4.34.0.8979 \
    "$launcher" --auto-update-check \
    >"$test_root/automatic-rollback-retry-staged.out"
grep -q '自动更新已暂缓，保留 4.33.0.8000' \
    "$test_root/automatic-rollback-retry-staged.out"
"$launcher" --diagnose | grep -q 'UU 版本.*4.33.0.8000'
printf '4.34.0.8979\n' >"$install_dir/bin/.installed-version"

"$launcher" --diagnose | grep -q '4.34.0.8979'
frame_status="$prefix/drive_c/uu-remote-wayland-frame-status.ini"
printf '%s\r\n' \
    '[hook]' \
    'pid=5151' \
    'version=55' \
    'status_bits=12' \
    '[capture]' \
    'calls=0' \
    'rendered=0' \
    >"$frame_status"
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test-0 \
    "$launcher" --diagnose >"$test_root/wayland-idle-frame-hook.txt"
grep -q 'Wayland GDI 画面钩子.*已安装（v55；等待远控采集）' \
    "$test_root/wayland-idle-frame-hook.txt"
printf '%s\r\n' \
    '[hook]' \
    'pid=5151' \
    'version=55' \
    'status_bits=12' \
    '[capture]' \
    'calls=1' \
    'rendered=0' \
    >"$frame_status"
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test-0 \
    "$launcher" --diagnose >"$test_root/wayland-failed-frame-hook.txt"
grep -q 'Wayland GDI 画面钩子.*未安装/不完整' \
    "$test_root/wayland-failed-frame-hook.txt"
printf '%s\n' \
    '{"state":"stopped","capture_state":"active","capture_transport":"stopped","cursor_state":"metadata-inactive"}' \
    >"$state_dir/input-bridge-status.json"
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test-0 \
    "$launcher" --diagnose >"$test_root/wayland-stopped-bridge.txt"
grep -q '原生光标形状.*已停止' \
    "$test_root/wayland-stopped-bridge.txt"
grep -q 'Wayland 画面桥.*已停止' \
    "$test_root/wayland-stopped-bridge.txt"
grep -q 'Wayland GDI 画面钩子.*就绪（待应用启动）' \
    "$test_root/wayland-stopped-bridge.txt"
rm -f -- "$state_dir/input-bridge-status.json"
rm -f -- "$frame_status"
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

# A live UI heartbeat can belong to the splash, while required backend
# processes are missing. --show must finish initialization, then verify the
# real home instead of reporting a successful single-instance activation.
rm -f -- "$install_dir/bin/.healthd-running" "$install_dir/bin/.server-running"
touch "$prefix/drive_c/uu-remote-focus-hook-status.ini"
healthd_before=$(grep -c '^HEALTHD$' "$UU_REMOTE_FAKE_TRACE")
server_before=$(grep -c '^SERVER$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_HOME_MODE=needs-backend "$launcher" --show \
    >"$test_root/show-startup-race.out" 2>&1
test "$(grep -c '^HEALTHD$' "$UU_REMOTE_FAKE_TRACE")" -eq "$((healthd_before + 1))"
test "$(grep -c '^SERVER$' "$UU_REMOTE_FAKE_TRACE")" -eq "$((server_before + 1))"
grep -q '补齐运行依赖后重试' "$test_root/show-startup-race.out"
grep -q -- '--repaint-timeout-ms 15000' "$UU_REMOTE_FAKE_TRACE"

# Starting/activating a process successfully is not a visible-window success.
touch "$prefix/drive_c/uu-remote-focus-hook-status.ini"
if UU_REMOTE_FAKE_HOME_MODE=missing "$launcher" --show \
    >"$test_root/show-no-home.out" 2>&1; then
    printf '未显示主窗口时 --show 错误地返回成功。\n' >&2
    exit 1
fi
grep -q 'UU 主界面未在等待时间内显示' "$test_root/show-no-home.out"

# A fresh heartbeat file left by an exited controller must not skip full
# startup. Backend initialization happens before the first client launch.
touch "$prefix/drive_c/uu-remote-focus-hook-status.ini"
rm -f -- "$install_dir/bin/.healthd-running" "$install_dir/bin/.server-running"
healthd_before=$(grep -c '^HEALTHD$' "$UU_REMOTE_FAKE_TRACE")
client_before=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_CONTROLLER_RUNNING=0 UU_REMOTE_FAKE_HOME_MODE=needs-backend \
    "$launcher" --show >"$test_root/show-stale-hook.out" 2>&1
test "$(grep -c '^HEALTHD$' "$UU_REMOTE_FAKE_TRACE")" -eq "$((healthd_before + 1))"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -eq "$((client_before + 1))"
if grep -q '补齐运行依赖后重试' "$test_root/show-stale-hook.out"; then
    printf '已退出控制器的残留心跳错误触发快捷激活。\n' >&2
    exit 1
fi

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
    "$expected_streamer_original_sha"
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

# Neither login autostart nor a normal application-menu launch may wait for
# the network. Their versioned background watcher receives a grace period so
# the client is dispatched before an automatic update can acquire the prefix
# transaction lock.
for graphical_launch_arg in --autostart ''; do
    curl_count_before_graphical_launch=$(grep -c '^CURL ' \
        "$UU_REMOTE_FAKE_TRACE")
    client_unit_count_before_graphical_launch=$(grep -c \
        'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client ' \
        "$UU_REMOTE_FAKE_TRACE")
    if [[ -n $graphical_launch_arg ]]; then
        graphical_launch_args=("$graphical_launch_arg")
    else
        graphical_launch_args=()
    fi
    UU_REMOTE_DISABLE_UPDATE_WATCHER=0 \
    UU_REMOTE_FAKE_SYSTEMD_RUN_NOEXEC=1 \
    UU_REMOTE_FAKE_CURL_FAILURE=dns \
        "$launcher" "${graphical_launch_args[@]}"
    test "$(grep -c '^CURL ' "$UU_REMOTE_FAKE_TRACE")" -eq \
        "$curl_count_before_graphical_launch"
    test "$(grep -c 'SYSTEMD_RUN .*--unit=uu-remote-for-linux-client ' \
        "$UU_REMOTE_FAKE_TRACE")" -eq \
        "$((client_unit_count_before_graphical_launch + 1))"
    grep -q \
        "SYSTEMD_RUN .*--unit=uu-remote-for-linux-update .* $package_version_pattern 60\$" \
        "$UU_REMOTE_FAKE_TRACE"
done

# With the background watcher disabled, a normal graphical launch retains the
# synchronous compatibility fallback. A recognizable newer binary receives a
# freshly generated, hash-bound NVDEC profile without waiting for a package
# release.
printf '4.33.0.8000\n' >"$install_dir/bin/.installed-version"
client_count_before_startup_update=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_FAKE_LATEST_VERSION=4.35.0.9000 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.35.0.9000 \
    "$launcher"
"$launcher" --diagnose | grep -q 'UU 版本.*4.35.0.9000'
test -e "$install_dir/bin/d3d11.dll"
grep -q $'^4\.35\.0\.9000\t' \
    "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-generated.tsv"
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

# A post-4.38 release must rebuild all four version-bound features as one
# transaction: NVENC/portrait semantic profile, NVDEC binary profile and WOL
# capability profile. If any required semantic target disappears, the updater
# must restore the last fully compatible client instead of accepting a partial
# feature downgrade.
UU_REMOTE_DISABLE_AUTO_HWENCODE=0 "$launcher" --enable-hwencode
systemd_server_restarts_before=$(grep -c \
    'SYSTEMCTL --user restart uu-remote-for-linux-server.service' \
    "$UU_REMOTE_FAKE_TRACE" || true)
UU_REMOTE_DISABLE_AUTO_HWENCODE=0 \
UU_REMOTE_FAKE_STREAMER_FUTURE_COMPAT=1 \
UU_REMOTE_FAKE_SERVER_REGENERATE_ENCODER_CACHE=1 \
UU_REMOTE_HWDECODE_COMPATIBILITY_PROFILES="$empty_hwdecode_profiles" \
UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
UU_REMOTE_WOL_COMPATIBILITY_PROFILES="$empty_wol_profiles" \
UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
UU_REMOTE_FAKE_LATEST_VERSION=4.39.0.9400 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.39.0.9400 \
    "$launcher" --check-update
rm -f -- \
    "$install_dir/bin/.server-running" \
    "$install_dir/bin/.server-regenerated-encoder-cache"
UU_REMOTE_DISABLE_AUTO_HWENCODE=0 \
UU_REMOTE_FAKE_SERVER_REGENERATE_ENCODER_CACHE=1 \
UU_REMOTE_HWDECODE_COMPATIBILITY_PROFILES="$empty_hwdecode_profiles" \
UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
UU_REMOTE_WOL_COMPATIBILITY_PROFILES="$empty_wol_profiles" \
UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
    "$launcher" >"$test_root/future-adaptive-launch.out"
systemd_server_restarts_after=$(grep -c \
    'SYSTEMCTL --user restart uu-remote-for-linux-server.service' \
    "$UU_REMOTE_FAKE_TRACE" || true)
test "$systemd_server_restarts_after" -eq \
    "$((systemd_server_restarts_before + 1))"
grep -q '^SERVER_REGENERATED_ENCODER_CACHE$' "$UU_REMOTE_FAKE_TRACE"
grep -q 'UU 服务端已在 NVENC 档案生效后串行重启' \
    "$test_root/future-adaptive-launch.out"
UU_REMOTE_DISABLE_AUTO_HWENCODE=0 \
UU_REMOTE_HWDECODE_COMPATIBILITY_PROFILES="$empty_hwdecode_profiles" \
UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
UU_REMOTE_WOL_COMPATIBILITY_PROFILES="$empty_wol_profiles" \
UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
    "$launcher" >/dev/null
test "$(grep -c \
    'SYSTEMCTL --user restart uu-remote-for-linux-server.service' \
    "$UU_REMOTE_FAKE_TRACE")" -eq "$systemd_server_restarts_after"
"$launcher" --diagnose >"$test_root/future-adaptive-diagnose.txt"
grep -q 'UU 版本.*4.39.0.9400' "$test_root/future-adaptive-diagnose.txt"
grep -q '自适应语义档案.*完整（UU 4.39.0.9400；NVENC/竖屏）' \
    "$test_root/future-adaptive-diagnose.txt"
grep -q 'NVENC 编码策略.*已启用（NVENC/HEVC 深层验证通过）' \
    "$test_root/future-adaptive-diagnose.txt"
grep -q '硬解桥注入.*完整' "$test_root/future-adaptive-diagnose.txt"
grep -q 'UU WOL 服务端兼容.*自适应档案补丁完整' \
    "$test_root/future-adaptive-diagnose.txt"
awk -F '\t' '$1 == "4.39.0.9400" { found = 1 } END { exit !found }' \
    "$XDG_DATA_HOME/uu-remote-for-linux/hwdecode-generated.tsv"
awk -F '\t' '$1 == "4.39.0.9400" { found = 1 } END { exit !found }' \
    "$XDG_DATA_HOME/uu-remote-for-linux/wol-generated.tsv"
"$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy" \
    verify-streamer-profile \
    "$streamer" \
    4.39.0.9400 \
    "$XDG_DATA_HOME/uu-remote-for-linux/streamer-compatibility.json" \
    --require-portrait \
    >/dev/null
grep -q '"gdi_adapter_slot_index": 6' \
    "$XDG_DATA_HOME/uu-remote-for-linux/streamer-compatibility.json"
"$fixture_bin/runtime-profiler" verify \
    "$install_dir" 4.39.0.9400 \
    "$XDG_DATA_HOME/uu-remote-for-linux/runtime-compatibility.json" \
    --strict >/dev/null

# Even when the encoder, decoder and WOL semantic profilers all accept a
# future build, a missing input/focus/frame import contract must abort the
# transaction and restore the last complete version.
if UU_REMOTE_DISABLE_AUTO_HWENCODE=0 \
    UU_REMOTE_FAKE_STREAMER_FUTURE_COMPAT=1 \
    UU_REMOTE_FAKE_RUNTIME_INCOMPATIBLE=1 \
    UU_REMOTE_HWDECODE_COMPATIBILITY_PROFILES="$empty_hwdecode_profiles" \
    UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
    UU_REMOTE_WOL_COMPATIBILITY_PROFILES="$empty_wol_profiles" \
    UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
    UU_REMOTE_FAKE_LATEST_VERSION=4.40.0.9500 \
    UU_REMOTE_FAKE_INSTALLER_VERSION=4.40.0.9500 \
        "$launcher" --check-update \
        >"$test_root/runtime-incompatible.out" \
        2>"$test_root/runtime-incompatible.err"; then
    printf '运行时导入契约不兼容的未来版本被意外接受。\n' >&2
    exit 1
fi
grep -q '输入/焦点/画面钩子的导入语义不再兼容' \
    "$test_root/runtime-incompatible.err"
"$fixture_bin/runtime-profiler" verify \
    "$install_dir" 4.39.0.9400 \
    "$XDG_DATA_HOME/uu-remote-for-linux/runtime-compatibility.json" \
    --strict >/dev/null

# Missing optional portrait semantics must no longer make a safe new client
# unusable. The updater commits the version with OpenH264, while the strict
# input/focus/frame runtime contract remains mandatory.
UU_REMOTE_DISABLE_AUTO_HWENCODE=0 \
UU_REMOTE_FAKE_STREAMER_FUTURE_COMPAT=1 \
UU_REMOTE_FAKE_STREAMER_MISSING_PORTRAIT=1 \
UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
UU_REMOTE_FAKE_LATEST_VERSION=4.40.0.9500 \
UU_REMOTE_FAKE_INSTALLER_VERSION=4.40.0.9500 \
    "$launcher" --check-update \
    >"$test_root/future-incompatible.out" \
    2>"$test_root/future-incompatible.err"
grep -q 'OpenH264 安全模式' "$test_root/future-incompatible.err"
"$launcher" --diagnose >"$test_root/future-fallback-diagnose.txt"
grep -q 'UU 版本.*4.40.0.9500' "$test_root/future-fallback-diagnose.txt"
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-disabled"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-enabled"

# A damaged rollback archive must be rejected before the updater moves or
# deletes the currently installed client. The failed new install remains
# intact for diagnosis instead of leaving an empty GameViewer directory.
if UU_REMOTE_DISABLE_AUTO_HWENCODE=0 \
    UU_REMOTE_FAKE_STREAMER_FUTURE_COMPAT=1 \
    UU_REMOTE_FAKE_STREAMER_MISSING_PORTRAIT=1 \
    UU_REMOTE_FAKE_RUNTIME_INCOMPATIBLE=1 \
    UU_REMOTE_FAKE_CORRUPT_ROLLBACK_SNAPSHOT=1 \
    UU_REMOTE_HWDECODE_COMPATIBILITY_PROFILES="$empty_hwdecode_profiles" \
    UU_REMOTE_HWDECODE_PROFILER_BIN="$fixture_bin/hwdecode-profiler" \
    UU_REMOTE_WOL_COMPATIBILITY_PROFILES="$empty_wol_profiles" \
    UU_REMOTE_WOL_PROFILER_BIN="$fixture_bin/wol-profiler" \
    UU_REMOTE_FAKE_LATEST_VERSION=4.41.0.9600 \
    UU_REMOTE_FAKE_INSTALLER_VERSION=4.41.0.9600 \
        "$launcher" --check-update \
        >"$test_root/corrupt-rollback.out" \
        2>"$test_root/corrupt-rollback.err"; then
    printf '损坏回滚归档的更新事务被意外接受。\n' >&2
    exit 1
fi
grep -q '更新回滚快照无法完整解压；当前安装保持不变' \
    "$test_root/corrupt-rollback.err"
test -f "$install_dir/GameViewer.exe"
test -f "$install_dir/bin/GameViewer.exe"
test "$(<"$install_dir/bin/.installed-version")" = 4.41.0.9600

# Missing optional streamer patch points degrade to OpenH264 on both backends.
# Runtime-import failures remain fail-closed because they affect input/focus
# and frame-hook safety rather than an optional codec.
printf 'stale\n' >"$XDG_DATA_HOME/uu-remote-for-linux/hwencode-enabled"
client_count_before_dmabuf_fallback=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_DISABLE_AUTO_HWENCODE=1 \
XDG_SESSION_TYPE=wayland \
WAYLAND_DISPLAY=wayland-test-0 \
    "$launcher" \
    >"$test_root/dmabuf-fallback.out" \
    2>"$test_root/dmabuf-fallback.err"
grep -q '原生 DMA-BUF + OpenH264 安全模式' \
    "$test_root/dmabuf-fallback.err"
test ! -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-enabled"
test -e "$XDG_DATA_HOME/uu-remote-for-linux/hwencode-disabled"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -eq \
    "$((client_count_before_dmabuf_fallback + 1))"

client_count_before_x11_fallback=$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")
UU_REMOTE_DISABLE_AUTO_HWENCODE=1 \
    "$launcher" \
    >"$test_root/x11-openh264-fallback.out" \
    2>"$test_root/x11-openh264-fallback.err"
grep -q 'X11 使用 OpenH264 安全模式' \
    "$test_root/x11-openh264-fallback.err"
test "$(grep -c '^CLIENT$' "$UU_REMOTE_FAKE_TRACE")" -eq \
    "$((client_count_before_x11_fallback + 1))"

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
