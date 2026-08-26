#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge"

test_root=$(mktemp -d)
declare -a watcher_pids=()
cleanup() {
    local pid

    for pid in "${watcher_pids[@]}"; do
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    done
    rm -rf -- "$test_root"
}
trap cleanup EXIT

readonly setting="$test_root/setting.ini"
readonly policy_lock="$test_root/client-update-policy.lock"
readonly watcher_lock="$test_root/update-bridge.lock"
readonly bridge_log="$test_root/update-bridge.log"
readonly trace="$test_root/trace"
readonly scenario="$test_root/scenario"
readonly launcher="$test_root/launcher"
readonly update_marker="$test_root/update-installed"

write_setting_enabled() {
    printf '[settingCenter]\nAutoUpdate=1\nCloseOption=1\n' >"$setting"
}

write_launcher() {
    {
        printf '#!/usr/bin/env bash\n'
        printf 'set -euo pipefail\n'
        # The generated launcher must evaluate these expressions, not this
        # fixture generator.
        # shellcheck disable=SC2016
        printf 'printf "check-start %%s\\n" "$(date +%%s%%N)" >>%q\n' "$trace"
        # shellcheck disable=SC2016
        printf 'case "$(<%q)" in\n' "$scenario"
        # shellcheck disable=SC2016
        printf '  offline) rm -f -- "$0"; exit 7 ;;\n'
        # shellcheck disable=SC2016
        printf '  slow-dns) rm -f -- "$0"; sleep 2; exit 28 ;;\n'
        # shellcheck disable=SC2016
        printf '  update) touch %q; rm -f -- "$0"; exit 0 ;;\n' \
            "$update_marker"
        # shellcheck disable=SC2016
        printf '  concurrent) sleep 0.3; printf "check-done\\n" >>%q; rm -f -- "$0" ;;\n' \
            "$trace"
        printf '  persistent) exit 0 ;;\n'
        printf '  *) exit 2 ;;\n'
        printf 'esac\n'
    } >"$launcher"
    chmod 0755 "$launcher"
}

wait_for_trace() {
    local attempt

    for ((attempt = 0; attempt < 100; attempt++)); do
        [[ -s $trace ]] && return 0
        sleep 0.02
    done
    return 1
}

run_failure_case() {
    local name=$1 start elapsed watcher_pid

    : >"$trace"
    printf '%s\n' "$name" >"$scenario"
    write_setting_enabled
    write_launcher
    "$bridge" watch \
        "$setting" "$launcher" "$bridge_log" "$watcher_lock" 60 &
    watcher_pid=$!
    watcher_pids+=("$watcher_pid")
    wait_for_trace

    # A blocked background resolver must not share the foreground policy lock.
    start=$(date +%s%N)
    "$bridge" defer-client-check "$setting" "$policy_lock"
    elapsed=$((($(date +%s%N) - start) / 1000000))
    ((elapsed < 750)) || {
        printf '%s 故障使前台移交阻塞了 %s ms。\n' \
            "$name" "$elapsed" >&2
        return 1
    }
    if "$bridge" setting-enabled "$setting"; then
        printf '%s 故障后官方前台检查仍为启用。\n' "$name" >&2
        return 1
    fi
    wait "$watcher_pid" || true
}

run_failure_case offline
run_failure_case slow-dns

# Changing the upstream preference is part of the foreground handoff.  It must
# not wake a previously sleeping watcher and race the visible client launch.
: >"$trace"
printf 'persistent\n' >"$scenario"
write_setting_enabled
write_launcher
"$bridge" watch \
    "$setting" "$launcher" "$bridge_log" "$watcher_lock" 3 &
persistent_pid=$!
watcher_pids+=("$persistent_pid")
wait_for_trace
test "$(grep -c '^check-start ' "$trace")" -eq 1
"$bridge" defer-client-check "$setting" "$policy_lock"
sleep 0.5
test "$(grep -c '^check-start ' "$trace")" -eq 1
kill "$persistent_pid"
wait "$persistent_pid" >/dev/null 2>&1 || true

# An available update is still handed to the Linux updater, but only after
# the graphical grace period.  No foreground network request is needed.
: >"$trace"
rm -f -- "$update_marker"
printf 'update\n' >"$scenario"
write_setting_enabled
write_launcher
"$bridge" defer-client-check "$setting" "$policy_lock"
"$bridge" watch \
    "$setting" "$launcher" "$bridge_log" "$watcher_lock" 60 \
    "" "" 1.1.42 2 &
update_pid=$!
watcher_pids+=("$update_pid")
sleep 0.3
test ! -s "$trace"
test ! -e "$update_marker"
wait "$update_pid"
test -e "$update_marker"
grep -q '^check-start ' "$trace"

# A login storm may dispatch many launchers, but the watcher lock permits only
# one network/update transaction.  The policy rewrite has its own lock and
# remains safe in parallel.
: >"$trace"
printf 'concurrent\n' >"$scenario"
write_setting_enabled
write_launcher
declare -a concurrent_pids=()
for _attempt in {1..32}; do
    "$bridge" watch \
        "$setting" "$launcher" "$bridge_log" "$watcher_lock" 60 &
    concurrent_pids+=("$!")
    "$bridge" defer-client-check "$setting" "$policy_lock" &
    concurrent_pids+=("$!")
done
for concurrent_pid in "${concurrent_pids[@]}"; do
    wait "$concurrent_pid"
done
test "$(grep -c '^check-start ' "$trace")" -eq 1
test "$(grep -c '^check-done$' "$trace")" -eq 1
if "$bridge" setting-enabled "$setting"; then
    printf '并发启动压力测试后官方前台检查仍为启用。\n' >&2
    exit 1
fi

printf '异步更新启动故障与并发压力检查通过。\n'
