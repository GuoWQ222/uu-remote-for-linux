#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly proxy="$project_root/lib/uu-remote-for-linux/uu-remote-tray-proxy"

"$proxy" --check | grep -q '^backend=StatusNotifierItem/AppIndicator3$'
"$proxy" --check | grep -q '^x11=ready$'

PROXY_PATH="$proxy" /usr/bin/python3 - <<'PY'
import ast
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import time
from unittest import mock

proxy = os.environ["PROXY_PATH"]
source = Path(proxy).read_text()
ast.parse(source, filename=proxy)
assert '"选择解码器…"' in source
assert '"远控窗口布局"' in source
assert '"被控屏幕"' in source
assert '"主显示器（自动）"' in source
assert '"手动（不自动移动）"' in source
assert '"单窗口（主屏）"' in source
assert '"双窗口（每屏一个）"' in source
assert '"--select-decoder-and-restart"' in source
assert "self.select_decoder_and_restart" in source
namespace = runpy.run_path(proxy)
proxy_class = namespace["TrayProxy"]

with tempfile.TemporaryDirectory() as state:
    layout_file = Path(state) / "window-layout"
    assert namespace["read_window_layout"](layout_file) == (
        namespace["WINDOW_LAYOUT_MANUAL"]
    )
    namespace["write_window_layout"](
        layout_file, namespace["WINDOW_LAYOUT_DUAL"]
    )
    assert layout_file.read_text() == namespace["WINDOW_LAYOUT_DUAL"] + "\n"
    assert namespace["read_window_layout"](layout_file) == (
        namespace["WINDOW_LAYOUT_DUAL"]
    )
    layout_file.write_bytes(b"\xff\xfe\x00")
    assert namespace["read_window_layout"](layout_file) == (
        namespace["WINDOW_LAYOUT_MANUAL"]
    )


class LayoutArea:
    def __init__(self, x, y, width, height):
        self.x = x
        self.y = y
        self.width = width
        self.height = height


class LayoutMonitor:
    def __init__(self, area, manufacturer, model, scale=1):
        self.area = LayoutArea(*area)
        self.manufacturer = manufacturer
        self.model = model
        self.scale = scale

    def get_workarea(self):
        return self.area

    def get_geometry(self):
        return self.area

    def get_scale_factor(self):
        return self.scale

    def get_manufacturer(self):
        return self.manufacturer

    def get_model(self):
        return self.model


class LayoutDisplay:
    def __init__(self):
        self.monitors = [
            LayoutMonitor((0, 0, 1440, 2560), "PHL", "HDMI-0"),
            LayoutMonitor((1506, 32, 2494, 1408), "AOC", "HDMI-1"),
        ]

    def get_primary_monitor(self):
        return self.monitors[1]

    def get_n_monitors(self):
        return len(self.monitors)

    def get_monitor(self, index):
        return self.monitors[index]


layout_display = LayoutDisplay()


class LayoutGdk:
    class Display:
        @staticmethod
        def get_default():
            return layout_display


assert namespace["gdk_monitor_workareas"](LayoutGdk) == [
    (1506, 32, 2494, 1408),
    (0, 0, 1440, 2560),
]
fit = namespace["fit_video_window_to_workarea"]
assert fit(
    (1506, 32, 2494, 1408),
    (1537, 32, 2432, 1408),
    (16, 9),
) == (1537, 32, 2432, 1408)
assert fit(
    (1506, 32, 2494, 1408),
    (1506, 32, 2494, 1408),
    (16, 9),
) == (1537, 32, 2432, 1408)
assert fit(
    (0, 0, 1440, 2560),
    (0, 0, 1000, 700),
    (16, 9),
) == (0, 855, 1440, 850)
# The learned remote shape replaces a fixed 16:9 surface: portrait, 21:9
# ultrawide and 16:10 hosts keep their proportions on the same work area.
assert fit((1506, 32, 2494, 1408), None, (9, 16)) == (2368, 32, 769, 1407)
assert fit((1506, 32, 2494, 1408), None, (3440, 1440)) == (
    1506, 194, 2494, 1084
)
assert fit((1506, 32, 2494, 1408), None, (1920, 1200)) == (
    1659, 32, 2188, 1407
)
# An unknown shape fills the work area and UU letterboxes the video.
assert fit((1506, 32, 2494, 1408), None, None) == (1506, 32, 2494, 1408)

client_aspect = namespace["window_client_aspect"]
assert client_aspect((0, 0, 1280, 760)) == (1280, 720)
assert client_aspect((0, 0, 612, 1128)) == (612, 1088)
assert client_aspect((0, 0, 2600, 520)) is None
assert client_aspect((0, 0, 100, 30)) is None
assert client_aspect(None) is None

monitor_descriptors = namespace["gdk_monitor_descriptors"](LayoutGdk)
assert [row["id"] for row in monitor_descriptors] == [
    "PHL|HDMI-0",
    "AOC|HDMI-1",
]

# GNOME's 200% scale halves GDK geometry; X11 window moves need device pixels.
unscaled_monitors = layout_display.monitors
layout_display.monitors = [
    LayoutMonitor((0, 0, 1280, 720), "AOC", "Q27", scale=2),
    LayoutMonitor((1280, 0, 1920, 1080), "DEL", "U2720Q", scale=2),
]
assert namespace["gdk_monitor_workareas"](LayoutGdk) == [
    (2560, 0, 3840, 2160),
    (0, 0, 2560, 1440),
]
assert [
    row["geometry"]
    for row in namespace["gdk_monitor_descriptors"](LayoutGdk)
] == [(0, 0, 2560, 1440), (2560, 0, 3840, 2160)]
layout_display.monitors = unscaled_monitors
assert [row["primary"] for row in monitor_descriptors] == [False, True]
assert namespace["monitor_menu_label"](monitor_descriptors[1]) == (
    "HDMI-1 — 2494×1408（主显示器）"
)

