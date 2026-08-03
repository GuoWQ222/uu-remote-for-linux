#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge"
readonly hook="$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
readonly injector="$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
readonly shim="$project_root/lib/uu-remote-for-linux/wevtapi.dll"
readonly compiler="${UU_REMOTE_MINGW_CC:-x86_64-w64-mingw32-gcc}"
compile_flags=()

[[ -z ${UU_REMOTE_MINGW_INCLUDE_DIR:-} ]] ||
    compile_flags+=("-isystem" "$UU_REMOTE_MINGW_INCLUDE_DIR")
[[ -z ${UU_REMOTE_MINGW_LIBRARY_DIR:-} ]] ||
    compile_flags+=(
        "-B$UU_REMOTE_MINGW_LIBRARY_DIR"
        "-L$UU_REMOTE_MINGW_LIBRARY_DIR"
    )
[[ -z ${UU_REMOTE_MINGW_RUNTIME_DIR:-} ]] ||
    compile_flags+=(
        "-B$UU_REMOTE_MINGW_RUNTIME_DIR"
        "-L$UU_REMOTE_MINGW_RUNTIME_DIR"
    )

for command_name in wine wineboot wineserver "$compiler"; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'SKIP Wine 显式输入钩子实测（缺少 %s）\n' "$command_name"
        exit 0
    }
done
[[ -n ${DISPLAY:-} ]] || {
    printf 'SKIP Wine 显式输入钩子实测（DISPLAY 未设置）\n'
    exit 0
}
[[ -s $hook ]] || {
    printf 'Wine 显式输入钩子实测前缺少 DLL：%s\n' "$hook" >&2
    exit 1
}
[[ -s $injector ]] || {
    printf 'Wine 输入钩子实测前缺少注入器：%s\n' "$injector" >&2
    exit 1
}

test_root=$(mktemp -d)
bridge_pid=
preloader_pid=
injector_pid=
server_pid=

