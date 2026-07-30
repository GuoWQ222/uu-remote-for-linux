#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uuyc-linux-controller/uuyc-wol-bridge"

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

readonly server="$test_root/GameViewerServer.exe"
readonly backup="$test_root/GameViewerServer.uuyc-wol-original.exe"
readonly expected="$test_root/GameViewerServer.expected.exe"
readonly log_dir="$test_root/client/Log"
readonly patch_offset=4096

truncate -s 8192 "$server"
printf '\110\211\134' |
    dd of="$server" bs=1 seek="$patch_offset" conv=notrunc status=none
cp "$server" "$expected"
printf '\260\001\303' |
    dd of="$expected" bs=1 seek="$patch_offset" conv=notrunc status=none
original_sha=$(sha256sum "$server")
original_sha=${original_sha%% *}
patched_sha=$(sha256sum "$expected")
patched_sha=${patched_sha%% *}

UUYC_WOL_PATCH_OFFSET="$patch_offset" \
    "$bridge" patch "$server" "$backup" "$original_sha" "$patched_sha"
UUYC_WOL_PATCH_OFFSET="$patch_offset" \
    "$bridge" patched "$server" "$backup" "$original_sha" "$patched_sha"
cmp -s "$server" "$expected"
test "$(sha256sum "$backup" | cut -d' ' -f1)" = "$original_sha"

UUYC_WOL_PATCH_OFFSET="$patch_offset" \
    "$bridge" patch "$server" "$backup" "$original_sha" "$patched_sha"
UUYC_WOL_PATCH_OFFSET="$patch_offset" \
    "$bridge" restore "$server" "$backup" "$original_sha" "$patched_sha"
test "$(sha256sum "$server" | cut -d' ' -f1)" = "$original_sha"
test ! -e "$backup"

printf '\001' | dd of="$server" bs=1 seek=12 conv=notrunc status=none
if UUYC_WOL_PATCH_OFFSET="$patch_offset" \
    "$bridge" patch "$server" "$backup" "$original_sha" "$patched_sha" \
    >"$test_root/reject.out" 2>"$test_root/reject.err"; then
    printf '未知服务端版本被意外修改。\n' >&2
    exit 1
fi
grep -q '不支持当前 GameViewerServer.exe' "$test_root/reject.err"

mkdir -p "$log_dir"
test "$("$bridge" cloud-state "$log_dir")" = unknown
printf '%s\n' '{"support_wol":false}' >"$log_dir/log_1.txt"
test "$("$bridge" cloud-state "$log_dir")" = disabled
printf '%s\n' '{"support_wol":true}' >>"$log_dir/log_1.txt"
test "$("$bridge" cloud-state "$log_dir")" = enabled
sleep 0.02
printf 'new session\n' >"$log_dir/log_2.txt"
test "$("$bridge" cloud-state "$log_dir")" = unknown

printf '远程开机识别桥检查通过。\n'