with tempfile.TemporaryDirectory() as state:
    selection_file = Path(state) / "controlled-monitor.json"
    assert namespace["read_monitor_selection"](selection_file) == "primary"
    namespace["write_monitor_selection"](
        selection_file, "PHL|HDMI-0"
    )
    assert namespace["read_monitor_selection"](selection_file) == (
        "PHL|HDMI-0"
    )


class LayoutX11:
    pid_atom = 1
    wm_state_atom = 2

    def __init__(self):
        self.moves = []
        self.accept_moves = True
        self.visible_windows = [0x300, 0x301, 0x302]
        # UU's startup viewers: a 16:9 video area plus its 40-pixel caption.
        self.rects = {
            0x300: (0, 0, 1280, 760),
            0x301: (0, 0, 1024, 616),
            0x302: (100, 100, 920, 680),
            0x303: (0, 0, 612, 1128),
            0x304: (0, 0, 2600, 520),
        }

    def windows(self):
        return self.visible_windows

    @staticmethod
    def window_class(window):
        if window in (0x300, 0x301, 0x302, 0x303, 0x304):
            return "gameviewer.exe", "gameviewer.exe"
        return "other.exe", "other.exe"

    @staticmethod
    def property_first_cardinal(window, atom):
        if window not in (0x300, 0x301, 0x302, 0x303, 0x304):
            return None
        return 4242 if atom == 1 else 1

    @staticmethod
    def window_title(window):
        return {
            0x300: "DESKTOP-PRIMARY",
            0x301: "DESKTOP-SECONDARY",
            0x302: "网易UU远程",
            0x303: "DESKTOP-PORTRAIT",
            0x304: "DESKTOP-ODD-SHAPE",
        }[window]

    def window_rect(self, window):
        return self.rects[window]

    def move_resize(self, window, x, y, width, height):
        self.moves.append((window, x, y, width, height))
        if self.accept_moves:
            self.rects[window] = (x, y, width, height)


class ImmediateGLib:
    @staticmethod
    def timeout_add(_delay, callback, *args):
        callback(*args)
        return 1


layout_x11 = LayoutX11()
layout_proxy = proxy_class.__new__(proxy_class)
layout_proxy.x11 = layout_x11
layout_proxy.Gdk = LayoutGdk
layout_proxy.GLib = ImmediateGLib
layout_proxy.prefix = "/mock-prefix"
layout_proxy.window_layout = namespace["WINDOW_LAYOUT_MANUAL"]
layout_proxy.window_layout_monitor_signature = ()
layout_proxy.window_layout_state = {}
layout_globals = namespace["mapped_remote_windows"].__globals__
original_layout_controller_processes = layout_globals["controller_processes"]
layout_globals["controller_processes"] = lambda *_args: {4242}
try:
    # The default layout must leave a manually moved or maximized XWayland
    # viewer completely under compositor/user control, even on a forced scan.
    assert layout_proxy.apply_window_layout(force=True) == 0
    assert layout_x11.moves == []

    layout_proxy.window_layout = namespace["WINDOW_LAYOUT_SINGLE"]
    assert layout_proxy.apply_window_layout(force=True) == 1
    assert layout_x11.moves == [
        (0x300, 1537, 32, 2432, 1408),
    ]
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout() == 0
    assert layout_x11.moves == []
    assert layout_proxy.window_layout_state[0x300][1] == (
        namespace["WINDOW_LAYOUT_SETTLED"]
    )

    # A temporary unmap/remap keeps the settled identity and must not trigger
    # another resize when the same X11 window returns.
    settled_state = layout_proxy.window_layout_state[0x300]
    layout_x11.visible_windows = []
    assert layout_proxy.apply_window_layout() == 0
    assert layout_proxy.window_layout_state[0x300] == settled_state
    layout_x11.visible_windows = [0x300, 0x301, 0x302]
    assert layout_proxy.apply_window_layout() == 0
    assert layout_x11.moves == []

    # If UU restores its startup geometry before verification, retry once and
    # stop as soon as the target geometry is observed.
    layout_proxy.window_layout_state[0x300] = (
        layout_proxy.window_layout_state[0x300][0],
        1,
        0.0,
    )
    layout_x11.rects[0x300] = (0, 0, 1280, 760)
    assert layout_proxy.apply_window_layout() == 1
    assert layout_x11.moves == [
        (0x300, 1537, 32, 2432, 1408),
    ]
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout() == 0
    assert layout_x11.moves == []

    # An already-correct outer window receives exactly one pulse to refresh a
    # stale mixed-orientation Qt canvas, then converges without another pulse.
    layout_proxy.window_layout_state.clear()
    assert layout_proxy.apply_window_layout(force=True) == 1
    assert layout_x11.moves == [
        (0x300, 1537, 32, 2431, 1408),
        (0x300, 1537, 32, 2432, 1408),
    ]
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout() == 0
    assert layout_x11.moves == []

    layout_proxy.window_layout = namespace["WINDOW_LAYOUT_DUAL"]
    layout_proxy.window_layout_state.clear()
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout(force=True) == 2
    assert layout_x11.moves == [
        # The primary window is already at its target and needs one pulse.
        (0x300, 1537, 32, 2431, 1408),
        (0x300, 1537, 32, 2432, 1408),
        # The secondary window differs, so one direct resize is sufficient.
        (0x301, 0, 855, 1440, 850),
    ]
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout() == 0
    assert layout_x11.moves == []

    # Failed moves retain the finite three-attempt ceiling.
    layout_proxy.window_layout = namespace["WINDOW_LAYOUT_SINGLE"]
    layout_proxy.window_layout_state.clear()
    layout_x11.rects[0x300] = (0, 0, 1280, 760)
    layout_x11.rects[0x301] = (0, 0, 1024, 616)
    layout_x11.accept_moves = False
    for attempt in range(namespace["WINDOW_LAYOUT_REFLOW_ATTEMPTS"]):
        if attempt:
            target, attempts, _next_attempt = (
                layout_proxy.window_layout_state[0x300]
            )
            layout_proxy.window_layout_state[0x300] = (
                target,
                attempts,
                0.0,
            )
        assert layout_proxy.apply_window_layout() == 1
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout() == 0
    assert layout_x11.moves == []

    # A portrait remote display keeps its own shape instead of a fixed 16:9
    # surface, even though UU opened its viewer narrower than 640 pixels.
    layout_x11.accept_moves = True
    layout_x11.visible_windows = [0x303]
    layout_proxy.window_layout_state.clear()
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout(force=True) == 1
    assert layout_x11.moves == [(0x303, 2368, 32, 769, 1407)]
    assert layout_proxy.window_layout_aspects[0x303] == (612, 1088)

    # The shape is learned once per viewer: a later re-layout keeps the
    # portrait proportions even if the window now reports another geometry.
    layout_x11.rects[0x303] = (0, 0, 1280, 760)
    layout_proxy.window_layout_state.clear()
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout(force=True) == 1
    assert layout_x11.moves == [(0x303, 2368, 32, 769, 1407)]

    # An implausible startup shape falls back to filling the work area.
    layout_x11.visible_windows = [0x304]
    layout_proxy.window_layout_state.clear()
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout(force=True) == 1
    assert layout_x11.moves == [(0x304, 1506, 32, 2494, 1408)]
    assert layout_proxy.window_layout_aspects[0x304] is None
