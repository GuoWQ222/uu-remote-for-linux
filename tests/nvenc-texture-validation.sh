#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
    -o "$test_root/nvenc-texture-validation" \
    "$project_root/tests/nvenc-texture-validation.c"
"$test_root/nvenc-texture-validation"
