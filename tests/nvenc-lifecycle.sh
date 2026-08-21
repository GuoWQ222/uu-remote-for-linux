#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

cc -std=c11 -Wall -Wextra -Werror -pthread \
    -o "$test_root/nvenc-lifecycle" \
    "$project_root/tests/nvenc-lifecycle.c"
timeout 10s "$test_root/nvenc-lifecycle"