finally:
    layout_globals["controller_processes"] = (
        original_layout_controller_processes
    )


class CompletedProcess:
    returncode = 0


class DummyIndicator:
    def __init__(self):
        self.statuses = []

    def set_status(self, status):
        self.statuses.append(status)


class ExitGtk:
    quit_calls = 0

    @classmethod
    def main_quit(cls):
        cls.quit_calls += 1


class ExitIndicatorStatus:
    PASSIVE = "passive"
    ACTIVE = "active"


class ExitAppIndicator:
    IndicatorStatus = ExitIndicatorStatus


with tempfile.TemporaryDirectory() as state:
    exiting = proxy_class.__new__(proxy_class)
    exiting.exit_in_progress = False
    exiting.indicator = DummyIndicator()
    exiting.AppIndicator3 = ExitAppIndicator
    exiting.launcher = "/mock/uu-remote-for-linux"
    exiting.log_file = Path(state) / "tray.log"
    exiting.Gtk = ExitGtk
    exiting.write_status = lambda *_args: None
    calls = []

    def fake_run(command, **kwargs):
        calls.append((command, kwargs))
        return CompletedProcess()

    exit_globals = exiting.request_full_exit.__globals__
    original_which = exit_globals["shutil_which"]
    exit_globals["shutil_which"] = lambda _command: "/mock/tool"
    try:
        with mock.patch.object(namespace["subprocess"], "run", fake_run):
            exiting.request_full_exit("test")
    finally:
        exit_globals["shutil_which"] = original_which

    systemd_run = next(
        command for command, _kwargs in calls if command[0] == "systemd-run"
    )
    assert "--setenv=UU_REMOTE_TRAY_EXIT_HELPER=1" in systemd_run
    assert systemd_run[-2:] == ["/mock/uu-remote-for-linux", "--stop"]
    assert ExitGtk.quit_calls == 1


class RunningProcess:
    def poll(self):
        return None


class DummyGtk:
    @staticmethod
    def main_quit():
        raise AssertionError("托盘不应在解码器选择期间退出")


instance = proxy_class.__new__(proxy_class)
instance.prefix = "/mock-prefix"
instance.decoder_process = RunningProcess()
instance.ever_saw_client = False
instance.client_missing_ticks = 0
instance.remove_legacy_icons = lambda: 0
instance.write_status = lambda *_args: None
instance.consume_client_quit_event = lambda: False
instance.controlled_session_state = lambda: "idle"
instance.controller_recovery_requested = lambda: False
instance.controller_restart_idle_confirmations = 0
instance.Gtk = DummyGtk()
original_prefix_processes = namespace["prefix_processes"]
namespace["prefix_processes"].__globals__["prefix_processes"] = (
    lambda *_args: set()
)
written_statuses = []


def record_status(status, detail=""):
    written_statuses.append((status, detail))
    instance.status_written = (status, detail, "idle")


try:
    assert instance.periodic_check()
    assert not instance.ever_saw_client
    assert instance.client_missing_ticks == 0

    # A session state that settles after startup is republished once with
    # the last status and detail, not only on the next tray event.
    instance.write_status = record_status
    instance.status_written = ("active", "原生托盘已注册", "unknown")
    assert instance.periodic_check()
    assert instance.periodic_check()
    assert written_statuses == [("active", "原生托盘已注册")]
