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
import tempfile
import time
from unittest import mock

proxy = os.environ["PROXY_PATH"]
source = Path(proxy).read_text()
ast.parse(source, filename=proxy)
assert '"选择解码器…"' in source
assert '"远控窗口布局"' in source
assert '"单窗口（主屏）"' in source
assert '"双窗口（每屏一个）"' in source
assert '"--select-decoder-and-restart"' in source
assert "self.select_decoder_and_restart" in source
namespace = runpy.run_path(proxy)
proxy_class = namespace["TrayProxy"]

with tempfile.TemporaryDirectory() as state:
    layout_file = Path(state) / "window-layout"
    assert namespace["read_window_layout"](layout_file) == (
        namespace["WINDOW_LAYOUT_SINGLE"]
    )
    namespace["write_window_layout"](
        layout_file, namespace["WINDOW_LAYOUT_DUAL"]
    )
    assert layout_file.read_text() == namespace["WINDOW_LAYOUT_DUAL"] + "\n"
    assert namespace["read_window_layout"](layout_file) == (
        namespace["WINDOW_LAYOUT_DUAL"]
    )


class LayoutArea:
    def __init__(self, x, y, width, height):
        self.x = x
        self.y = y
        self.width = width
        self.height = height


class LayoutMonitor:
    def __init__(self, area):
        self.area = LayoutArea(*area)

    def get_workarea(self):
        return self.area


class LayoutDisplay:
    def __init__(self):
        self.monitors = [
            LayoutMonitor((0, 0, 1440, 2560)),
            LayoutMonitor((1506, 32, 2494, 1408)),
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


class LayoutX11:
    pid_atom = 1
    wm_state_atom = 2

    def __init__(self):
        self.moves = []

    @staticmethod
    def windows():
        return [0x300, 0x301, 0x302]

    @staticmethod
    def window_class(window):
        if window in (0x300, 0x301, 0x302):
            return "gameviewer.exe", "gameviewer.exe"
        return "other.exe", "other.exe"

    @staticmethod
    def property_first_cardinal(window, atom):
        if window not in (0x300, 0x301, 0x302):
            return None
        return 4242 if atom == 1 else 1

    @staticmethod
    def window_title(window):
        return {
            0x300: "DESKTOP-PRIMARY",
            0x301: "DESKTOP-SECONDARY",
            0x302: "网易UU远程",
        }[window]

    @staticmethod
    def window_rect(window):
        return {
            0x300: (0, 0, 1200, 800),
            0x301: (0, 0, 1000, 700),
            0x302: (100, 100, 920, 680),
        }[window]

    def move_resize(self, window, x, y, width, height):
        self.moves.append((window, x, y, width, height))


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
layout_proxy.window_layout = namespace["WINDOW_LAYOUT_SINGLE"]
layout_proxy.window_layout_monitor_signature = ()
layout_proxy.window_layout_state = {}
layout_globals = namespace["mapped_remote_windows"].__globals__
original_layout_controller_processes = layout_globals["controller_processes"]
layout_globals["controller_processes"] = lambda *_args: {4242}
try:
    assert layout_proxy.apply_window_layout(force=True) == 1
    assert layout_x11.moves == [
        (0x300, 1506, 32, 2493, 1408),
        (0x300, 1506, 32, 2494, 1408),
    ]
    layout_proxy.window_layout = namespace["WINDOW_LAYOUT_DUAL"]
    layout_proxy.window_layout_state.clear()
    layout_x11.moves.clear()
    assert layout_proxy.apply_window_layout(force=True) == 2
    assert layout_x11.moves == [
        (0x300, 1506, 32, 2493, 1408),
        (0x300, 1506, 32, 2494, 1408),
        (0x301, 0, 0, 1439, 2560),
        (0x301, 0, 0, 1440, 2560),
    ]
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
try:
    assert instance.periodic_check()
    assert not instance.ever_saw_client
    assert instance.client_missing_ticks == 0
finally:
    namespace["prefix_processes"].__globals__["prefix_processes"] = (
        original_prefix_processes
    )

with tempfile.TemporaryDirectory() as prefix:
    environment = os.environ.copy()
    environment["WINEPREFIX"] = prefix
    child = subprocess.Popen(["sleep", "5"], env=environment)
    try:
        assert namespace["process_matches_prefix"](
            child.pid, namespace["canonical_path"](prefix)
        )
        assert not namespace["process_matches_prefix"](
            child.pid, namespace["canonical_path"](prefix + "-other")
        )
    finally:
        child.terminate()
        child.wait()

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


class RepaintX11:
    pid_atom = 1
    wm_state_atom = 2

    def __init__(self):
        self.resizes = []

    @staticmethod
    def windows():
        return [0x100, 0x200]

    @staticmethod
    def window_class(window):
        if window == 0x200:
            return "gameviewer.exe", "gameviewer.exe"
        return "other.exe", "other.exe"

    @staticmethod
    def property_first_cardinal(window, atom):
        if window != 0x200:
            return None
        return 4242 if atom == 1 else 1

    @staticmethod
    def geometry(window):
        return (920, 680) if window == 0x200 else (10, 10)

    def resize(self, window, width, height):
        self.resizes.append((window, width, height))

    def close(self):
        pass

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
