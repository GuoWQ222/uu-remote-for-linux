#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uuyc-linux-controller/uuyc-sleep-bridge"
readonly fake_inhibit="$project_root/tests/fixtures/bin/sleep-systemd-inhibit"

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

readonly log_dir="$test_root/client/Log"
readonly prefix="$test_root/wineprefix"
readonly launcher="$test_root/launcher"
readonly bridge_log="$test_root/state/sleep-bridge.log"
readonly lock_file="$test_root/state/sleep-bridge.lock"
readonly inhibit_trace="$test_root/state/inhibit.trace"
mkdir -p -- "$log_dir" "$prefix" "${inhibit_trace%/*}"
install -Dm0755 /dev/null "$launcher"

write_log() {
    local file=$1 state=$2

    printf '%s\n' \
        "[Client.NewUI] [NewUi::HomePageMainWindow::preventSleep] prevent sleep connected_:0 state:$state execution_state:0" \
        >"$file"
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

trace_has() {
    local pattern=$1

    [[ -r $inhibit_trace ]] && grep -q "$pattern" "$inhibit_trace"
}

test "$("$bridge" state "$log_dir")" = unknown
write_log "$log_dir/log_1.txt" 2147483648
test "$("$bridge" state "$log_dir")" = disabled
printf '%s\n' \
    '[NewUi::HomePageMainWindow::preventSleep] prevent sleep connected_:0 state:2147483649 execution_state:0' \
    >>"$log_dir/log_1.txt"
test "$("$bridge" state "$log_dir")" = enabled
printf '%s\n' \
    '[NewUi::HomePageMainWindow::preventSleep] prevent sleep connected_:1 state:2147483651 execution_state:0' \
    >>"$log_dir/log_1.txt"
test "$("$bridge" state "$log_dir")" = enabled
sleep 0.02
printf 'new log without state\n' >"$log_dir/log_2.txt"
test "$("$bridge" state "$log_dir")" = unknown

write_log "$log_dir/log_2.txt" 2147483648
UUYC_SYSTEMD_INHIBIT_BIN="$fake_inhibit" \
UUYC_SLEEP_INHIBIT_TRACE="$inhibit_trace" \
UUYC_SLEEP_ASSUME_WINESERVER_RUNNING=1 \
    "$bridge" watch \
    "$log_dir" "$prefix" "$launcher" "$bridge_log" "$lock_file" 0.05 &
watcher_pid=$!
sleep 0.15
test ! -s "$inhibit_trace"

write_log "$log_dir/log_2.txt" 2147483649
wait_for "开启时创建休眠抑制器" trace_has '^ACQUIRE '

write_log "$log_dir/log_2.txt" 2147483648
wait_for "关闭时释放休眠抑制器" trace_has '^RELEASE '

write_log "$log_dir/log_2.txt" 2147483651
# The nested shell intentionally expands its positional argument.
# shellcheck disable=SC2016
wait_for "远控连接时重新创建休眠抑制器" \
    bash -c 'test "$(grep -c "^ACQUIRE " "$1")" -eq 2' _ "$inhibit_trace"

kill "$watcher_pid"
wait "$watcher_pid"
watcher_pid=""
# The nested shell intentionally expands its positional argument.
# shellcheck disable=SC2016
wait_for "监视器退出时释放休眠抑制器" \
    bash -c 'test "$(grep -c "^RELEASE " "$1")" -eq 2' _ "$inhibit_trace"

grep -q '已启用 Linux sleep:idle 抑制器' "$bridge_log"
grep -q '已释放 Linux sleep:idle 抑制器' "$bridge_log"
printf '防休眠兼容桥检查通过。\n'