cleanup() {
    if [[ $bridge_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$bridge_pid" >/dev/null 2>&1; then
        kill "$bridge_pid" >/dev/null 2>&1 || true
        wait "$bridge_pid" 2>/dev/null || true
    fi
    if [[ $preloader_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$preloader_pid" >/dev/null 2>&1; then
        kill "$preloader_pid" >/dev/null 2>&1 || true
        wait "$preloader_pid" 2>/dev/null || true
    fi
    if [[ $injector_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$injector_pid" >/dev/null 2>&1; then
        kill "$injector_pid" >/dev/null 2>&1 || true
        wait "$injector_pid" 2>/dev/null || true
    fi
    if [[ $server_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$server_pid" >/dev/null 2>&1; then
        kill "$server_pid" >/dev/null 2>&1 || true
        wait "$server_pid" 2>/dev/null || true
    fi
    WINEPREFIX="$test_root/prefix" WINEDEBUG=-all wineserver -k \
        >/dev/null 2>&1 || true
    if [[ ${UU_REMOTE_KEEP_WINE_HOOK_TEST:-0} == 1 ]]; then
        printf '保留 Wine 输入钩子测试目录：%s\n' "$test_root" >&2
    else
        rm -rf -- "$test_root"
    fi
}
trap cleanup EXIT

readonly prefix="$test_root/prefix"
readonly state_dir="$test_root/state"
readonly endpoint="$prefix/drive_c/uu-remote-input-bridge.endpoint"
readonly wol_config="$prefix/drive_c/uu-remote-wol-bridge.ini"
readonly log="$test_root/input-bridge.log"
readonly lock="$test_root/input-bridge.lock"
readonly trace="$test_root/xtest.trace"
readonly compiled_probe="$test_root/compiled-GameViewerServer.exe"
readonly compiled_streamer_probe="$test_root/compiled-streamer.dll"
readonly probe="$prefix/drive_c/GameViewerServer.exe"
readonly streamer_probe="$prefix/drive_c/streamer.dll"
readonly wol_status="$prefix/drive_c/uu-remote-wol-hook-status.ini"
readonly frame_file="$prefix/drive_c/uu-remote-wayland-frame.bin"
readonly frame_status="$prefix/drive_c/uu-remote-wayland-frame-status.ini"
mkdir -p "$state_dir"
export WINEDLLOVERRIDES='wevtapi=n'

"$compiler" \
    -std=c11 \
    -Os \
    -Wall \
    -Wextra \
    -Werror \
    -shared \
    -Wl,--no-insert-timestamp \
    "${compile_flags[@]}" \
    -o "$compiled_streamer_probe" \
    "$project_root/tests/fixtures/input-hook-streamer-probe.c" \
    -lgdi32 \
    -luser32
"$compiler" \
    -Os \
    -mwindows \
    -Wl,--no-insert-timestamp \
    "${compile_flags[@]}" \
    -o "$compiled_probe" \
    "$project_root/tests/fixtures/input-hook-probe.c" \
    -lgdi32 \
    -luser32 \
    -liphlpapi \
    -lwevtapi \
    -lws2_32

WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all wineboot -u \
    >/dev/null 2>&1
install -Dm0644 "$hook" "$prefix/drive_c/uu-remote-input-hook.dll"
install -Dm0644 "$shim" "$prefix/drive_c/wevtapi.dll"
install -Dm0644 "$injector" \
    "$prefix/drive_c/uu-remote-input-injector.exe"
install -Dm0755 "$compiled_probe" "$probe"
install -Dm0644 "$compiled_streamer_probe" "$streamer_probe"
printf '%s\n' \
    '[wol]' \
    'enabled=1' \
    'interface=enp0s31f6' \
    'reference_ip=198.18.0.1' \
    'native_ip=10.6.12.133' \
    'gateway=10.6.15.254' \
    'mac=58:11:22:76:19:64' \
    'native_magic=1' \
    'native_power_wakeup=1' \
    >"$wol_config"
/usr/bin/python3 - "$frame_file" <<'PY'
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
width, height = 128, 72
stride = width * 4
size = stride * height
header = struct.pack(
    "<10I24x",
    0x46575555,
    1,
    64,
    width,
    height,
    stride,
    size,
    2,
    1,
    1,
)
frame = bytearray(size)
for y in range(height):
    for x in range(width):
        offset = y * stride + x * 4
        blue = x * 255 // (width - 1)
        green = y * 255 // (height - 1)
        frame[offset : offset + 4] = bytes((blue, green, 255 - blue, 0))
path.write_bytes(header + bytes(size) + frame)
PY

UU_REMOTE_INPUT_BRIDGE_FAKE_TRACE="$trace" XDG_SESSION_TYPE=x11 \
    "$bridge" watch \
    "$state_dir" "$endpoint" "$log" "$lock" 2 &
bridge_pid=$!
for _ in {1..100}; do
    [[ -s $endpoint ]] && break
    sleep 0.02
done
[[ -s $endpoint ]]

WINEPREFIX="$prefix" WINEDEBUG=-all timeout 30s \
    wine 'C:\GameViewerServer.exe' \
    >"$test_root/server.log" 2>&1 &
server_pid=$!
for _ in {1..500}; do
    grep -Fq 'preloaded=1' "$wol_status" 2>/dev/null &&
        grep -Fq 'status_bits=15' "$wol_status" 2>/dev/null &&
        break
    sleep 0.02
done
grep -Fq 'preloaded=1' "$wol_status"
grep -Fq 'status_bits=15' "$wol_status"
WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine 'C:\uu-remote-input-injector.exe' \
    --watch GameViewerServer.exe 'C:\uu-remote-input-hook.dll' \
    >"$test_root/injector.log" 2>&1 &
injector_pid=$!
wait "$server_pid"
server_pid=
for _ in {1..100}; do
    grep -q '"hook_pid": [1-9]' \
        "$state_dir/input-bridge-status.json" 2>/dev/null &&
        grep -q '^flush$' "$trace" 2>/dev/null &&
        grep -Fq 'preloaded=1' "$wol_status" 2>/dev/null &&
        grep -Fq 'status_bits=15' "$wol_status" 2>/dev/null &&
        break
    sleep 0.02
done
grep -q '"hook_pid": [1-9]' "$state_dir/input-bridge-status.json"
grep -q '^motion 32768 16384 1$' "$trace"
grep -q '^flush$' "$trace"
grep -q 'GameViewerServer 输入钩子已连接' "$log"
grep -Fq 'preloaded=1' "$wol_status"
grep -Fq 'status_bits=15' "$wol_status"
grep -A3 -F '[calls]' "$wol_status" | grep -Fq 'addresses=2'
grep -A3 -F '[calls]' "$wol_status" | grep -Fq 'info=2'
grep -A3 -F '[calls]' "$wol_status" | grep -Fq 'if_table2=1'
grep -A3 -F '[patched]' "$wol_status" | grep -Fq 'addresses=1'
grep -A3 -F '[patched]' "$wol_status" | grep -Fq 'info=1'
grep -A3 -F '[patched]' "$wol_status" | grep -Fq 'if_table2=1'
grep -A4 -F '[hook]' "$frame_status" | grep -Fq 'version=13'
grep -A4 -F '[hook]' "$frame_status" | grep -Fq 'status_bits=31'
grep -A4 -F '[capture]' "$frame_status" | grep -Eq 'rendered=[1-9]'

kill "$bridge_pid" 2>/dev/null || true
wait "$bridge_pid" 2>/dev/null || true
bridge_pid=
kill "$injector_pid" 2>/dev/null || true
wait "$injector_pid" 2>/dev/null || true
injector_pid=
printf 'Wine DLL 预加载、共享帧 GDI 截获、输入和 WOL 映射实测通过。\n'
