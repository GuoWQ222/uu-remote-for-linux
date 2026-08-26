#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-second-screen-bridge"
test_root=$(mktemp -d)
daemon_pid=""
bridge_pid=""

cleanup() {
    [[ -z $bridge_pid ]] || kill "$bridge_pid" >/dev/null 2>&1 || true
    [[ -z $daemon_pid ]] || kill "$daemon_pid" >/dev/null 2>&1 || true
    rm -rf -- "$test_root"
}
trap cleanup EXIT

readonly daemon_socket="$test_root/virtual-display.sock"
readonly daemon_log="$test_root/daemon-operations.jsonl"
readonly endpoint="$test_root/prefix/drive_c/uu-remote-second-screen-bridge.ini"
readonly status="$test_root/state/second-screen-status.json"
readonly lock="$test_root/state/second-screen.lock"
readonly bridge_events="$test_root/state/second-screen-events.jsonl"
readonly choice_file="$test_root/second-screen-choice"
printf '%s\n' 扩展 >"$choice_file"
export UU_REMOTE_SECOND_SCREEN_CHOOSER="$project_root/tests/fixtures/second-screen-chooser"
export UU_REMOTE_FAKE_SECOND_SCREEN_CHOICE_FILE="$choice_file"

/usr/bin/python3 - "$daemon_socket" "$daemon_log" <<'PY' &
import json
import os
import signal
import socket
import sys
from pathlib import Path

socket_path = Path(sys.argv[1])
log_path = Path(sys.argv[2])
socket_path.parent.mkdir(parents=True, exist_ok=True)
socket_path.unlink(missing_ok=True)
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(str(socket_path))
server.listen(16)
server.settimeout(0.2)
active = False
mode = "1920x1080"
running = True


def stop(_signum, _frame):
    global running
    running = False


signal.signal(signal.SIGTERM, stop)
while running:
    try:
        connection, _ = server.accept()
    except TimeoutError:
        continue
    with connection:
        request = json.loads(connection.makefile("rb").readline())
        command = request["command"]
        if command in {"create", "update"}:
            active = True
            mode = request["mode"]
        elif command == "remove":
            active = False
        if command in {"create", "update", "remove"}:
            with log_path.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(request, sort_keys=True) + "\n")
        result = {
            "protocol_version": 1,
            "desired": {
                "active": active,
                "mode": mode,
                "projection": request.get("projection", "extend"),
            },
            "backend": {
                "outputs": [
                    {
                        "output": "HDMI-1",
                        "active": True,
                        "primary": True,
                        "current_mode": "2560x1440",
                        "rotation": "normal",
                        "geometry": [2560, 1440, 1440, 0],
                    }
                ],
                "xrandr": {
                    "modes": ["1920x1080", "2560x1440", "1600x900"],
                    "mode_rates": {
                        "1920x1080": ["60"],
                        "2560x1440": ["59.95"],
                        "1600x900": ["60"],
                    },
                }
            },
        }
        response = {"ok": True, "request_id": request.get("request_id"), "result": result}
        connection.sendall((json.dumps(response) + "\n").encode())
server.close()
socket_path.unlink(missing_ok=True)
PY
daemon_pid=$!

for _ in {1..100}; do
    [[ -S $daemon_socket ]] && break
    sleep 0.02
done
[[ -S $daemon_socket ]]
"$bridge" check "$daemon_socket" >/dev/null

mkdir -p -- "$(dirname -- "$bridge_events")"
"$bridge" watch "$endpoint" "$status" "$daemon_socket" "$lock" \
    >"$bridge_events" &
bridge_pid=$!
for _ in {1..100}; do
    [[ -s $endpoint && -s $status ]] && break
    sleep 0.02
done
[[ -s $endpoint && -s $status ]]
[[ $(stat -c %a "$endpoint") == 600 ]]
[[ $(stat -c %a "$status") == 600 ]]

/usr/bin/python3 - "$endpoint" "$choice_file" <<'PY'
import configparser
import socket
import struct
import sys
from concurrent.futures import ThreadPoolExecutor

configuration = configparser.ConfigParser()
configuration.read(sys.argv[1])
port = configuration.getint("bridge", "port")
token = bytes.fromhex(configuration.get("bridge", "token"))
request = struct.Struct("<IBBHI16sIIII")
response = struct.Struct("<IBBHI16sII")
magic = 0x53535555


def exchange(client, command, sequence, width=0, height=0, rate=0, rotation=0, packet_token=token):
    client.sendto(
        request.pack(magic, 1, command, request.size, sequence, packet_token, width, height, rate, rotation),
        ("127.0.0.1", port),
    )
    packet, source = client.recvfrom(4096)
    assert source == ("127.0.0.1", port)
    values = response.unpack(packet)
    assert values[:5] == (magic, 1, command | 0x80, response.size, sequence)
    assert values[5] == token
    return values[6], values[7]


