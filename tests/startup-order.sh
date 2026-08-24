#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root

/usr/bin/python3 - "$project_root/bin/uu-remote-for-linux" <<'PY'
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")


def function_body(name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"missing function: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {name}")


runtime = source[source.index("if ! start_input_bridge") :]
ordered_calls = [
    "start_input_bridge",
    "start_input_injector",
    "ensure_service_running",
    'ensure_wine_image_running "$healthd_runtime_unit"',
    'ensure_wine_image_running "$server_runtime_unit"',
    "start_focus_injector",
    "start_systemd_client_launch",
    "wait_input_hook_active",
]
positions = [runtime.index(call) for call in ordered_calls]
assert positions == sorted(positions), (ordered_calls, positions)

service = function_body("ensure_service_running")
assert service.index("service_running && return") < service.index("run_logged")

hook_check = function_body("input_hook_active")
assert '"state": "active"' in hook_check
assert "hook_pid" in hook_check
assert "hook_version" in hook_check

hook_wait = function_body("wait_input_hook_active")
assert "input_hook_active && return 0" in hook_wait
assert '"$input_bridge" status' not in hook_wait

defer = source[source.index("defer_startup_update_to_watcher=false") :]
defer_condition = defer[: defer.index(
    "if $startup_update_requested && ! $defer_startup_update_to_watcher"
)]
assert "$autostart_launch" not in defer_condition
assert "UU_REMOTE_DISABLE_UPDATE_WATCHER" in defer_condition
assert '[[ -x $update_bridge ]]' in defer
assert '"$update_bridge_initial_delay"' in source
assert 'UU_REMOTE_UPDATE_BRIDGE_INITIAL_DELAY:-60' in source

print("PASS safe startup dependency order")
PY
