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
readonly trace="$test_root/xtest.trace"
mkdir -p "$state_dir"

export UU_REMOTE_INPUT_BRIDGE_FAKE_TRACE="$trace"
export XDG_SESSION_TYPE=x11

"$bridge" check
"$bridge" watch \
    "$state_dir" "$endpoint" "$log" "$lock" 0.2 &
bridge_pid=$!

for _ in {1..100}; do
    [[ -s $endpoint ]] && break
    sleep 0.02
done
[[ -s $endpoint ]]
[[ $(stat -c '%a' "$endpoint") == 600 ]]
grep -q '^\[bridge\]$' "$endpoint"
grep -q '^force_cursor=1$' "$endpoint"

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

def send(kind, sequence, payload, packet_token=token):
    size = header.size + len(payload)
    sock.sendto(
        header.pack(magic, 1, kind, size, sequence, packet_token) + payload,
        address,
    )

# Rejected authentication attempt must never reach the fake XTest backend.
send(2, 1, struct.pack("<iiIIIQ", 1, 1, 0, 1, 0, 0), b"\0" * 16)
send(1, 2, struct.pack("<II", 4242, 3))
send(2, 3, struct.pack("<iiIIIQ", 32768, 32768, 0, 0x8001, 0, 0))
send(2, 4, struct.pack("<iiIIIQ", 0, 0, 0, 0x0002, 0, 0))
send(2, 5, struct.pack("<iiIIIQ", 0, 0, 0, 0x0004, 0, 0))
send(3, 6, struct.pack("<HHIIQ", 0x41, 0x1E, 0x0008, 0, 0))
send(3, 7, struct.pack("<HHIIQ", 0x41, 0x1E, 0x000A, 0, 0))
# A stuck mouse press must be released by the daemon watchdog.
send(2, 8, struct.pack("<iiIIIQ", 0, 0, 0, 0x0002, 0, 0))
time.sleep(0.4)
PY

for _ in {1..100}; do
    grep -q '^button 1 0$' "$trace" 2>/dev/null &&
        grep -q '"hook_pid": 4242' \
            "$state_dir/input-bridge-status.json" 2>/dev/null &&
        break
    sleep 0.02
done

grep -q '^motion 32768 32768 1$' "$trace"
grep -q '^button 1 1$' "$trace"
grep -q '^button 1 0$' "$trace"
grep -q '^keycode 38 1$' "$trace"
grep -q '^keycode 38 0$' "$trace"
grep -q '"hook_pid": 4242' "$state_dir/input-bridge-status.json"
grep -q '"hook_version": 3' "$state_dir/input-bridge-status.json"
grep -q '"mouse_packets": 4' "$state_dir/input-bridge-status.json"
grep -q '"mouse_moves": 1' "$state_dir/input-bridge-status.json"
grep -q '"mouse_buttons": 3' "$state_dir/input-bridge-status.json"
grep -q '"keyboard_packets": 2' "$state_dir/input-bridge-status.json"
grep -q '"rejected": 1' "$state_dir/input-bridge-status.json"
grep -q 'GameViewerServer 输入钩子已连接（PID 4242，v3）' "$log"
grep -q '安全释放 0 个按键和 1 个鼠标按钮（输入超时）' "$log"

kill "$bridge_pid"
wait "$bridge_pid"
bridge_pid=
[[ ! -e $endpoint ]]
[[ $("$bridge" status "$state_dir") == stopped ]]
grep -q '^close$' "$trace"

printf '原生输入桥认证、键鼠映射和安全松键检查通过。\n'
