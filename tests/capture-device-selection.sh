#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
/usr/bin/python3 "$project_root/tests/capture-device-selection.py"
if ! pkg-config --exists libpipewire-0.3 libspa-0.2 libdrm egl glesv2; then
    printf 'SKIP native CPU capture test (development packages unavailable)\n'
    exit 0
fi
temporary_dir=$(mktemp -d)
trap 'rm -rf -- "$temporary_dir"' EXIT
# shellcheck disable=SC2046
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    $(pkg-config --cflags libpipewire-0.3 libspa-0.2 libdrm egl glesv2) \
    "$project_root/tests/capture-cpu.c" -o "$temporary_dir/cpu-test" \
    $(pkg-config --libs libpipewire-0.3 libspa-0.2 libdrm egl glesv2)
"$temporary_dir/cpu-test"
# shellcheck disable=SC2046
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    $(pkg-config --cflags egl) "$project_root/tests/egl-device-order.c" \
    -o "$temporary_dir/egl-test" $(pkg-config --libs egl)
"$temporary_dir/egl-test"
