#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge"
readonly hook="$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
readonly injector="$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
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

cleanup() {
    if [[ $bridge_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$bridge_pid" >/dev/null 2>&1; then
        kill "$bridge_pid" >/dev/null 2>&1 || true
        wait "$bridge_pid" 2>/dev/null || true
    fi
    WINEPREFIX="$test_root/prefix" WINEDEBUG=-all wineserver -k \
        >/dev/null 2>&1 || true
    rm -rf -- "$test_root"
}
trap cleanup EXIT

readonly prefix="$test_root/prefix"
readonly state_dir="$test_root/state"
readonly endpoint="$prefix/drive_c/uu-remote-input-bridge.endpoint"
readonly log="$test_root/input-bridge.log"
readonly lock="$test_root/input-bridge.lock"
readonly trace="$test_root/xtest.trace"
readonly probe="$test_root/GameViewerServer.exe"
readonly streamer_probe="$test_root/streamer.dll"
mkdir -p "$state_dir"

"$compiler" \
    -std=c11 \
    -Os \
    -Wall \
    -Wextra \
    -Werror \
    -shared \
    -Wl,--no-insert-timestamp \
    "${compile_flags[@]}" \
    -o "$streamer_probe" \
    "$project_root/tests/fixtures/input-hook-streamer-probe.c" \
    -lgdi32 \
    -luser32
"$compiler" \
    -Os \
    -mwindows \
    -Wl,--no-insert-timestamp \
    "${compile_flags[@]}" \
    -o "$probe" \
    "$project_root/tests/fixtures/input-hook-probe.c" \
    -luser32

WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all wineboot -u \
    >/dev/null 2>&1
install -Dm0644 "$hook" "$prefix/drive_c/uu-remote-input-hook.dll"
install -Dm0644 "$injector" \
    "$prefix/drive_c/uu-remote-input-injector.exe"

UU_REMOTE_INPUT_BRIDGE_FAKE_TRACE="$trace" XDG_SESSION_TYPE=x11 \
    "$bridge" watch \
    "$state_dir" "$endpoint" "$log" "$lock" 2 &
bridge_pid=$!
for _ in {1..100}; do
    [[ -s $endpoint ]] && break
    sleep 0.02
done
[[ -s $endpoint ]]

WINEPREFIX="$prefix" WINEDEBUG=-all \
    wine 'C:\uu-remote-input-injector.exe' \
    --watch GameViewerServer.exe 'C:\uu-remote-input-hook.dll' \
    >"$test_root/injector.log" 2>&1 &
WINEPREFIX="$prefix" WINEDEBUG=-all timeout 20s wine "$probe"
for _ in {1..100}; do
    grep -q '"hook_pid": [1-9]' \
        "$state_dir/input-bridge-status.json" 2>/dev/null &&
        grep -q '^flush$' "$trace" 2>/dev/null &&
        break
    sleep 0.02
done
grep -q '"hook_pid": [1-9]' "$state_dir/input-bridge-status.json"
grep -q '^motion 32768 16384 1$' "$trace"
[[ $(grep -c '^motion 32768 16384 1$' "$trace") -ge 2 ]]
grep -q '^flush$' "$trace"
grep -q 'GameViewerServer 输入钩子已连接' "$log"

kill "$bridge_pid"
wait "$bridge_pid"
bridge_pid=
printf 'Wine 显式 DLL 注入和 SendInput IAT 截获实测通过。\n'
