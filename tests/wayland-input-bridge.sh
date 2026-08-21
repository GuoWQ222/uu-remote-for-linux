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
export UU_REMOTE_WAYLAND_CAPTURE_TEST_STREAMS='77:128x72@64,0;78:64x128@0,0'

/usr/bin/python3 - "$bridge" <<'PY'
import importlib.machinery
import importlib.util
import sys
import tempfile

path = sys.argv[1]
loader = importlib.machinery.SourceFileLoader("uu_remote_input_bridge", path)
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)

def configure(streams):
    backend = module.WaylandPortalBackend.__new__(
        module.WaylandPortalBackend
    )
    backend._configure_stream_layout(streams, use_x11=False)
    return backend

def rejected(streams):
    try:
        configure(streams)
    except module.PortalRequestError:
        return
    raise AssertionError(f"unsafe Portal layout accepted: {streams!r}")

valid = configure([
    {"node": 1, "width": 3840, "height": 2160, "position": (0, 0)},
    {"node": 2, "width": 3840, "height": 2160, "position": (3840, 0)},
    {"node": 3, "width": 3840, "height": 2160, "position": (0, 2160)},
    {"node": 4, "width": 3840, "height": 2160, "position": (3840, 2160)},
])
assert (valid.canvas_width, valid.canvas_height) == (7680, 4320)
with tempfile.TemporaryDirectory() as directory:
    log_path = module.Path(directory) / "large.log"
    log_path.write_bytes(b"x" * (1024 * 1024) + b"\nfinal-error\n")
    assert module.read_last_log_line(log_path) == "final-error"
rejected([
    {"node": index + 1, "width": 1, "height": 1, "position": (index, 0)}
    for index in range(17)
])
rejected([
    {"node": 1, "width": 16385, "height": 1, "position": (0, 0)}
])
rejected([
    {"node": 1, "width": 8192, "height": 4097, "position": (0, 0)}
])
rejected([
    {"node": 1, "width": 1, "height": 1, "position": (-(2**31), 0)},
    {"node": 2, "width": 1, "height": 1, "position": (2**31 - 1, 0)},
])
rejected([
    {"node": 7, "width": 1, "height": 1, "position": (0, 0)},
    {"node": 7, "width": 1, "height": 1, "position": (1, 0)},
])

original_load = module.WaylandPortalBackend._load_gio
original_start = module.WaylandPortalBackend._start_capture_bridge
original_trace = module.os.environ.get("UU_REMOTE_WAYLAND_PORTAL_FAKE_TRACE")
original_video = module.os.environ.get(
    "UU_REMOTE_WAYLAND_CAPTURE_TEST_VIDEO"
)
try:
    with tempfile.TemporaryDirectory() as directory:
        root = module.Path(directory)
        module.os.environ["UU_REMOTE_WAYLAND_PORTAL_FAKE_TRACE"] = str(
            root / "trace"
        )
        module.os.environ["UU_REMOTE_WAYLAND_CAPTURE_TEST_VIDEO"] = "1"
        module.WaylandPortalBackend._load_gio = lambda self: None
        module.WaylandPortalBackend._start_capture_bridge = (
            lambda self: (_ for _ in ()).throw(
                RuntimeError("fixture failure")
            )
        )
        try:
            module.WaylandPortalBackend(root, root / "frame")
        except module.WaylandCaptureStartupError as error:
            assert "fixture failure" in str(error)
        else:
            raise AssertionError("capture startup failure was not classified")
finally:
    module.WaylandPortalBackend._load_gio = original_load
    module.WaylandPortalBackend._start_capture_bridge = original_start
    if original_trace is None:
        module.os.environ.pop("UU_REMOTE_WAYLAND_PORTAL_FAKE_TRACE", None)
    else:
        module.os.environ["UU_REMOTE_WAYLAND_PORTAL_FAKE_TRACE"] = original_trace
    if original_video is None:
        module.os.environ.pop("UU_REMOTE_WAYLAND_CAPTURE_TEST_VIDEO", None)
    else:
        module.os.environ["UU_REMOTE_WAYLAND_CAPTURE_TEST_VIDEO"] = original_video

events = []

