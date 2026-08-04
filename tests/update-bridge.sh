#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge"

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

readonly setting="$test_root/setting.ini"
readonly launcher="$test_root/launcher"
readonly trace="$test_root/checks.log"
readonly log_file="$test_root/state/update-bridge.log"
readonly lock_file="$test_root/state/update-bridge.lock"
readonly request_file="$test_root/prefix/drive_c/uu-remote-upstream-update.request"
readonly processing_file="$test_root/prefix/drive_c/uu-remote-upstream-update.processing"
readonly status_file="$test_root/state/update-bridge-status"

write_setting() {
    local value=$1
    {
        printf '[other]\nAutoUpdate=1\n'
        printf '[settingCenter]\nAutoUpdate=%s\n' "$value"
    } >"${setting}.tmp"
    mv -f -- "${setting}.tmp" "$setting"
}

write_setting 0
if "$bridge" setting-enabled "$setting"; then
    printf 'AutoUpdate=0 被误判为已启用。\n' >&2
    exit 1
fi

write_setting true
"$bridge" setting-enabled "$setting"

{
    printf '#!/usr/bin/env bash\n'
    printf 'printf "%%s\\n" "$*" >>%q\n' "$trace"
    # Generate a launcher that removes itself at runtime.
    # shellcheck disable=SC2016
    printf 'rm -f -- "$0"\n'
} >"$launcher"
chmod 0755 "$launcher"

"$bridge" watch \
    "$setting" "$launcher" "$log_file" "$lock_file" 1 &
watcher_pid=$!
wait "$watcher_pid"
watcher_pid=""

test "$(wc -l <"$trace")" -eq 1
grep -Fxq -- '--auto-update-check' "$trace"
grep -q 'UU 自动更新开关：enabled' "$log_file"

write_setting 0
{
    printf '#!/usr/bin/env bash\n'
    printf 'printf "unexpected\\n" >>%q\n' "$trace"
} >"$launcher"
chmod 0755 "$launcher"
nohup "$bridge" watch \
    "$setting" "$launcher" "$log_file" "$lock_file" 1 &
watcher_pid=$!
sleep 0.2
kill -HUP "$watcher_pid"
sleep 0.2
kill -0 "$watcher_pid"
sleep 1
kill "$watcher_pid"
wait "$watcher_pid" >/dev/null 2>&1 || true
watcher_pid=""
test "$(wc -l <"$trace")" -eq 1
grep -q 'UU 自动更新开关：disabled' "$log_file"

{
    printf '#!/usr/bin/env bash\n'
    printf 'printf "%%s\\n" "$*" >>%q\n' "$trace"
} >"$launcher"
chmod 0755 "$launcher"
mkdir -p -- "${request_file%/*}"
"$bridge" watch \
    "$setting" "$launcher" "$log_file" "$lock_file" 30 "$request_file" \
    "$status_file" 1.1.15 &
watcher_pid=$!
sleep 0.2
: >"$request_file"
for _attempt in {1..50}; do
    grep -Fxq -- '--upstream-update-handoff' "$trace" && break
    sleep 0.1
done
grep -Fxq -- '--upstream-update-handoff' "$trace"
test ! -e "$request_file"
test ! -e "$processing_file"
grep -q '已接管 UU 内置更新请求' "$log_file"
grep -Fxq 'status=active' "$status_file"
grep -Fxq 'package_version=1.1.15' "$status_file"
kill "$watcher_pid"
wait "$watcher_pid" >/dev/null 2>&1 || true
watcher_pid=""

printf '自动更新兼容桥检查通过。\n'