finally:
    namespace["prefix_processes"].__globals__["prefix_processes"] = (
        original_prefix_processes
    )

with tempfile.TemporaryDirectory() as state:
    status_proxy = proxy_class.__new__(proxy_class)
    status_proxy.state_dir = Path(state)
    status_proxy.status_file = Path(state) / "tray-proxy-status"
    status_proxy.removed_icons = 0
    status_proxy.last_controlled_session_state = "idle"
    status_proxy.write_status("active", "ok")
    assert "controlled_session=idle\n" in status_proxy.status_file.read_text()
    assert status_proxy.status_written == ("active", "ok", "idle")

with tempfile.TemporaryDirectory() as prefix:
    environment = os.environ.copy()
    environment["WINEPREFIX"] = prefix
    child = subprocess.Popen(["sleep", "5"], env=environment)
    try:
        # A freshly exec'd process can briefly expose an empty /proc/PID/environ
        # even though Popen has returned.  The tray scans periodically, so the
        # test must wait for the same eventually-consistent kernel view instead
        # of treating the first read as synchronous.
        expected_prefix = namespace["canonical_path"](prefix)
        prefix_visible_deadline = time.monotonic() + 1.0
        prefix_visible = False
        while child.poll() is None:
            prefix_visible = namespace["process_matches_prefix"](
                child.pid, expected_prefix
            )
            if prefix_visible or time.monotonic() >= prefix_visible_deadline:
                break
            time.sleep(0.01)
        assert prefix_visible, {
            "poll": child.poll(),
            "environment": namespace["process_environment"](child.pid),
            "arguments": namespace["process_arguments"](child.pid),
        }
        assert not namespace["process_matches_prefix"](
            child.pid, namespace["canonical_path"](prefix + "-other")
        )
    finally:
        child.terminate()
        child.wait()

# Linux comm is an arbitrary 16-byte field, not guaranteed UTF-8.  A process
# with a non-text name must not crash the tray's global /proc scan.
invalid_name = subprocess.Popen(
    [
        sys.executable,
        "-c",
        "import ctypes,time; "
        "ctypes.CDLL(None).prctl(15, ctypes.c_char_p(b'\\xffuu'), 0, 0, 0); "
        "time.sleep(5)",
    ]
)
try:
    time.sleep(0.05)
    assert namespace["process_name"](invalid_name.pid).endswith("uu")
finally:
    invalid_name.terminate()
    invalid_name.wait()

with tempfile.TemporaryDirectory() as prefix, tempfile.TemporaryDirectory() as other:
    environment = os.environ.copy()
    environment["WINEPREFIX"] = prefix
    controller_id = "A537971D-B665-47E5-8B5B-DFF52C5CDE44"
    (Path(prefix) / controller_id).write_text("sleep 5\n")
    controller = subprocess.Popen(
        ["source=start.exe", controller_id],
        executable="/bin/bash",
        cwd=prefix,
        env=environment,
    )
    other_environment = os.environ.copy()
    other_environment["WINEPREFIX"] = other
    unrelated = subprocess.Popen(["sleep", "5"], env=other_environment)
    try:
        assert namespace["quiesce_controller"](prefix, 0)
        controller.wait(timeout=2)
        assert unrelated.poll() is None
    finally:
        if controller.poll() is None:
            controller.terminate()
            controller.wait()
        unrelated.terminate()
        unrelated.wait()

# The direct-launch source= form also needs proof of the loaded controller
# image. A generic UUID-bearing process in the same prefix must be excluded.
with tempfile.TemporaryDirectory() as prefix:
    environment = os.environ.copy()
    environment["WINEPREFIX"] = prefix
    controller_id = "A537971D-B665-47E5-8B5B-DFF52C5CDE44"
    (Path(prefix) / controller_id).write_text("sleep 5\n")
    child = subprocess.Popen(
        ["source=", controller_id], executable="/bin/bash", cwd=prefix,
        env=environment,
    )
    original_read_text = Path.read_text
    try:
        assert child.pid not in namespace["controller_processes"](prefix)
        expected = Path(prefix) / "drive_c/Program Files/Netease/GameViewer/bin/GameViewer.exe"
        def mapped_controller(path, *args, **kwargs):
            if str(path) == f"/proc/{child.pid}/maps":
                return f"140000000-140001000 r--p 00000000 fc:00 123 {expected}\n"
            return original_read_text(path, *args, **kwargs)
        with mock.patch.object(Path, "read_text", mapped_controller):
            assert child.pid in namespace["controller_processes"](prefix)
            assert child.pid not in namespace["controller_processes"](prefix + "-other")
            globals_ = namespace["controller_processes"].__globals__
            original_arguments = globals_["process_arguments"]
            try:
                globals_["process_arguments"] = lambda pid: (["source=system", "idle", "process", controller_id] if pid == child.pid else original_arguments(pid))
                assert child.pid in namespace["controller_processes"](prefix)
                assert child.pid not in namespace["controller_processes"](prefix + "-other")
            finally:
                globals_["process_arguments"] = original_arguments
    finally:
        child.terminate()
        child.wait()

