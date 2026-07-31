#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge"
readonly fake_gsettings="$project_root/tests/fixtures/bin/keyboard-gsettings"

test_root=$(mktemp -d)
watcher_pid=

cleanup() {
    if [[ $watcher_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$watcher_pid" >/dev/null 2>&1; then
        kill "$watcher_pid" >/dev/null 2>&1 || true
        wait "$watcher_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT

readonly settings_root="$test_root/settings"
readonly state_dir="$test_root/state"
readonly log="$test_root/keyboard.log"
readonly lock="$test_root/keyboard.lock"
readonly focus="$test_root/focus"
mkdir -p "$settings_root" "$state_dir"

export UU_REMOTE_KEYBOARD_BRIDGE_GSETTINGS_BIN="$fake_gsettings"
export UU_REMOTE_KEYBOARD_BRIDGE_FAKE_FOCUS_FILE="$focus"
export UU_REMOTE_KEYBOARD_GSETTINGS_ROOT="$settings_root"
export XDG_SESSION_TYPE=x11

reset_settings() {
    printf "%s\n" "['<Super>space', 'XF86Keyboard']" \
        >"$settings_root/switch"
    printf "%s\n" "['<Shift><Super>space', '<Shift>XF86Keyboard']" \
        >"$settings_root/switch_backward"
    printf "%s\n" "['<Super>space']" >"$settings_root/ibus_triggers"
    printf "%s\n" "'Super_L'" >"$settings_root/overlay"
    printf '%s\n' local >"$focus"
}

wait_for_value() {
    local path=$1 expected=$2 attempt
    for ((attempt = 0; attempt < 100; attempt++)); do
        if [[ -r $path ]] && [[ $(<"$path") == "$expected" ]]; then
            return 0
        fi
        sleep 0.02
    done
    printf '等待状态超时：%s != %s\n' "$path" "$expected" >&2
    return 1
}

assert_originals() {
    [[ $(<"$settings_root/switch") == \
        "['<Super>space', 'XF86Keyboard']" ]]
    [[ $(<"$settings_root/switch_backward") == \
        "['<Shift><Super>space', '<Shift>XF86Keyboard']" ]]
    [[ $(<"$settings_root/ibus_triggers") == "['<Super>space']" ]]
    [[ $(<"$settings_root/overlay") == "'Super_L'" ]]
}

start_watcher() {
    "$bridge" watch "$state_dir" "$log" "$lock" 0.05 &
    watcher_pid=$!
}

reset_settings
"$bridge" check
start_watcher
for _ in {1..100}; do
    [[ $("$bridge" status "$state_dir" 2>/dev/null || true) == idle ]] && break
    sleep 0.02
done
[[ $("$bridge" status "$state_dir") == idle ]]
assert_originals

printf '%s\n' remote >"$focus"
wait_for_value "$settings_root/switch" "['XF86Keyboard']"
wait_for_value "$settings_root/switch_backward" "['<Shift>XF86Keyboard']"
wait_for_value "$settings_root/ibus_triggers" "[]"
wait_for_value "$settings_root/overlay" "''"
[[ -s $state_dir/keyboard-bridge-restore.json ]]
[[ $("$bridge" status "$state_dir") == active ]]

printf '%s\n' local >"$focus"
wait_for_value "$settings_root/switch" \
    "['<Super>space', 'XF86Keyboard']"
wait_for_value "$settings_root/switch_backward" \
    "['<Shift><Super>space', '<Shift>XF86Keyboard']"
wait_for_value "$settings_root/ibus_triggers" "['<Super>space']"
wait_for_value "$settings_root/overlay" "'Super_L'"
[[ ! -e $state_dir/keyboard-bridge-restore.json ]]

printf '%s\n' remote >"$focus"
wait_for_value "$settings_root/switch" "['XF86Keyboard']"
kill "$watcher_pid"
wait "$watcher_pid"
watcher_pid=
assert_originals
[[ ! -e $state_dir/keyboard-bridge-restore.json ]]
[[ $("$bridge" status "$state_dir") == stopped ]]

# SIGKILL cannot run a finally block.  The recovery journal must repair the
# exact original values when the bridge is next started or explicitly restored.
reset_settings
start_watcher
printf '%s\n' remote >"$focus"
wait_for_value "$settings_root/switch" "['XF86Keyboard']"
kill -KILL "$watcher_pid"
wait "$watcher_pid" 2>/dev/null || true
watcher_pid=
[[ -s $state_dir/keyboard-bridge-restore.json ]]
[[ $(<"$settings_root/switch") == "['XF86Keyboard']" ]]
"$bridge" restore "$state_dir" "$log"
assert_originals
[[ ! -e $state_dir/keyboard-bridge-restore.json ]]

grep -q '已临时释放 Linux Super+Space' "$log"
grep -q '已恢复 Linux 原快捷键' "$log"
printf '键盘桥聚焦、恢复和崩溃恢复检查通过。\n'
