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
export UU_REMOTE_INPUT_BRIDGE_FAKE_LOCK_MASK=2
export UU_REMOTE_WAYLAND_CAPTURE_TEST_VIDEO=1
export UU_REMOTE_WAYLAND_CAPTURE_TEST_STREAMS='77:128x72@64,0;78:64x128@0,0'

/usr/bin/python3 - "$bridge" <<'PY'
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
from pathlib import Path

path = sys.argv[1]
loader = importlib.machinery.SourceFileLoader("uu_remote_input_bridge", path)
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)

assert module.CAPTURE_PRIMARY_STREAM_RATE == 60
assert module.CAPTURE_SECONDARY_STREAM_RATE == 30
assert module.CAPTURE_MAX_STREAM_RATE == 60
assert module.CAPTURE_MOTION_COPY_RATIO_DENOMINATOR == 32
assert module.capture_stream_rate(77, 77) == 60
assert module.capture_stream_rate(78, 77) == 30
assert module.capture_backfill_indexes([4, 7], [5, 7], 0) == []
assert module.capture_backfill_indexes([4, 6], [5, 7], 0) == [1]
assert module.capture_backfill_indexes([4, 6], [5, 7], 1) == [0]
try:
    module.capture_backfill_indexes([1], [1, 2], 0)
except ValueError:
    pass
else:
    raise AssertionError("mismatched capture generations were accepted")
source_text = Path(path).read_text(encoding="utf-8")
assert 'source.set_property("always-copy", True)' in source_text
assert 'source.set_property("always-copy", False)' not in source_text
assert '"identity", f"corrupt-frame-guard-{index}"' in source_text
assert (
    '"drop-buffer-flags", self.Gst.BufferFlags.CORRUPTED'
    in source_text
)
assert 'sink.set_property("processing-deadline", 0)' in source_text
assert 'sink.set_property("enable-last-sample", False)' in source_text
assert 'sink.set_property("wait-on-eos", False)' in source_text
assert '"video/x-raw,format=BGRA,"' in source_text
assert "advance_capture_deadline" not in source_text
assert "if not eligible:" not in source_text
assert 'if running and hasattr(backend, "ensure_healthy"):' in source_text

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

class IndicatorQuery:
    def __init__(self):
        self.results = [(0, 5), (1, 0)]
        self.calls = 0

    def __call__(self, _display, _device, indicators):
        result, mask = self.results.pop(0)
        self.calls += 1
        if result == 0:
            pointer = module.ctypes.cast(
                indicators,
                module.ctypes.POINTER(module.ctypes.c_uint),
            )
            pointer[0] = mask
        return result

indicator_query = IndicatorQuery()
lock_backend = module.WaylandPortalBackend.__new__(
    module.WaylandPortalBackend
)
lock_backend.fake_trace = ""
lock_backend.display = object()
lock_backend.x11 = object()
lock_backend._xkb_get_indicator_state = indicator_query
lock_backend._lock_mask_cached = None
lock_backend._lock_mask_checked_at = 0.0
assert lock_backend.lock_mask() == 5
assert lock_backend.lock_mask() == 5
assert indicator_query.calls == 1
lock_backend._lock_mask_checked_at = 0.0
assert lock_backend.lock_mask() is None
assert indicator_query.calls == 2
lock_backend._xkb_get_indicator_state = None
lock_backend._lock_mask_cached = 5
lock_backend._lock_mask_checked_at = 0.0
assert lock_backend.lock_mask() is None

# A stationary controller emits absolute MOVE packets continuously.  Portal
# must receive the first one, suppress exact no-op repeats, and reassert the
# same target if a local physical mouse has moved the compositor pointer away.
motion = configure([
    {"node": 9, "width": 128, "height": 72, "position": (0, 0)},
])
motion.session_handle = "/fixture/session"
motion.pointer_motion_requests = 0
motion.pointer_motion_injected = 0
motion.pointer_motion_suppressed = 0
motion.pointer_motion_same_target = 0
motion.pointer_motion_changed_target = 0
motion.pointer_motion_unit_changes = 0
motion.pointer_motion_two_point_oscillations = 0
motion.pointer_motion_actual_matches = 0
motion.pointer_motion_actual_mismatches = 0
motion.pointer_absolute_requests = 0
motion.pointer_relative_requests = 0
motion.pointer_relative_zero_suppressed = 0
motion.previous_absolute_request = None
motion.last_absolute_request = None
motion.pointer_recent_targets = []
motion.last_absolute_pointer = None
motion.pointer_actual_root_x = 0
motion.pointer_actual_root_y = 0
motion.pointer_target_root_x = 0
motion.pointer_target_root_y = 0
notifications = []
actual_positions = [None, (64, 36), (62, 36)]
motion._root_pointer_position = lambda: actual_positions.pop(0)
motion._notify = lambda method, signature, values: notifications.append(
    (method, signature, values)
)
for _ in range(3):
    motion.motion(32768, 32768, True, False)
