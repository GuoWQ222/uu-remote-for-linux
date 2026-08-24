#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly iterations="${UU_REMOTE_FRAME_STRESS_ITERATIONS:-2000}"
readonly full_frames="${UU_REMOTE_FRAME_STRESS_FULL_FRAMES:-4}"
readonly threads="${UU_REMOTE_FRAME_STRESS_THREADS:-4}"
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

for value in "$iterations" "$full_frames" "$threads"; do
    [[ $value =~ ^[1-9][0-9]*$ ]] || {
        printf '无效的帧压力测试参数：%s\n' "$value" >&2
        exit 2
    }
done

cc \
    -std=c11 \
    -O1 \
    -g \
    -Wall \
    -Wextra \
    -Werror \
    -fno-omit-frame-pointer \
    -fsanitize=address,undefined \
    -pthread \
    -o "$test_root/frame-helper-stress" \
    "$project_root/tests/frame-helper-stress.c" \
    "$project_root/src/uu-remote-frame-helper.c"

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:strict_string_checks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
timeout 180s "$test_root/frame-helper-stress" \
    "$iterations" "$full_frames" "$threads"
