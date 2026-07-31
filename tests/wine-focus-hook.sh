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

WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all wineboot -u \
    >/dev/null 2>&1
(
    cd "$bin_dir"
    WINEPREFIX="$prefix" WINEDEBUG=-all timeout 20s \
        wine 'C:\probe\bin\GameViewer.exe'
)
grep -A3 -F '[hook]' "$status" | grep -Fq 'version=8'
grep -A3 -F '[hook]' "$status" | grep -Fq 'status_bits=31'
grep -A4 -F '[focus]' "$status" | grep -Eq 'suppressed_activate=[1-9]'
grep -A4 -F '[focus]' "$status" | grep -Eq 'suppressed_activate_app=[1-9]'
grep -A4 -F '[focus]' "$status" | grep -Eq 'stabilized_nonclient=[1-9]'
grep -A4 -F '[keyboard_hook]' "$status" | grep -Eq 'reused=[1-9]'
grep -A4 -F '[keyboard_hook]' "$status" | grep -Eq 'released=[1-9]'

printf 'Wine 主控同进程焦点归组、边框稳定和键盘钩子去抖实测通过。\n'
