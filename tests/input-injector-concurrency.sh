#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root

/usr/bin/python3 - "$project_root/src/uu-remote-input-injector.c" <<'PY'
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


def compact(text: str) -> str:
    return re.sub(r"\s+", "", text)


watch = compact(function_body("watch_process"))
watch_module = compact(function_body("watch_process_with_module"))
inject_once = compact(function_body("inject_once"))
launch = compact(function_body("launch_suspended"))
initialize = compact(function_body("initialize_remote_hook"))
inject = compact(function_body("inject_library"))
worker = compact(function_body("remote_operation_worker"))
start = compact(function_body("start_remote_operation"))
poll = compact(function_body("poll_remote_operation"))

assert "INFINITE" not in watch
assert "INFINITE" not in watch_module
assert "start_remote_operation(pid,dll_path,REMOTE_OPERATION_INJECT)" in watch
assert "start_remote_operation(pid,dll_path,REMOTE_OPERATION_INITIALIZE)" in watch
assert "poll_remote_operation(&operation,&operation_success,&operation_error)" in watch
assert "start_remote_operation(state->pid,dll_path,REMOTE_OPERATION_INJECT)" in watch_module
assert "poll_remote_operation(&state->operation,&operation_success,&operation_error)" in watch_module
assert "if(watched[index].operation){++index;continue;}" in watch_module
assert "inject_library(operation->pid,operation->dll_path,FALSE,INFINITE)" in worker
assert "initialize_remote_hook(operation->pid,operation->dll_path,FALSE,INFINITE)" in worker
assert "CreateThread(NULL,0,remote_operation_worker,operation,0,NULL)" in start
assert "WaitForSingleObject(operation->worker,0)" in poll
assert "*operation_pointer=NULL" in poll
assert "DIRECT_INJECTION_WAIT_MS" in inject_once
assert "DIRECT_INJECTION_WAIT_MS" in launch
assert "WaitForSingleObject(thread,wait_timeout)" in initialize
assert "WaitForSingleObject(thread,wait_timeout)" in inject
print("PASS input injector single-outstanding-operation policy")
PY