# Exit-on-close observes withdrawn home windows, never minimized ones.
with tempfile.TemporaryDirectory() as prefix:
    setting = Path(prefix) / "drive_c/users/test/AppData/Local/GameViewer/setting.ini"
    setting.parent.mkdir(parents=True)
    setting.write_text("[settingCenter]\nCloseOption=1\n")
    probe = proxy_class.__new__(proxy_class)
    probe.prefix = prefix
    class HomeX11:
        pid_atom = 1
        wm_state_atom = 2
        state = 0
        def windows(self): return [10]
        def window_title(self, _window): return "网易UU远程"
        def property_first_cardinal(self, _window, atom):
            return 42 if atom == self.pid_atom else self.state
    probe.x11 = HomeX11()
    globals_ = namespace["controller_processes"].__globals__
    original = globals_["controller_processes"]
    try:
        globals_["controller_processes"] = lambda _prefix: {42}
        assert not probe.home_window_withdrawn()  # not shown yet
        probe.x11.state = 1
        assert not probe.home_window_withdrawn()
        probe.x11.state = 3
        assert not probe.home_window_withdrawn()  # minimized
        probe.x11.state = 0
        assert probe.home_window_withdrawn()
        setting.write_text("[settingCenter]\nCloseOption=0\n")
        assert not probe.home_window_withdrawn()  # background mode
    finally:
        globals_["controller_processes"] = original

# Controller recovery must not SIGKILL a different process that reused the
# controller PID after the graceful termination request.
with tempfile.TemporaryDirectory() as state:
    reused = proxy_class.__new__(proxy_class)
    reused.prefix = state
    reused.controller_recovery_reason = "test"
    reused.controller_restart_marker.parent.mkdir(parents=True)
    reused.controller_restart_marker.touch()
    reused.controlled_session_state = lambda: "idle"
    reused.write_status = lambda *_args: None
    reused.launcher = "/mock/uu-remote-for-linux"
    reused.command_log_handle = lambda: open(
        Path(state) / "recovery.log", "a", encoding="utf-8"
    )
    recovery_globals = reused.recover_controller.__globals__
    original_controller_processes = recovery_globals["controller_processes"]
    process_scans = iter(({4242}, set(), set()))
    recovery_globals["controller_processes"] = lambda *_args: next(
        process_scans
    )
    try:
        with (
            mock.patch.object(namespace["os"], "kill") as kill,
            mock.patch.object(namespace["subprocess"], "Popen") as popen,
        ):
            assert reused.recover_controller()
    finally:
        recovery_globals["controller_processes"] = (
            original_controller_processes
        )
    assert kill.call_args_list == [mock.call(4242, namespace["signal"].SIGTERM)]
    popen.assert_called_once()

with tempfile.TemporaryDirectory() as prefix:
    drive_c = Path(prefix) / "drive_c"
    drive_c.mkdir()
    request = drive_c / namespace["UPSTREAM_UPDATE_REQUEST_MARKER"]
    request.touch()
    assert namespace["upstream_update_handoff_active"](prefix)
    request.unlink()
    processing = drive_c / namespace["UPSTREAM_UPDATE_PROCESSING_MARKER"]
    processing.touch()
    assert namespace["upstream_update_handoff_active"](prefix)

with tempfile.TemporaryDirectory() as prefix:
    environment = os.environ.copy()
    environment["WINEPREFIX"] = prefix
    controller_id = "4B6671A6-B8EA-4597-A9B7-3EFA8575855F"
    (Path(prefix) / controller_id).write_text("sleep 5\n")
    child = subprocess.Popen(
        ["source=start.exe", controller_id],
        executable="/bin/bash",
        cwd=prefix,
        env=environment,
    )
    try:
        assert child.pid in namespace["controller_processes"](
            namespace["canonical_path"](prefix)
        )
    finally:
        child.terminate()
        child.wait()

with tempfile.TemporaryDirectory() as prefix:
    prefix_path = Path(prefix)
    setting = (
        prefix_path
        / "drive_c/users/test/AppData/Local/GameViewer/setting.ini"
    )
    log_dir = (
        prefix_path
        / "drive_c/Program Files/Netease/GameViewer/log/client/Log"
    )
    setting.parent.mkdir(parents=True)
    log_dir.mkdir(parents=True)
    setting.write_text("[settingCenter]\nCloseOption=1\n")
    assert namespace["exit_on_close_enabled"](prefix)

    marker = namespace["CLIENT_QUIT_MARKER"]
    client_log = log_dir / "log_20260731150000000_100.txt"
    client_log.write_text(f"old event {marker}\n")
    monitor = proxy_class.__new__(proxy_class)
    monitor.prefix = namespace["canonical_path"](prefix)
    monitor.client_log_path = None
    monitor.client_log_offset = 0
    monitor.client_log_tail = ""
    monitor.prime_client_quit_monitor()
    assert not monitor.consume_client_quit_event()

    split = len(marker) // 2
    with client_log.open("a") as log_handle:
        log_handle.write(marker[:split])
    assert not monitor.consume_client_quit_event()
    with client_log.open("a") as log_handle:
        log_handle.write(marker[split:] + "\n")
    assert monitor.consume_client_quit_event()

    exits = []
    monitor.decoder_process = None
    monitor.ever_saw_client = True
    monitor.client_missing_ticks = 0
    monitor.remove_legacy_icons = lambda: 0
    monitor.request_full_exit = lambda detail: exits.append(detail)
    monitor.controlled_session_state = lambda: "idle"
    monitor.controller_recovery_requested = lambda: False
    monitor.controller_restart_idle_confirmations = 0
    with client_log.open("a") as log_handle:
        log_handle.write(marker + "\n")
    update_request = (
        prefix_path
        / "drive_c"
        / namespace["UPSTREAM_UPDATE_REQUEST_MARKER"]
    )
    update_request.touch()
    original_prefix_processes = namespace["prefix_processes"]
    namespace["prefix_processes"].__globals__["prefix_processes"] = (
        lambda *_args: {1234}
    )
    try:
        assert monitor.periodic_check()
        assert not exits
        update_request.unlink()
        with client_log.open("a") as log_handle:
            log_handle.write(marker + "\n")
        assert not monitor.periodic_check()
        assert exits
    finally:
        namespace["prefix_processes"].__globals__["prefix_processes"] = (
            original_prefix_processes
        )

    setting.write_text("[settingCenter]\nCloseOption=0\n")
    assert not namespace["exit_on_close_enabled"](prefix)

    # Dock Quit ends the controller even when window-close means hide-to-tray.
    # An explicit quit marker and actual process disappearance both need full
    # cleanup; a still-running, hidden controller must remain available.
    globals_ = monitor.periodic_check.__globals__
    original_controllers = globals_["controller_processes"]
    monitor.write_status = lambda *_args: None
    monitor.consume_client_quit_event = lambda: False
    monitor.client_missing_ticks = 0
    exits.clear()
    try:
        globals_["controller_processes"] = lambda _prefix: {1234}
        assert monitor.periodic_check()
        assert not exits
        globals_["controller_processes"] = lambda _prefix: set()
        for _ in range(namespace["CLIENT_MISSING_GRACE_TICKS"] - 1):
            assert monitor.periodic_check()
        assert not monitor.periodic_check()
        assert len(exits) == 1
        exits.clear()
        monitor.consume_client_quit_event = lambda: True
        globals_["controller_processes"] = lambda _prefix: {1234}
        assert not monitor.periodic_check()
        assert len(exits) == 1
    finally:
        globals_["controller_processes"] = original_controllers

