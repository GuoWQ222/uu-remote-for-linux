#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly hook="$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
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
        printf 'SKIP Wine 主控焦点稳定器实测（缺少 %s）\n' "$command_name"
        exit 0
    }
done
[[ -n ${DISPLAY:-} ]] || {
    printf 'SKIP Wine 主控焦点稳定器实测（DISPLAY 未设置）\n'
    exit 0
}
[[ -s $hook ]] || {
    printf 'Wine 主控焦点稳定器实测前缺少 DLL：%s\n' "$hook" >&2
    exit 1
}

test_root=$(mktemp -d)
cleanup() {
    WINEPREFIX="$test_root/prefix" WINEDEBUG=-all wineserver -k \
        >/dev/null 2>&1 || true
    WINEPREFIX="$test_root/prefix" WINEDEBUG=-all timeout 10s \
        wineserver -w >/dev/null 2>&1 || true
    if [[ ${UU_REMOTE_KEEP_FOCUS_HOOK_TEST:-0} == 1 ]]; then
        printf '保留 Wine 焦点测试目录：%s\n' "$test_root" >&2
    else
        rm -rf -- "$test_root"
    fi
}
trap cleanup EXIT

readonly prefix="$test_root/prefix"
readonly bin_dir="$prefix/drive_c/probe/bin"
readonly status="$prefix/drive_c/uu-remote-focus-hook-status.ini"
mkdir -p "$bin_dir"

"$compiler" \
    -std=c11 -Os -Wall -Wextra -Werror -shared \
    -Wl,--no-insert-timestamp \
    "${compile_flags[@]}" \
    -o "$bin_dir/Qt5Core.dll" \
    "$project_root/tests/fixtures/focus-hook-qtcore-probe.c" \
    -luser32
"$compiler" \
    -std=c11 -Os -Wall -Wextra -Werror -mwindows \
    -Wl,--no-insert-timestamp \
    "${compile_flags[@]}" \
    -o "$bin_dir/GameViewer.exe" \
    "$project_root/tests/fixtures/focus-hook-probe.c" \
    -luser32
install -Dm0644 "$hook" "$bin_dir/uu-remote-input-hook.dll"

probe_status=0
for attempt in 1 2; do
    WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all wineboot -u \
        >/dev/null 2>&1
    if (
        cd "$bin_dir"
        WINEPREFIX="$prefix" WINEDEBUG=-all timeout 60s \
            wine 'C:\probe\bin\GameViewer.exe'
    ); then
        probe_status=0
        break
    else
        probe_status=$?
    fi
    if (( probe_status != 124 || attempt == 2 )); then
        exit "$probe_status"
    fi
    printf '%s\n' \
        'Wine 焦点探针启动超时；清理临时前缀后重试一次。' >&2
    WINEPREFIX="$prefix" WINEDEBUG=-all wineserver -k \
        >/dev/null 2>&1 || true
    WINEPREFIX="$prefix" WINEDEBUG=-all timeout 10s \
        wineserver -w >/dev/null 2>&1 || true
done
(( probe_status == 0 )) || exit "$probe_status"
grep -A3 -F '[hook]' "$status" | grep -Fq 'version=28'
grep -A3 -F '[hook]' "$status" | grep -Fq 'status_bits=1023'
grep -A4 -F '[focus]' "$status" | grep -Eq 'suppressed_activate=[1-9]'
grep -A4 -F '[focus]' "$status" | grep -Eq 'suppressed_activate_app=[1-9]'
grep -A4 -F '[focus]' "$status" | grep -Eq 'stabilized_nonclient=[1-9]'
grep -A4 -F '[keyboard_hook]' "$status" | grep -Eq 'reused=[1-9]'
grep -A4 -F '[keyboard_hook]' "$status" | grep -Eq 'released=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Fq 'mode=wndproc-arbiter'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'subclassed_total=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'external_chains=[1-9]'
grep -A12 -F '[window_state]' "$status" |
    grep -Eq 'nonclient_right_clicks_suppressed=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'storms_detected=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'storms_resolved=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'blocked_activations=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'modal_latches=[1-9]'
grep -A12 -F '[window_state]' "$status" | grep -Eq 'post_modal_handoffs=[1-9]'
grep -A8 -F '[focus]' "$status" | grep -Eq 'apply_posted=[0-9]+'
grep -A8 -F '[focus]' "$status" | grep -Eq 'apply_rate_limited=[0-9]+'
grep -A4 -F '[home_window]' "$status" | grep -Eq 'reopen_blocked=[1-9]'
grep -A4 -F '[home_window]' "$status" | grep -Eq 'show_authorized=[1-9]'
grep -A7 -F '[event_loop]' "$status" |
    grep -Fq 'mode=qt-wine-sticky-message-guard'
grep -A5 -F '[ui_health]' "$status" | grep -Eq 'pings_sent=[1-9]'
grep -A5 -F '[ui_health]' "$status" | grep -Eq 'pings_acked=[1-9]'
grep -A9 -F '[ui_health]' "$status" |
    grep -Eq 'no_livelock_suppressions=[1-9]'
grep -A13 -F '[ui_health]' "$status" |
    grep -Eq 'consecutive_timeouts=[2-9]|consecutive_timeouts=[1-9][0-9]+'
grep -A13 -F '[ui_health]' "$status" |
    grep -Eq 'hard_stalls_detected=[1-9]'
grep -A2 -F '[worker]' "$status" | grep -Eq 'heartbeats=[2-9]|heartbeats=[1-9][0-9]+'

printf 'Wine 主控隐藏状态、焦点反馈限速、模态切换和键盘钩子去抖实测通过。\n'