assert len(notifications) == 2, notifications
assert motion.pointer_motion_requests == 3
assert motion.pointer_motion_injected == 2
assert motion.pointer_motion_suppressed == 1
assert motion.pointer_motion_same_target == 2
assert motion.pointer_motion_changed_target == 0
assert motion.pointer_motion_actual_matches == 1
assert motion.pointer_motion_actual_mismatches == 1
assert motion.pointer_recent_targets == [[9, 64, 36]] * 3
motion.motion(0, 0, False)
assert len(notifications) == 2, notifications
assert motion.pointer_motion_requests == 4
assert motion.pointer_motion_suppressed == 2
assert motion.pointer_absolute_requests == 3
assert motion.pointer_relative_requests == 1
assert motion.pointer_relative_zero_suppressed == 1
assert motion.last_absolute_pointer == (9, 64, 36)
motion.motion(2, -1, False)
assert len(notifications) == 3, notifications
assert motion.pointer_motion_injected == 3
assert motion.pointer_relative_requests == 2
assert motion.last_absolute_pointer is None
assert notifications[0][2][2:] == (9, 64.0, 36.0)

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

assert module.WaylandPortalBackend._process_exit_detail(0) == "exit=0"
assert (
    module.WaylandPortalBackend._process_exit_detail(-15)
    == "signal=SIGTERM(15)"
)

class ReplacementProcess:
    def poll(self):
        return None

with tempfile.TemporaryDirectory() as directory:
    root = module.Path(directory)
    cursor_backend = module.WaylandPortalBackend.__new__(
        module.WaylandPortalBackend
    )
    cursor_backend.cursor_helper_log_path = root / "cursor.log"
    cursor_backend.cursor_helper_log_path.write_text(
        "cursor-update count=5 sequence=9\n", encoding="utf-8"
    )
    cursor_backend.cursor_helper_restart_times = []
    cursor_backend.cursor_helper_restarts = 0
    cursor_backend.cursor_helper_last_exit = ""
    cursor_backend.cursor_helper_process = object()
    cursor_starts = []

    def start_cursor(reset_cursor=True):
        cursor_starts.append(reset_cursor)
        cursor_backend.cursor_helper_process = ReplacementProcess()

    cursor_backend._start_cursor_bridge = start_cursor
    cursor_backend._restart_cursor_bridge(-11)
    assert cursor_starts == [False]
    assert cursor_backend.cursor_helper_restarts == 1
    assert "SIGSEGV(11)" in cursor_backend.cursor_helper_last_exit
    assert "cursor-update count=5" in cursor_backend.cursor_helper_last_exit
    assert cursor_backend.cursor_helper_process.poll() is None
    assert "cursor-bridge-restarted count=1" in (
        cursor_backend.cursor_helper_log_path.read_text(encoding="utf-8")
    )

    cursor_backend.cursor_helper_restart_times = [
        module.time.monotonic(),
    ] * module.CURSOR_HELPER_RESTART_LIMIT
    try:
        cursor_backend._restart_cursor_bridge(1)
    except RuntimeError as error:
        assert "反复停止" in str(error)
        assert "exit=1" in str(error)
    else:
        raise AssertionError("cursor helper restart fuse did not open")
    assert cursor_starts == [False]

    descriptor, peer_descriptor = os.pipe()
    fd_backend = module.WaylandPortalBackend.__new__(
        module.WaylandPortalBackend
    )
    fd_backend.fake_trace = ""
    fd_backend.cursor_path = root / "cursor.bin"
    fd_backend.cursor_helper_path = root / "cursor-helper"
    fd_backend.cursor_helper_log_path = root / "fd-failure.log"
    fd_backend.streams = [
        {"node": 77, "width": 128, "height": 72},
    ]
    fd_backend._open_pipewire_remote = lambda: descriptor
    original_open = module.os.open

    def fail_log_open(*_args, **_kwargs):
        raise OSError("fixture log open failure")

    module.os.open = fail_log_open
    try:
        try:
            fd_backend._start_cursor_bridge(reset_cursor=False)
        except OSError as error:
            assert "fixture log open failure" in str(error)
        else:
            raise AssertionError("cursor log open failure was accepted")
    finally:
        module.os.open = original_open
        os.close(peer_descriptor)
    try:
        os.fstat(descriptor)
    except OSError:
        pass
    else:
        os.close(descriptor)
        raise AssertionError("cursor Portal descriptor leaked")

health_events = []

class HealthyBus:
    def pop_filtered(self, _types):
        health_events.append("bus")
        return None

