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
    with client_log.open("a") as log_handle:
        log_handle.write(marker + "\n")
    original_prefix_processes = namespace["prefix_processes"]
    namespace["prefix_processes"].__globals__["prefix_processes"] = (
        lambda *_args: {1234}
    )
    try:
        assert not monitor.periodic_check()
        assert exits
    finally:
        namespace["prefix_processes"].__globals__["prefix_processes"] = (
            original_prefix_processes
        )

    setting.write_text("[settingCenter]\nCloseOption=0\n")
    assert not namespace["exit_on_close_enabled"](prefix)

display = os.environ.get("DISPLAY")
if display:
    x11 = namespace["X11"](display)
    assert x11.root
    x11.close()
PY

printf '原生托盘代理检查通过。\n'
