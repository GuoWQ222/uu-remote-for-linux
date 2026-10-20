#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
pkg-config --exists libeis-1.0
# shellcheck disable=SC2046
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    $(pkg-config --cflags libeis-1.0) \
    "$project_root/tests/eis-pointer-server.c" \
    -o "$temporary/server" $(pkg-config --libs libeis-1.0)
python3 "$project_root/tests/eis-pointer.py" "$temporary/server"