class HealthyPipeline:
    def get_bus(self):
        return HealthyBus()

    def get_state(self, _timeout):
        health_events.append("state")
        return (1, 2, 0)

class DeadCursorProcess:
    def poll(self):
        health_events.append("cursor")
        return -11

health_backend = module.WaylandPortalBackend.__new__(
    module.WaylandPortalBackend
)
health_backend.fake_trace = ""
health_backend.capture_pipeline = HealthyPipeline()
health_backend.capture_callback_error = ""
health_backend.cursor_helper_process = DeadCursorProcess()
health_backend.Gst = type(
    "Gst",
    (),
    {
        "MessageType": type("MessageType", (), {"ERROR": 1, "EOS": 2}),
        "StateChangeReturn": type("StateChangeReturn", (), {"FAILURE": 0}),
        "State": type("State", (), {"PAUSED": 1, "PLAYING": 2}),
    },
)
health_backend._restart_cursor_bridge = (
    lambda returncode: health_events.append(("restart", returncode))
)
health_backend.ensure_healthy()
assert health_events == ["bus", "state", "cursor", ("restart", -11)]
PY

# Mutter emits cursor-only PipeWire buffers with no pixels and the CORRUPTED
# flag. GstVideoFilter allocates a full output buffer before mapping the empty
# input, so without an early guard a transform can forward unwritten pool
# memory as an apparent full frame.
/usr/bin/python3 - <<'PY'
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst

Gst.init(None)
WIDTH = 64
HEIGHT = 32
FRAME_BYTES = WIDTH * HEIGHT * 4
DURATION = Gst.SECOND // 60


def run_pipeline(guard: bool):
    guard_text = (
        "identity drop-buffer-flags=corrupted ! " if guard else ""
    )
    pipeline = Gst.parse_launch(
        "appsrc name=src format=time "
        # Force a real GstVideoFilter conversion so the fixture also covers
        # compositors whose native packed-four-byte format differs from BGRA.
        'caps="video/x-raw,format=RGBA,width=64,height=32,framerate=60/1" ! '
        f"{guard_text}"
        "videoconvert ! videoscale ! "
        "video/x-raw,format=BGRA,width=64,height=32 ! "
        "appsink name=sink sync=false max-buffers=10 drop=false"
    )
    source = pipeline.get_by_name("src")
    sink = pipeline.get_by_name("sink")
    assert (
        pipeline.set_state(Gst.State.PLAYING)
        != Gst.StateChangeReturn.FAILURE
    )
    try:
        for index, value in enumerate((0x11, None, 0x77)):
            if value is None:
                buffer = Gst.Buffer.new()
                buffer.set_flags(Gst.BufferFlags.CORRUPTED)
            else:
                pixels = bytes((value, value, value, 0xFF)) * WIDTH * HEIGHT
                buffer = Gst.Buffer.new_allocate(None, len(pixels), None)
                assert buffer.fill(0, pixels) == len(pixels)
            buffer.pts = index * DURATION
            buffer.duration = DURATION
            assert source.emit("push-buffer", buffer) == Gst.FlowReturn.OK
        assert source.emit("end-of-stream") == Gst.FlowReturn.OK

        outputs = []
        for _ in range(4):
            sample = sink.emit("try-pull-sample", Gst.SECOND)
            if sample is None:
                break
            buffer = sample.get_buffer()
            mapped, info = buffer.map(Gst.MapFlags.READ)
            assert mapped
            try:
                outputs.append(
                    (
                        len(info.data),
                        buffer.has_flags(Gst.BufferFlags.CORRUPTED),
                        bytes(info.data[:3]),
                    )
                )
            finally:
                buffer.unmap(info)
        return outputs
    finally:
        pipeline.set_state(Gst.State.NULL)


unguarded = run_pipeline(False)
assert len(unguarded) == 3, unguarded
assert unguarded[0] == (FRAME_BYTES, False, b"\x11\x11\x11"), unguarded
assert unguarded[1][0] == FRAME_BYTES and unguarded[1][1], unguarded
assert unguarded[2] == (FRAME_BYTES, False, b"\x77\x77\x77"), unguarded

guarded = run_pipeline(True)
assert guarded == [
    (FRAME_BYTES, False, b"\x11\x11\x11"),
    (FRAME_BYTES, False, b"\x77\x77\x77"),
], guarded
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
grep -q '^native_lock_keys=1$' "$endpoint"
grep -q '^lock_state_valid=1$' "$endpoint"
grep -q '^lock_mask=2$' "$endpoint"
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
send(2, 10, struct.pack("<iiIIIQ", 0, 32768, 0, 0xC001, 0, 0))
# One down/up pair must toggle each desktop lock exactly once.  Scan-code
# packets exercise the same KEYEVENTF_SCANCODE path used by SendInput.
send(3, 11, struct.pack("<HHIIQ", 0x14, 0x3A, 0x0008, 0, 0))
send(3, 12, struct.pack("<HHIIQ", 0x14, 0x3A, 0x000A, 0, 0))
send(3, 13, struct.pack("<HHIIQ", 0x90, 0x45, 0x0008, 0, 0))
send(3, 14, struct.pack("<HHIIQ", 0x90, 0x45, 0x000A, 0, 0))
send(3, 15, struct.pack("<HHIIQ", 0x91, 0x46, 0x0008, 0, 0))
send(3, 16, struct.pack("<HHIIQ", 0x91, 0x46, 0x000A, 0, 0))
time.sleep(0.2)
PY

