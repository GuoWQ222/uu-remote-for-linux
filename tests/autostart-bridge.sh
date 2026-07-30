#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uuyc-linux-controller/uuyc-autostart-bridge"

test_root=$(mktemp -d)
watcher_pid=""
cleanup() {
    if [[ -n $watcher_pid ]]; then
        kill "$watcher_pid" >/dev/null 2>&1 || true
        wait "$watcher_pid" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT

readonly registry="$test_root/user.reg"
readonly desktop="$test_root/config/autostart/uuyc-linux-controller.desktop"
readonly launcher="$test_root/launcher with space"
readonly log_file="$test_root/state/autostart-bridge.log"
readonly lock_file="$test_root/state/autostart-bridge.lock"

install -Dm0755 /dev/null "$launcher"

write_registry_off() {
    {
        printf '%s\n' \
            'WINE REGISTRY Version 2' \
            '' \
            '[Software\\Microsoft\\Windows\\CurrentVersion\\Run] 123456789' \
            '#time=1d'
    } >"${registry}.tmp"
    mv -f -- "${registry}.tmp" "$registry"
}

write_registry_on() {
    {
        printf '%s\n' \
            'WINE REGISTRY Version 2' \
            '' \
            '[Software\\Microsoft\\Windows\\CurrentVersion\\Run] 123456789' \
            '#time=1d' \
            '"GameViewer"="C:\\\\Program Files\\\\Netease\\\\GameViewer\\\\bin\\\\GameViewerLauncher.exe"'
    } >"${registry}.tmp"
    mv -f -- "${registry}.tmp" "$registry"
}

wait_for() {
    local description=$1
    shift
    local attempt

    for ((attempt = 0; attempt < 100; attempt++)); do
        if "$@"; then
            return
        fi
        sleep 0.05
    done
    printf '等待超时：%s\n' "$description" >&2
    return 1
}

write_registry_off
if "$bridge" registry-enabled "$registry"; then
    printf '空 Run 键被误判为已启用。\n' >&2
    exit 1
fi
"$bridge" sync "$registry" "$desktop" "$launcher" "$log_file"
test ! -e "$desktop"

write_registry_on
"$bridge" registry-enabled "$registry"
"$bridge" sync "$registry" "$desktop" "$launcher" "$log_file"
"$bridge" native-enabled "$desktop"
grep -Fqx "Exec=\"$launcher\" --autostart" "$desktop"
grep -Fqx 'X-GNOME-Autostart-enabled=true' "$desktop"
grep -Fqx 'X-UUYC-Autostart-Bridge=true' "$desktop"

desktop_hash=$(sha256sum "$desktop")
"$bridge" sync "$registry" "$desktop" "$launcher" "$log_file"
test "$(sha256sum "$desktop")" = "$desktop_hash"

write_registry_off
"$bridge" sync "$registry" "$desktop" "$launcher" "$log_file"
test ! -e "$desktop"

mkdir -p -- "${desktop%/*}"
printf '%s\n' '[Desktop Entry]' 'Name=Unmanaged entry' >"$desktop"
if "$bridge" sync "$registry" "$desktop" "$launcher" "$log_file"; then
    printf '兼容桥意外删除了非本项目管理的自启动文件。\n' >&2
    exit 1
fi
grep -Fqx 'Name=Unmanaged entry' "$desktop"
rm -f -- "$desktop"

live_prefix="$test_root/live-prefix"
live_registry="$live_prefix/user.reg"
live_state="$test_root/live-state"
mkdir -p -- "$live_prefix"
install -Dm0644 /dev/null "$live_prefix/system.reg"
write_registry_off
mv -f -- "$registry" "$live_registry"
printf 'enabled\n' >"$live_state"
UUYC_WINE_BIN="$project_root/tests/fixtures/bin/autostart-wine" \
UUYC_AUTOSTART_LIVE_STATE="$live_state" \
UUYC_AUTOSTART_ASSUME_WINESERVER_RUNNING=1 \
    "$bridge" sync "$live_registry" "$desktop" "$launcher" "$log_file"
"$bridge" native-enabled "$desktop"
printf 'disabled\n' >"$live_state"
UUYC_WINE_BIN="$project_root/tests/fixtures/bin/autostart-wine" \
UUYC_AUTOSTART_LIVE_STATE="$live_state" \
UUYC_AUTOSTART_ASSUME_WINESERVER_RUNNING=1 \
    "$bridge" sync "$live_registry" "$desktop" "$launcher" "$log_file"
test ! -e "$desktop"

printf 'enabled\n' >"$live_state"
UUYC_WINE_BIN="$project_root/tests/fixtures/bin/autostart-wine" \
UUYC_AUTOSTART_LIVE_STATE="$live_state" \
UUYC_AUTOSTART_ASSUME_WINESERVER_RUNNING=1 \
    "$bridge" watch \
    "$live_registry" "$desktop" "$launcher" "$log_file" \
    "$test_root/state/live-autostart-bridge.lock" 0.05 &
watcher_pid=$!
wait_for "实时注册表监视器创建自启动项" test -f "$desktop"
printf 'disabled\n' >"$live_state"
wait_for "实时注册表监视器删除自启动项" test ! -e "$desktop"
kill "$watcher_pid"
wait "$watcher_pid" >/dev/null 2>&1 || true
watcher_pid=""

write_registry_off
"$bridge" watch \
    "$registry" "$desktop" "$launcher" "$log_file" "$lock_file" 0.05 &
watcher_pid=$!

write_registry_on
wait_for "监视器创建自启动项" test -f "$desktop"
"$bridge" native-enabled "$desktop"

write_registry_off
wait_for "监视器删除自启动项" test ! -e "$desktop"

rm -f -- "$launcher"
wait_for "启动器删除后监视器退出" bash -c '! kill -0 "$1" 2>/dev/null' _ \
    "$watcher_pid"
wait "$watcher_pid"
watcher_pid=""

grep -q '已启用 Linux 登录自启动' "$log_file"
grep -q '已关闭 Linux 登录自启动' "$log_file"
printf '自启动兼容桥检查通过。\n'
