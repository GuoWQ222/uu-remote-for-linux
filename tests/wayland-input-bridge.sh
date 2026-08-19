#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge"

test_root=$(mktemp -d)
bridge_pid=

cleanup() {
    if [[ $bridge_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$bridge_pid" >/dev/null 2>&1; then
        kill "$bridge_pid" >/dev/null 2>&1 || true
        wait "$bridge_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT

readonly state_dir="$test_root/state"
readonly endpoint="$test_root/drive_c/uu-remote-input-bridge.endpoint"
readonly log="$test_root/input-bridge.log"
readonly lock="$test_root/input-bridge.lock"
readonly trace="$test_root/portal.trace"
readonly runtime_dir="$test_root/runtime"
mkdir -p "$state_dir" "$runtime_dir"

export XDG_SESSION_TYPE=wayland
export WAYLAND_DISPLAY=wayland-test-0
export XDG_RUNTIME_DIR="$runtime_dir"
export UU_REMOTE_DESKTOP_BACKEND=wayland-xwayland
export UU_REMOTE_WAYLAND_PORTAL_FAKE_TRACE="$trace"
export UU_REMOTE_WAYLAND_CAPTURE_TEST_VIDEO=1

[[ $("$bridge" check) == wayland-portal ]]
"$bridge" watch \
    "$state_dir" "$endpoint" "$log" "$lock" 0.2 &
bridge_pid=$!

for _ in {1..100}; do
    [[ -s $endpoint ]] && break
    sleep 0.02
done
[[ -s $endpoint ]]
grep -q '^native_lock_keys=0$' "$endpoint"
if grep -q '^lock_state_valid=' "$endpoint"; then
    printf 'Wayland 端点不应发布 XKB 锁定状态。\n' >&2
    exit 1
fi

/usr/bin/python3 - "$endpoint" <<'PY'
import configparser
import socket
import struct
import sys
import time
from pathlib import Path

endpoint = Path(sys.argv[1])
config = configparser.ConfigParser()
config.read(endpoint)
port = config.getint("bridge", "port")
token = bytes.fromhex(config.get("bridge", "token"))
address = ("127.0.0.1", port)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
header = struct.Struct("<IBBHI16s")
magic = 0x50495555

def send(kind, sequence, payload):
    size = header.size + len(payload)
    sock.sendto(
        header.pack(magic, 1, kind, size, sequence, token) + payload,
        address,
    )

send(1, 1, struct.pack("<II", 5151, 8))
send(2, 2, struct.pack("<iiIIIQ", 32768, 32768, 0, 0x8001, 0, 0))
send(2, 3, struct.pack("<iiIIIQ", 5, -3, 0, 0x0001, 0, 0))
send(2, 4, struct.pack("<iiIIIQ", 0, 0, 0, 0x0002, 0, 0))
send(2, 5, struct.pack("<iiIIIQ", 0, 0, 0, 0x0004, 0, 0))
send(2, 6, struct.pack("<iiIIIQ", 0, 0, 120, 0x0800, 0, 0))
send(3, 7, struct.pack("<HHIIQ", 0x41, 0x1E, 0x0008, 0, 0))
send(3, 8, struct.pack("<HHIIQ", 0x41, 0x1E, 0x000A, 0, 0))
time.sleep(0.2)
PY

for _ in {1..100}; do
    grep -q '^NotifyKeyboardKeycode 30 0$' "$trace" 2>/dev/null && break
    sleep 0.02
done

grep -q '^session authorized devices=3 stream=77 128x72$' "$trace"
grep -q '^capture active transport=shared-frame$' "$trace"
grep -q '^NotifyPointerMotionAbsolute 77 ' "$trace"
grep -q '^NotifyPointerMotion 5.0 -3.0$' "$trace"
grep -q '^NotifyPointerButton 272 1$' "$trace"
grep -q '^NotifyPointerButton 272 0$' "$trace"
[[ $(grep -c '^NotifyPointerAxisDiscrete 0 -1$' "$trace") == 1 ]]
grep -q '^NotifyKeyboardKeycode 30 1$' "$trace"
grep -q '^NotifyKeyboardKeycode 30 0$' "$trace"
grep -q '"backend": "wayland-portal"' \
    "$state_dir/input-bridge-status.json"
grep -q '"portal_stream": 77' \
    "$state_dir/input-bridge-status.json"
grep -q '"capture_state": "active"' \
    "$state_dir/input-bridge-status.json"
grep -Eq '"capture_frames": [1-9][0-9]*' \
    "$state_dir/input-bridge-status.json"
test -s "$(dirname "$endpoint")/uu-remote-wayland-frame.bin"
grep -q '"hook_pid": 5151' \
    "$state_dir/input-bridge-status.json"

kill "$bridge_pid"
wait "$bridge_pid"
bridge_pid=
[[ $("$bridge" status "$state_dir") == stopped ]]
[[ ! -e $(dirname "$endpoint")/uu-remote-wayland-frame.bin ]]
[[ -z $(find "$runtime_dir" -type f -name 'uu-remote-wayland-frame-*' -print -quit) ]]
grep -q '^close$' "$trace"

printf 'Wayland Portal 后端选择、坐标、键鼠和滚轮映射检查通过。\n'
