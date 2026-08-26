#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

UU_REMOTE_USER_INSTALL_PREFIX="$test_root/prefix" \
    "$project_root/scripts/install-user.sh" >/dev/null

readonly installed_runtime="$test_root/prefix/lib/uu-remote-for-linux"
readonly installed_profiler="$installed_runtime/uu-remote-runtime-profiler"
readonly installed_virtual_display="$installed_runtime/uu-remote-x11-virtual-display"
readonly installed_second_screen_bridge="$installed_runtime/uu-remote-second-screen-bridge"
readonly installed_virtual_display_daemon="$installed_runtime/uu-remote-virtual-displayd"
readonly installed_virtual_display_control="$test_root/prefix/bin/uu-remote-virtual-displayctl"
readonly installed_virtual_display_unit="$test_root/prefix/share/systemd/user/uu-remote-virtual-display.service"

[[ -x $installed_profiler ]]
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-runtime-profiler" \
    "$installed_profiler"
"$installed_profiler" --help >/dev/null 2>&1 && {
    printf '运行时 profiler 对无效调用意外返回成功。\n' >&2
    exit 1
}
[[ -x $test_root/prefix/bin/uu-remote-for-linux ]]
[[ -x $installed_virtual_display ]]
[[ -x $installed_second_screen_bridge ]]
"$installed_virtual_display" self-test >/dev/null
[[ -x $installed_virtual_display_daemon ]]
"$installed_virtual_display_daemon" --self-test >/dev/null
[[ -x $installed_virtual_display_control ]]
[[ -r $installed_virtual_display_unit ]]
grep -Fq "ExecStart=$installed_runtime/uu-remote-virtual-displayd" \
    "$installed_virtual_display_unit"
grep -Fq "ExecStopPost=-$installed_runtime/uu-remote-x11-virtual-display disable" \
    "$installed_virtual_display_unit"
printf '源码用户安装运行时兼容组件检查通过。\n'
