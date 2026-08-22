#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-frame-helper.c"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so"
readonly compiler="${CC:-cc}"

command -v "$compiler" >/dev/null 2>&1 || {
    printf '缺少 C 编译器：%s\n' "$compiler" >&2
    exit 1
}

temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/uu-remote-frame-helper.XXXXXX")
temporary="$temporary_dir/uu-remote-frame-helper.so"
trap 'rm -rf -- "$temporary_dir"' EXIT

"$compiler" \
    -std=c11 \
    -O3 \
    -fPIC \
    -fvisibility=hidden \
    -Wall \
    -Wextra \
    -Werror \
    -Wl,--as-needed \
    -Wl,--no-undefined \
    -Wl,-z,relro,-z,now \
    -shared \
    -o "$temporary" \
    "$source_file"
strip --strip-unneeded "$temporary"
install -Dm0755 "$temporary" "$output_file"
/usr/bin/python3 - "$output_file" <<'PY'
import ctypes
import sys

library = ctypes.CDLL(sys.argv[1])
library.uu_frame_helper_self_test.restype = ctypes.c_uint32
if library.uu_frame_helper_self_test() != 1:
    raise SystemExit("native frame helper self-test failed")
PY
printf '%s\n' "$output_file"
sha256sum "$output_file"
