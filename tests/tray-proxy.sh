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
assert '"--select-decoder-and-restart"' in source
assert "self.select_decoder_and_restart" in source
namespace = runpy.run_path(proxy)
proxy_class = namespace["TrayProxy"]


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

display = os.environ.get("DISPLAY")
if display:
    x11 = namespace["X11"](display)
    assert x11.root
    x11.close()
PY

printf '原生托盘代理检查通过。\n'
