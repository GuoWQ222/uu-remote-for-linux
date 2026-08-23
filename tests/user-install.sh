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

[[ -x $installed_profiler ]]
cmp -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-runtime-profiler" \
    "$installed_profiler"
"$installed_profiler" --help >/dev/null 2>&1 && {
    printf '运行时 profiler 对无效调用意外返回成功。\n' >&2
    exit 1
}
[[ -x $test_root/prefix/bin/uu-remote-for-linux ]]
printf '源码用户安装运行时兼容组件检查通过。\n'
