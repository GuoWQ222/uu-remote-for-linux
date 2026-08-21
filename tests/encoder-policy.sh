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

malformed_streamer="$test_root/malformed-streamer.dll"
python3 - "$malformed_streamer" <<'PY'
import struct
import sys

data = bytearray(0x200)
data[:2] = b"MZ"
struct.pack_into("<I", data, 0x3C, 0x80)
data[0x80:0x84] = b"PE\0\0"
struct.pack_into(
    "<HHIIIHH", data, 0x84, 0x8664, 2, 0, 0, 0, 0xF0, 0
)
optional = 0x98
struct.pack_into("<H", data, optional, 0x20B)
struct.pack_into("<Q", data, optional + 24, 0x140000000)
sections = optional + 0xF0
data[sections:sections + 8] = b".text\0\0\0"
struct.pack_into(
    "<IIII", data, sections + 8, 0xFFFFFFF0, 0x1000,
    0xFFFFFFF0, 0x1E0
)
rdata = sections + 40
data[rdata:rdata + 8] = b".rdata\0\0"
struct.pack_into("<IIII", data, rdata + 8, 0x20, 0x2000, 0x20, 0x1E0)
with open(sys.argv[1], "wb") as stream:
    stream.write(data)
PY
malformed_status=0
if timeout 2s "$policy" probe-streamer "$malformed_streamer" \
    >"$test_root/malformed.out" 2>"$test_root/malformed.err"; then
    printf '越界 PE 节被 streamer 分析器意外接受。\n' >&2
    exit 1
else
    malformed_status=$?
fi
if ((malformed_status == 124)); then
    printf '越界 PE 节导致 streamer 分析器卡死。\n' >&2
    exit 1
fi
grep -q 'streamer.dll 的 PE 结构无效.*PE 节范围越界' \
    "$test_root/malformed.err"
if grep -q 'Traceback' "$test_root/malformed.err"; then
    printf '越界 PE 节触发了 Python traceback。\n' >&2
    exit 1
fi

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