with tempfile.TemporaryDirectory() as prefix, tempfile.TemporaryDirectory() as state:
    recovery = proxy_class.__new__(proxy_class)
    recovery.prefix = namespace["canonical_path"](prefix)
    recovery.launcher = "/mock/uu-remote-for-linux"
    recovery.log_file = Path(state) / "tray.log"
    recovery.client_missing_ticks = 3
    recovery.decoder_process = None
    recovery.ever_saw_client = True
    recovery.remove_legacy_icons = lambda: 0
    recovery.consume_client_quit_event = lambda: False
    recovery.server_log_path = None
    recovery.server_log_offset = 0
    recovery.server_log_tail = ""
    recovery.server_session_states = {}
    recovery.last_controlled_session_state = "unknown"
    recovery.controller_restart_idle_confirmations = 0
    recovery.controller_recovery_suppressed = 0
    recovery.controller_recovery_deferred = 0
    recovery.controller_recovery_reason = ""
    recovery.controller_recovery_defer_detail = ""
    recovery.write_status = lambda *_args: None
    server_log_dir = recovery.server_connection_log_dir
    server_log_dir.mkdir(parents=True)
    (server_log_dir / "connection_log_20260803000000000_1.txt").write_text(
        "server ready\n"
    )
    marker = recovery.controller_restart_marker
    marker.parent.mkdir(parents=True, exist_ok=True)
    valid_request = (
        "reason=event-loop-livelock\n"
        "hook_version=16\n"
        "guard_evidence=0\n"
        "sticky_null_evidence=1\n"
        "window_generation=7\n"
    )
    marker.write_text(valid_request)
    assert recovery.controller_recovery_requested()
    assert recovery.controlled_session_state() == "idle"

    recovery_globals = recovery.recover_controller.__globals__
    original_controller_processes = recovery_globals["controller_processes"]
    recovery_globals["controller_processes"] = lambda *_args: set()
    try:
        with mock.patch.object(namespace["subprocess"], "Popen") as popen:
            assert recovery.periodic_check()
            assert recovery.controller_restart_idle_confirmations == 1
            popen.assert_not_called()
            assert recovery.periodic_check()
            popen.assert_called_once()
    finally:
        recovery_globals["controller_processes"] = original_controller_processes
    assert not marker.exists()
    assert recovery.client_missing_ticks == 0
    assert recovery.controller_recovery_grace_until > time.monotonic()

    focus_status = recovery.controller_focus_status
    focus_status.write_text(
        "[hook]\n"
        "pid=4242\n"
        "version=18\n"
        "[ui_health]\n"
        "pings_sent=41\n"
        "pings_acked=39\n"
        "target_generation=9\n"
        "consecutive_timeouts=2\n"
        "hard_stalls_detected=1\n"
    )
    marker.write_text(
        "pid=4242\n"
        "reason=ui-hard-stall\n"
        "hook_version=18\n"
        "guard_evidence=0\n"
        "sticky_null_evidence=0\n"
        "ui_timeout_evidence=1\n"
        "consecutive_timeouts=2\n"
        "pings_sent=41\n"
        "pings_acked=39\n"
        "window_generation=9\n"
    )
    assert recovery.controller_recovery_requested()
    assert recovery.controller_recovery_reason == "ui-hard-stall"
    with mock.patch.object(namespace["subprocess"], "Popen") as popen:
        recovery.show_main_window()
        popen.assert_not_called()
    assert recovery.controller_restart_idle_confirmations == 1
    marker.unlink()

    marker.write_text(
        "pid=4243\n"
        "reason=ui-hard-stall\n"
        "hook_version=18\n"
        "ui_timeout_evidence=1\n"
        "consecutive_timeouts=2\n"
        "pings_sent=41\n"
        "pings_acked=39\n"
        "window_generation=9\n"
    )
    assert not recovery.controller_recovery_requested()
    assert not marker.exists()

    marker.write_text(
        "pid=4242\n"
        "reason=ui-hard-stall\n"
        "hook_version=18\n"
        "ui_timeout_evidence=1\n"
        "consecutive_timeouts=1\n"
        "pings_sent=41\n"
        "pings_acked=39\n"
        "window_generation=9\n"
    )
    assert not recovery.controller_recovery_requested()
    assert not marker.exists()

    marker.write_text("reason=stale\n")
    stale = time.time() - (
        namespace["CONTROLLER_RESTART_REQUEST_MAX_AGE_SECONDS"] + 5
    )
    os.utime(marker, (stale, stale))
    assert not recovery.controller_recovery_requested()
    assert not marker.exists()

    marker.write_text(
        "reason=ui-message-timeout\n"
        "hook_version=14\n"
    )
    assert not recovery.controller_recovery_requested()
    assert not marker.exists()

