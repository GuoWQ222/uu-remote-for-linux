#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly lifecycle="$project_root/third_party/nvenc/uu-remote-nvenc-lifecycle.h"
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

/usr/bin/python3 - "$lifecycle" <<'PY'
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")


def function_body(name: str) -> str:
    match = re.search(rf"static void {name}\([^{{]+\)\s*\{{", source)
    assert match, f"missing lifecycle function: {name}"
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"unterminated lifecycle function: {name}")


for name in ("uu_cancel_encoder_destroy", "uu_retire_destroyed_encoder"):
    body = function_body(name)
    unlock = body.index("pthread_mutex_unlock(&encoder->mutex)")
    bridge_lock = body.index("pthread_mutex_lock(&uu_bridge_mutex)")
    assert unlock < bridge_lock, f"reverse encoder -> bridge lock order in {name}"
print("PASS NVENC encoder bridge-to-encoder lock-order policy")
PY

cc -std=c11 -Wall -Wextra -Werror -pthread \
    -o "$test_root/nvenc-lifecycle" \
    "$project_root/tests/nvenc-lifecycle.c"
timeout 10s "$test_root/nvenc-lifecycle"

if [[ ${UU_REMOTE_NVENC_TSAN:-0} == 1 ]]; then
    # Keep the sanitizer binary non-PIE so GCC ThreadSanitizer can reserve its
    # shadow address space more reliably on current Ubuntu kernels.  TSan is
    # opt-in because kernel ASLR can still terminate it before test code runs.
    cc -std=c11 -O1 -g -Wall -Wextra -Werror -pthread \
        -fno-omit-frame-pointer -fsanitize=thread -no-pie \
        -o "$test_root/nvenc-lifecycle-tsan" \
        "$project_root/tests/nvenc-lifecycle.c"
    TSAN_OPTIONS=halt_on_error=1:history_size=7 \
        timeout 20s "$test_root/nvenc-lifecycle-tsan"
fi