client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
client.settimeout(0.25)
client.sendto(
    request.pack(magic, 1, 4, request.size, 1, b"\0" * 16, 0, 0, 0, 0),
    ("127.0.0.1", port),
)
try:
    client.recvfrom(4096)
except TimeoutError:
    pass
else:
    raise AssertionError("unauthenticated packet received a reply")

assert exchange(client, 4, 1) == (1, 0)
assert exchange(client, 4, 1) == (0, 2)
# A server mutation may arrive as UPDATE before CREATE.  It must fail closed
# until the controller has completed the visible mode-selection step.
assert exchange(client, 2, 2, 1600, 900, 75000, 1) == (0, 3)
assert exchange(client, 5, 3) == (1, 0)
# Real UU orders UPDATE_MODES before PLUG_IN.  UPDATE must cache geometry but
# leave the monitor absent so UU can identify it in the PLUG_IN delta.
assert exchange(client, 2, 4, 1600, 900, 75000, 1) == (1, 0)
assert exchange(client, 1, 5, 2560, 1440, 60000, 0) == (1, 0)
assert exchange(client, 2, 6, 1920, 1080, 60000, 0) == (1, 0)
assert exchange(client, 3, 7) == (1, 0)
# Never reuse the daemon's persisted projection for a fresh connection.
assert exchange(client, 1, 8, 1920, 1080, 60000, 0) == (0, 3)
with open(sys.argv[2], "w", encoding="utf-8") as handle:
    handle.write("复制\n")
assert exchange(client, 5, 9) == (1, 0)
assert exchange(client, 2, 10, 1920, 1080, 60000, 0) == (1, 0)
assert exchange(client, 1, 11, 1920, 1080, 60000, 0) == (1, 0)
assert exchange(client, 3, 12) == (1, 0)
client.close()


def ping(index):
    worker = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    worker.settimeout(2)
    try:
        assert exchange(worker, 4, index + 1) == (1, 0)
    finally:
        worker.close()


with ThreadPoolExecutor(max_workers=8) as executor:
    list(executor.map(ping, range(24)))
PY

/usr/bin/python3 - "$daemon_log" "$status" "$bridge_events" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    operations = [json.loads(line) for line in handle]
expected_operations = [
    {
        "version": 1,
        "request_id": operations[0]["request_id"],
        "command": "create",
        "mode": "1600x900",
        "rate": "60",
        "rotation": "left",
        "projection": "extend",
    },
    {
        "version": 1,
        "request_id": operations[1]["request_id"],
        "command": "update",
        "mode": "1920x1080",
        "rate": "60",
        "rotation": "normal",
        "projection": "extend",
    },
    {
        "version": 1,
        "request_id": operations[2]["request_id"],
        "command": "remove",
    },
    {
        "version": 1,
        "request_id": operations[3]["request_id"],
        "command": "create",
        "mode": "2560x1440",
        "rate": "59.95",
        "rotation": "normal",
        "projection": "duplicate",
    },
    {
        "version": 1,
        "request_id": operations[4]["request_id"],
        "command": "remove",
    },
]
if operations != expected_operations:
    print(
        json.dumps(
            {"expected": expected_operations, "actual": operations},
            ensure_ascii=False,
            indent=2,
        ),
        file=sys.stderr,
    )
assert operations == expected_operations
status = json.load(open(sys.argv[2], encoding="utf-8"))
assert status["state"] == "active"
assert status["accepted"] >= 30
assert status["rejected"] >= 2
assert status["protocol_version"] == 1
assert status["choice_requests"] == 2
assert status["projection_mode"] is None
assert status["pending_geometry"] is None
events = [json.loads(line) for line in open(sys.argv[3], encoding="utf-8")]
assert any(
    event.get("event") == "request_failed"
    and event.get("command") == "update"
    and "尚未" in event.get("error", "")
    for event in events
)
assert any(
    event.get("event") == "update_deferred"
    and event.get("mode") == "1600x900"
    and event.get("projection") == "extend"
    for event in events
)
assert any(
    event.get("event") == "rate_normalized"
    and event.get("mode") == "1600x900"
    and event.get("requested") == "75"
    and event.get("selected") == "60"
    for event in events
)
assert [
    event["projection"]
    for event in events
    if event.get("event") == "choice_completed"
] == ["extend", "duplicate"]
PY

kill "$bridge_pid"
wait "$bridge_pid"
bridge_pid=""
[[ ! -e $endpoint ]]
printf '无线副屏鉴权、协议转换、并发与退出清理检查通过。\n'