with tempfile.TemporaryDirectory() as prefix, tempfile.TemporaryDirectory() as state:
    active = proxy_class.__new__(proxy_class)
    active.prefix = namespace["canonical_path"](prefix)
    active.launcher = "/mock/uu-remote-for-linux"
    active.log_file = Path(state) / "tray.log"
    active.client_missing_ticks = 0
    active.server_log_path = None
    active.server_log_offset = 0
    active.server_log_tail = ""
    active.server_session_states = {}
    active.last_controlled_session_state = "unknown"
    active.controller_restart_idle_confirmations = 0
    active.controller_recovery_suppressed = 0
    active.controller_recovery_deferred = 0
    active.controller_recovery_reason = ""
    active.controller_recovery_defer_detail = ""
    active.write_status = lambda *_args: None
    server_log_dir = active.server_connection_log_dir
    server_log_dir.mkdir(parents=True)
    server_log = server_log_dir / "connection_log_20260803000000000_1.txt"
    server_log.write_text(
        "[2026-08-03 22:24:02.261][I][controlled][rtc] "
        "Session: 123 Old state: have-remote-offer New state: stable\n"
    )
    marker = active.controller_restart_marker
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(valid_request)
    assert active.controlled_session_state() == "active"
    with mock.patch.object(namespace["subprocess"], "Popen") as popen:
        assert not active.recover_controller()
        popen.assert_not_called()
    assert marker.exists()
    assert active.controller_recovery_deferred == 1
    assert active.controller_recovery_suppressed == 0

    with server_log.open("a") as log_handle:
        log_handle.write(
            "[2026-08-03 22:24:17.627][I][controlled][rtc] "
            "Session: 123 Old state: stable New state: closed\n"
        )
    assert active.controlled_session_state() == "idle"

# UU 4.43+ writes only encrypted .slog files; the session estimate then uses
# remote input and GameViewerServer's capture/encode load.
assert namespace["windows_image_name"](
    r"C:\Program Files\Netease\GameViewer\bin\GameViewerServer.exe"
) == "GameViewerServer.exe"
assert namespace["stat_cpu_ticks"](
    "1234 (a) b) S 1 1 1 0 -1 4194560 10 0 0 0 70 30 0 0 20 0"
) == 100
assert namespace["stat_cpu_ticks"]("1234 (truncated) S 1") is None
assert isinstance(namespace["process_cpu_ticks"](os.getpid()), int)

with tempfile.TemporaryDirectory() as prefix, tempfile.TemporaryDirectory() as state:
    (Path(state) / "input-bridge-status.json").write_text(
        '{"pid": 41, "mouse_packets": 2, "keyboard_packets": 3}'
    )
    assert namespace["input_bridge_activity"](Path(state)) == (41, 5)
    (Path(state) / "input-bridge-status.json").write_text("{")
    assert namespace["input_bridge_activity"](Path(state)) is None

    estimate_globals = proxy_class.controlled_session_activity_state.__globals__
    clock = [100.0]
    servers = {7001}
    cpu = {7001: 1000}
    inputs = [(41, 0)]
    patched = {
        "server_processes": lambda _prefix: set(servers),
        "process_cpu_ticks": lambda pid: cpu.get(pid),
        "input_bridge_activity": lambda _state_dir: inputs[0],
    }
    originals = {name: estimate_globals[name] for name in patched}
    estimate_globals.update(patched)
    hz = os.sysconf("SC_CLK_TCK")
    quiet_samples = int(
        namespace["CONTROLLED_SESSION_QUIET_SECONDS"]
        // namespace["CONTROLLED_SESSION_SAMPLE_SECONDS"]
    )

    def advance(seconds, cpu_percent=1.0):
        clock[0] += seconds
        for pid in cpu:
            cpu[pid] += round(hz * seconds * cpu_percent / 100)

    def new_estimate():
        instance = proxy_class.__new__(proxy_class)
        instance.prefix = namespace["canonical_path"](prefix)
        instance.state_dir = Path(state)
        return instance

    try:
        with mock.patch.object(
            namespace["time"], "monotonic", lambda: clock[0]
        ):
            estimate = new_estimate()
            # Warming up: a full quiet window has not been observed yet.
            assert estimate.controlled_session_state() == "unknown"
            advance(2)
            assert estimate.controlled_session_state() == "unknown"
            # A whole quiet window at idle load (1% of one core) is idle.
            for _ in range(quiet_samples):
                advance(2)
                estimate.controlled_session_state()
            assert estimate.controlled_session_state() == "idle"
            assert estimate.last_controlled_session_state == "idle"

            # Remote input keeps the session active for a full quiet window;
            # samples between the 2 s intervals reuse the cached state.
            inputs[0] = (41, 3)
            advance(2)
            assert estimate.controlled_session_state() == "active"
            advance(0.5)
            assert estimate.controlled_session_state() == "active"
            for _ in range(quiet_samples - 1):
                advance(2)
                assert estimate.controlled_session_state() == "active"
            advance(2)
            assert estimate.controlled_session_state() == "idle"

            # A passive viewer sends no input but keeps capture/encode busy.
            advance(2, cpu_percent=40)
            assert estimate.controlled_session_state() == "active"

            # Without GameViewerServer nobody can control this desktop.
            servers.clear()
            alone = new_estimate()
            assert alone.controlled_session_state() == "idle"
            servers.add(7001)

            # Neither signal readable stays unknown even after warming up.
            cpu.clear()
            inputs[0] = None
            blind = new_estimate()
            for _ in range(quiet_samples + 1):
                blind.controlled_session_state()
                advance(2)
            assert blind.controlled_session_state() == "unknown"
            cpu[7001] = 1000
            inputs[0] = (41, 3)

            # Text logs left by an older UU are ignored once .slog is newer.
            stale = new_estimate()
            log_dir = stale.server_connection_log_dir
            log_dir.mkdir(parents=True)
            text_log = log_dir / "connection_log_20260803000000000_1.txt"
            text_log.write_text(
                "Session: 9 Old state: have-remote-offer New state: stable\n"
            )
            slog = log_dir / "connection_log_controlled.slog"
            slog.write_bytes(b"\0")
            os.utime(text_log, (1_000_000, 1_000_000))
            assert stale.latest_server_connection_log() is None
            assert stale.controlled_session_state() == "unknown"
            os.utime(slog, (900_000, 900_000))
            assert stale.latest_server_connection_log() == text_log
    finally:
        estimate_globals.update(originals)