for _ in {1..100}; do
    if grep -q '^NotifyKeyboardKeycode 70 0$' "$trace" 2>/dev/null &&
        grep -q '^lock_mask=5$' "$endpoint" 2>/dev/null; then
        break
    fi
    sleep 0.02
done
for _ in {1..150}; do
    if grep -q '"pointer_motion_requests": 4' \
        "$state_dir/input-bridge-status.json" 2>/dev/null &&
        grep -q '"keyboard_packets": 8' \
            "$state_dir/input-bridge-status.json" 2>/dev/null; then
        break
    fi
    sleep 0.02
done

grep -q '^session authorized devices=3 streams=77:128x72,78:64x128 canvas=192x128$' "$trace"
grep -q '^capture active transport=shared-frame$' "$trace"
grep -q '^tracked_cursor_authoritative=1$' "$endpoint"
grep -q '^NotifyPointerMotionAbsolute 77 64.0 36.0$' "$trace"
[[ $(grep -c '^NotifyPointerMotionAbsolute 78 0.0 64.0$' "$trace") == 1 ]]
grep -q '^NotifyPointerMotion 5.0 -3.0$' "$trace"
grep -q '^NotifyPointerButton 272 1$' "$trace"
grep -q '^NotifyPointerButton 272 0$' "$trace"
[[ $(grep -c '^NotifyPointerAxisDiscrete 0 -1$' "$trace") == 1 ]]
grep -q '^NotifyKeyboardKeycode 30 1$' "$trace"
grep -q '^NotifyKeyboardKeycode 30 0$' "$trace"
for event in \
    'NotifyKeyboardKeycode 58 1' \
    'NotifyKeyboardKeycode 58 0' \
    'NotifyKeyboardKeycode 69 1' \
    'NotifyKeyboardKeycode 69 0' \
    'NotifyKeyboardKeycode 70 1' \
    'NotifyKeyboardKeycode 70 0'; do
    [[ $(grep -c "^${event}$" "$trace") == 1 ]]
done
grep -q '^lock_mask=5$' "$endpoint"
grep -q '"backend": "wayland-portal"' \
    "$state_dir/input-bridge-status.json"
grep -q '"portal_stream": 77' \
    "$state_dir/input-bridge-status.json"
grep -q '"capture_state": "active"' \
    "$state_dir/input-bridge-status.json"
grep -q '"target_fps": 60' \
    "$state_dir/input-bridge-status.json"
grep -q '"effective_fps":' \
    "$state_dir/input-bridge-status.json"
[[ $(grep -o '"deduplicate": 1' \
    "$state_dir/input-bridge-status.json" | wc -l) -eq 2 ]]
grep -q '"target_fps": 30' \
    "$state_dir/input-bridge-status.json"
grep -q '"source_copies":' \
    "$state_dir/input-bridge-status.json"
grep -q '"backfill_copies":' \
    "$state_dir/input-bridge-status.json"
grep -q '"native_updates":' \
    "$state_dir/input-bridge-status.json"
grep -q '"changed_blocks":' \
    "$state_dir/input-bridge-status.json"
grep -q '"temporal_suppressed_blocks":' \
    "$state_dir/input-bridge-status.json"
grep -q '"last_copy_bytes":' \
    "$state_dir/input-bridge-status.json"
grep -q '"motion_events":' \
    "$state_dir/input-bridge-status.json"
grep -q '"corrupted_frames_dropped":' \
    "$state_dir/input-bridge-status.json"
grep -q '"map_failures_dropped":' \
    "$state_dir/input-bridge-status.json"
grep -q '"short_frames_dropped":' \
    "$state_dir/input-bridge-status.json"
grep -q '"pointer_motion_requests": 4' \
    "$state_dir/input-bridge-status.json"
grep -q '"pointer_motion_injected": 3' \
    "$state_dir/input-bridge-status.json"
grep -q '"pointer_motion_suppressed": 1' \
    "$state_dir/input-bridge-status.json"
grep -q '"keyboard_packets": 8' \
    "$state_dir/input-bridge-status.json"
grep -q '"duplicates":' \
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
