#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly policy="$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy"

test_root=$(mktemp -d)
watch_pid=""
cleanup() {
    if [[ -n $watch_pid ]]; then
        kill "$watch_pid" >/dev/null 2>&1 || true
        wait "$watch_pid" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT

cache="$test_root/encoder-cache.json"
backup="$test_root/encoder-cache.backup.json"
enabled="$test_root/hwencode-enabled"
lock="$test_root/policy.lock"

printf '%s\n' \
    '{"encoder_capabilities":[{"video_codec":1,"codec_impl":0,"device_id":8578,"adapter_id":1012,"width":"3840","height":2160,"bit_depth":8}]}' \
    >"$cache"
if "$policy" verify "$cache" "$backup" 8578 1012 \
    >"$test_root/verify.out" 2>"$test_root/verify.err"; then
    printf '错误字段类型被 NVENC 策略意外接受。\n' >&2
    exit 1
fi
grep -q 'width 字段不是整数' "$test_root/verify.err"
if grep -q 'Traceback' "$test_root/verify.err"; then
    printf '错误字段类型触发了 Python traceback。\n' >&2
    exit 1
fi

: >"$enabled"
"$policy" watch "$cache" "$backup" 8578 1012 "$enabled" "$lock" \
    >"$test_root/watch.out" 2>"$test_root/watch.err" &
watch_pid=$!
sleep 1
kill -0 "$watch_pid"
if grep -q 'Traceback' "$test_root/watch.err"; then
    printf 'watcher 因错误字段类型产生了 Python traceback。\n' >&2
    exit 1
fi
error_count=$(grep -c 'width 字段不是整数' "$test_root/watch.err" || true)
if ((error_count < 1 || error_count > 2)); then
    printf 'watcher 没有对同一损坏缓存进行稳定限流：%s\n' \
        "$error_count" >&2
    exit 1
fi

temporary="$test_root/encoder-cache.new"
printf '%s\n' \
    '{"version":2,"encoder_capabilities":[{"video_codec":1,"adapter_id":0,"device_id":0,"width":3840,"height":2160,"frame_rate":-1,"codec_impl":6,"chroma_sampling":1,"bit_depth":8},{"video_codec":1,"adapter_id":1012,"device_id":8578,"width":3840,"height":2160,"frame_rate":-1,"codec_impl":33,"chroma_sampling":1,"bit_depth":8},{"video_codec":2,"adapter_id":1012,"device_id":8578,"width":3840,"height":2160,"frame_rate":-1,"codec_impl":33,"chroma_sampling":1,"bit_depth":8}]}' \
    >"$temporary"
mv -f -- "$temporary" "$cache"
for _ in {1..30}; do
    if "$policy" verify "$cache" "$backup" 8578 1012 \
        >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
"$policy" verify "$cache" "$backup" 8578 1012 >/dev/null
rm -f -- "$enabled"
wait "$watch_pid"
watch_pid=""
if grep -q 'Traceback' "$test_root/watch.err"; then
    printf 'watcher 恢复缓存时产生了 Python traceback。\n' >&2
    exit 1
fi

printf 'NVENC 能力缓存类型校验与 watcher 恢复测试通过。\n'