class RepaintX11:
    pid_atom = 1
    wm_state_atom = 2

    def __init__(self):
        self.resizes = []
        self.title = "网易UU远程"
        self.size = (920, 680)
        self.mapped = True
        self.closed = False

    @staticmethod
    def windows():
        return [0x100, 0x200]

    @staticmethod
    def window_class(window):
        if window == 0x200:
            return "gameviewer.exe", "gameviewer.exe"
        return "other.exe", "other.exe"

    def property_first_cardinal(self, window, atom):
        if window != 0x200:
            return None
        return 4242 if atom == 1 else (1 if self.mapped else 0)

    def geometry(self, window):
        return self.size if window == 0x200 else (10, 10)

    def window_title(self, window):
        return self.title if window == 0x200 else "other"

    def resize(self, window, width, height):
        self.resizes.append((window, width, height))

    def close(self):
        self.closed = True

repaint_x11 = RepaintX11()
repaint_globals = namespace["repaint_home_window"].__globals__
original_controller_processes = repaint_globals["controller_processes"]
original_x11 = repaint_globals["X11"]
repaint_globals["controller_processes"] = lambda *_args: {4242}
repaint_globals["X11"] = lambda *_args: repaint_x11
try:
    with mock.patch.object(namespace["time"], "sleep") as sleep:
        assert namespace["repaint_home_window"](
            "/mock-prefix", namespace["HOME_REPAINT_DELAY_MS"]
        )
    assert [call.args[0] for call in sleep.call_args_list] == [2.5, 0.08]
    assert repaint_x11.resizes == [
        (0x200, 921, 681),
        (0x200, 920, 680),
    ]
    assert repaint_x11.closed
    repaint_x11.size = (520, 360)
    assert namespace["mapped_home_window"](repaint_x11, "/mock-prefix") == (0x200, 520, 360)
    repaint_x11.title = "GameViewer"
    repaint_x11.size = (352, 245)
    assert namespace["mapped_home_window"](repaint_x11, "/mock-prefix") is None
    repaint_x11.title = "Remote desktop - another device"
    repaint_x11.size = (1280, 720)
    assert namespace["mapped_home_window"](repaint_x11, "/mock-prefix") is None
    repaint_x11.title = "网易UU远程"
    repaint_x11.mapped = False
    repaint_x11.resizes = []
    clock = [0.0]
    def advance(seconds):
        clock[0] += seconds
    with mock.patch.object(namespace["time"], "sleep", side_effect=advance), mock.patch.object(namespace["time"], "monotonic", side_effect=lambda: clock[0]):
        assert not namespace["repaint_home_window"]("/mock-prefix", 0, 250)
    assert not repaint_x11.resizes
    assert clock[0] == 0.25
    # A startup window becoming the real home during the wait is accepted.
    clock[0] = 0
    def show_after_delay(seconds):
        advance(seconds)
        if clock[0] >= 0.2:
            repaint_x11.mapped = True
    with mock.patch.object(namespace["time"], "sleep", side_effect=show_after_delay), mock.patch.object(namespace["time"], "monotonic", side_effect=lambda: clock[0]):
        assert namespace["repaint_home_window"]("/mock-prefix", 0, 1000)
finally:
    repaint_globals["controller_processes"] = original_controller_processes
    repaint_globals["X11"] = original_x11

display = os.environ.get("DISPLAY")
if display:
    x11 = namespace["X11"](display)
    assert x11.root
    x11.close()
PY

printf '原生托盘代理检查通过。\n'
