#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly daemon="$project_root/lib/uu-remote-for-linux/uu-remote-virtual-displayd"
readonly control="$project_root/bin/uu-remote-virtual-displayctl"
readonly manager="$project_root/tests/fixtures/virtual-display-manager"
test_root=$(mktemp -d)
daemon_pid=
cleanup() {
    if [[ ${daemon_pid:-} =~ ^[0-9]+$ ]]; then
        kill "$daemon_pid" >/dev/null 2>&1 || true
        wait "$daemon_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT

mkdir -p "$test_root/runtime" "$test_root/state"
printf '%s\n' \
    '{"active": true, "mode": "1920x1080", "rate": "60", "rotation": "normal", "projection": "extend"}' \
    >"$test_root/backend.json"

export XDG_SESSION_TYPE=x11
export DISPLAY=:99
export UU_REMOTE_VDISPLAYD_RUNTIME_DIR="$test_root/runtime"
export UU_REMOTE_VDISPLAYD_STATE_DIR="$test_root/state"
export UU_REMOTE_VDISPLAYD_SOCKET="$test_root/runtime/virtual-display.sock"
export UU_REMOTE_VDISPLAYD_MANAGER="$manager"
export UU_REMOTE_VDISPLAYD_MONITOR_INTERVAL=0.1
export UU_REMOTE_FAKE_VDISPLAY_STATE="$test_root/backend.json"
export UU_REMOTE_FAKE_VDISPLAY_DELAY=0.04
export UU_REMOTE_X11_VDISPLAY_SYSTEM_EDID="$test_root/fake-edid.bin"

"$daemon" --self-test | grep -Fq '"result": "pass"'
mkdir -p "$test_root/unit-lib"
install -m0755 "$daemon" "$test_root/unit-lib/uu-remote-virtual-displayd"
install -m0755 \
    "$project_root/lib/uu-remote-for-linux/uu-remote-x11-virtual-display" \
    "$test_root/unit-lib/uu-remote-x11-virtual-display"
sed -e "s|@LIBDIR@|$test_root/unit-lib|g" \
    "$project_root/share/systemd/user/uu-remote-virtual-display.service.in" > \
    "$test_root/uu-remote-virtual-display.service"
systemd-analyze --user verify "$test_root/uu-remote-virtual-display.service"

start_daemon() {
    "$daemon" >"$test_root/daemon.stdout" 2>"$test_root/daemon.stderr" &
    daemon_pid=$!
    for _ in $(seq 1 100); do
        [[ -S $test_root/runtime/virtual-display.sock ]] && return 0
        kill -0 "$daemon_pid" 2>/dev/null || return 1
        sleep 0.02
    done
    return 1
}

start_daemon
[[ $(stat -c %a "$test_root/runtime/virtual-display.sock") == 600 ]]
"$control" status >"$test_root/status-adopted.json"
grep -Fq '"state": "active"' "$test_root/status-adopted.json"
grep -Eq '"display_id": "[0-9a-f-]{36}"' "$test_root/status-adopted.json"

"$control" remove >"$test_root/removed.json"
grep -Fq '"state": "off"' "$test_root/removed.json"
grep -Fq '"active": false' "$test_root/backend.json"
if "$control" update --mode 1280x720 >/dev/null 2>"$test_root/update-off.err"; then
    printf '关闭状态下 update 意外成功。\n' >&2
    exit 1
fi
grep -Fq '尚未创建' "$test_root/update-off.err"

"$control" create --mode 2560x1440 --rotation left \
    >"$test_root/created.json"
grep -Fq '"state": "active"' "$test_root/created.json"
grep -Fq '"mode": "2560x1440"' "$test_root/created.json"
grep -Fq '"rotation": "left"' "$test_root/created.json"
test -s "$test_root/state/virtual-displayd-layout.json"

# UU owns the active Windows topology.  A physical-coordinate change while
# the virtual output remains healthy must not trigger a competing xrandr
# recovery; REMOVE then restores the exact pre-create physical layout.
sed -i 's/"physical_drift": false/"physical_drift": true/' \
    "$test_root/backend.json"
sleep 0.35
grep -Fq '"physical_drift": true' "$test_root/backend.json"
"$control" remove >"$test_root/layout-restored.json"
grep -Fq '"physical_drift": false' "$test_root/backend.json"
grep -Fq 'restore-layout' "$test_root/backend.operations.log"
test ! -e "$test_root/state/virtual-displayd-layout.json"
"$control" create --mode 2560x1440 --rotation left \
    >"$test_root/recreated.json"

client_pids=()
for _ in $(seq 1 24); do
    "$control" status >/dev/null &
    client_pids+=("$!")
done
for client_pid in "${client_pids[@]}"; do
    wait "$client_pid"
done
if grep -Fq 'overlapping backend operation' "$test_root/daemon.stderr"; then
    printf '守护进程没有串行化后端操作。\n' >&2
    exit 1
fi

if "$control" create --mode 9999x9999 >/dev/null 2>"$test_root/invalid.err"; then
    printf '不支持的分辨率意外成功。\n' >&2
    exit 1
fi
grep -Fq '后端不支持分辨率' "$test_root/invalid.err"
"$control" wait --state active --timeout 2 >/dev/null

# UU's SetDisplayConfig transaction may temporarily detach the virtual path or
# change its mode.  The live monitor must observe that state without issuing a
# competing xrandr write.  A later daemon restart may recover persisted state
# because no UU topology transaction survives that process boundary.
operation_count=$(wc -l <"$test_root/backend.operations.log")
printf '%s\n' \
    '{"active": false, "mode": "1920x1080", "rate": "60", "rotation": "normal"}' \
    >"$test_root/backend.json"
sleep 0.35
grep -Fq '"active": false' "$test_root/backend.json"
grep -Fq '"state": "active"' "$test_root/state/virtual-displayd-status.json"
grep -Fq '"observed_virtual": {' "$test_root/state/virtual-displayd-status.json"
grep -A8 -F '"observed_virtual": {' \
    "$test_root/state/virtual-displayd-status.json" | grep -Fq '"active": false'
[[ $(wc -l <"$test_root/backend.operations.log") -eq $operation_count ]]

# A hard crash does not run cleanup; systemd must restore the active session.
kill -KILL "$daemon_pid"
wait "$daemon_pid" 2>/dev/null || true
daemon_pid=
grep -Fq '"active": false' "$test_root/backend.json"
start_daemon
"$control" wait --state active --timeout 3 >"$test_root/restarted.json"
grep -Fq '"mode": "2560x1440"' "$test_root/restarted.json"
grep -Fq '"rotation": "left"' "$test_root/restarted.json"

# A clean logout/service stop ends the projection and clears persisted active
# state so UU can identify the next display from its before/after snapshot.
kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
grep -Fq '"active": false' "$test_root/backend.json"
grep -Fq '"active": false' "$test_root/state/virtual-displayd-desired.json"
start_daemon
"$control" wait --state off --timeout 3 >"$test_root/clean-restart.json"
kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
[[ -s $test_root/state/virtual-displayd.log ]]
[[ -s $test_root/state/virtual-displayd-status.json ]]
[[ -s $test_root/state/virtual-displayd-desired.json ]]

printf '虚拟显示器守护进程认证、串行状态机、被动拓扑观测和重启恢复检查通过。\n'