class FailingProcess:
    def poll(self):
        events.append("process-poll")
        return None

    def terminate(self):
        events.append("process-terminate")
        raise RuntimeError("terminate failed")

    def wait(self, timeout):
        events.append(f"process-wait-{timeout}")
        return 0

class FailingPipeline:
    def set_state(self, state):
        events.append("pipeline-stop")
        raise RuntimeError("pipeline stop failed")

    def get_state(self, timeout):
        events.append("pipeline-wait")

class FailingMap:
    def close(self):
        events.append("map-close")
        raise BufferError("exported view")

class FixturePath:
    def __init__(self, label, fail=False):
        self.label = label
        self.fail = fail

    def unlink(self, missing_ok=False):
        events.append(self.label)
        if self.fail:
            raise OSError(f"{self.label} failed")

class FailingConnection:
    def call_sync(self, *args):
        events.append("portal-close")
        raise RuntimeError("portal close failed")

class FailingX11:
    def XCloseDisplay(self, display):
        events.append("x11-close")
        raise RuntimeError("x11 close failed")

backend = module.WaylandPortalBackend.__new__(module.WaylandPortalBackend)
backend.fake_trace = ""
backend.capture_state = "active"
backend.cursor_helper_process = FailingProcess()
backend.capture_pipeline = FailingPipeline()
backend.Gst = type(
    "Gst", (), {"State": type("State", (), {"NULL": 0}), "SECOND": 1}
)
backend.capture_pipewire_fds = []
backend.capture_map = FailingMap()
backend.frame_path = FixturePath("frame-unlink", fail=True)
backend.capture_backing_path = FixturePath("backing-unlink")
backend.session_handle = "/fixture/session"
backend.connection = FailingConnection()
backend.Gio = type(
    "Gio",
    (),
    {"DBusCallFlags": type("Flags", (), {"NONE": 0})},
)
backend.display = object()
backend.x11 = FailingX11()
backend.cleanup_errors = []
backend.close()
assert {
    "process-terminate",
    "process-wait-2",
    "pipeline-stop",
    "pipeline-wait",
    "map-close",
    "frame-unlink",
    "backing-unlink",
    "portal-close",
    "x11-close",
}.issubset(events)
assert len(backend.cleanup_errors) == 6, backend.cleanup_errors
assert backend.capture_map is not None

backend = module.WaylandPortalBackend.__new__(module.WaylandPortalBackend)
backend.fake_trace = ""
backend.capture_pipeline = object()
backend.capture_callback_error = "node=7 fixture callback failure"
try:
    backend.ensure_healthy()
except RuntimeError as error:
    assert "fixture callback failure" in str(error)
else:
    raise AssertionError("capture callback failure was not detected")
PY

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
grep -q '^force_cursor=1$' "$endpoint"
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
send(2, 9, struct.pack("<iiIIIQ", 0, 32768, 0, 0xC001, 0, 0))
time.sleep(0.2)
PY

for _ in {1..100}; do
    grep -q '^NotifyKeyboardKeycode 30 0$' "$trace" 2>/dev/null && break
    sleep 0.02
done

grep -q '^session authorized devices=3 streams=77:128x72,78:64x128 canvas=192x128$' "$trace"
grep -q '^capture active transport=shared-frame$' "$trace"
grep -q '^NotifyPointerMotionAbsolute 77 ' "$trace"
grep -q '^NotifyPointerMotionAbsolute 78 0.0 ' "$trace"
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
grep -q '"cursor_state": "metadata"' \
    "$state_dir/input-bridge-status.json"
grep -Eq '"capture_frames": [1-9][0-9]*' \
    "$state_dir/input-bridge-status.json"
test -s "$(dirname "$endpoint")/uu-remote-wayland-frame.bin"
/usr/bin/python3 - "$(dirname "$endpoint")/uu-remote-wayland-frame.bin" <<'PY'
import mmap
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1]).resolve()
with path.open("rb") as handle, mmap.mmap(
    handle.fileno(), 0, access=mmap.ACCESS_READ
) as frame:
    header = struct.unpack_from("<10I2i16x", frame)
assert header[:3] == (0x46575555, 2, 64), header
assert header[3:6] == (192, 128, 768), header
assert header[7] == 2 and header[9] > 0, header
assert header[10:12] == (-64, 0), header
PY
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
