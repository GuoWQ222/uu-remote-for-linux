#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_file="$project_root/src/uu-remote-input-hook.c"
readonly output_file="$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
readonly injector_source="$project_root/src/uu-remote-input-injector.c"
readonly injector_output="$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
readonly compiler="${UU_REMOTE_MINGW_CC:-x86_64-w64-mingw32-gcc}"
readonly include_dir="${UU_REMOTE_MINGW_INCLUDE_DIR:-}"
readonly library_dir="${UU_REMOTE_MINGW_LIBRARY_DIR:-}"
readonly runtime_dir="${UU_REMOTE_MINGW_RUNTIME_DIR:-}"
extra_flags=()

[[ -z $include_dir ]] || extra_flags+=("-isystem" "$include_dir")
[[ -z $library_dir ]] || extra_flags+=("-B$library_dir" "-L$library_dir")
[[ -z $runtime_dir ]] || extra_flags+=("-B$runtime_dir" "-L$runtime_dir")

command -v "$compiler" >/dev/null 2>&1 || {
    printf '缺少 %s；Ubuntu 请安装 gcc-mingw-w64-x86-64。\n' \
        "$compiler" >&2
    exit 1
}

# Keep the checked-in PE artifact reproducible across local and CI builds.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-0}"

temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/uu-remote-input-hook.XXXXXX")
temporary="$temporary_dir/uu-remote-input-hook.dll"
injector_temporary="$temporary_dir/uu-remote-input-injector.exe"
trap 'rm -rf -- "$temporary_dir"' EXIT

"$compiler" \
    -std=c11 \
    -Os \
    -Wall \
    -Wextra \
    -Werror \
    -shared \
    -Wl,--no-insert-timestamp \
    -Wl,--image-base,0x180000000 \
    -Wl,--nxcompat \
    -Wl,--dynamicbase \
    "${extra_flags[@]}" \
    -o "$temporary" \
    "$source_file" \
    -lgdi32 \
    -luser32 \
    -lws2_32

x86_64-w64-mingw32-strip --strip-unneeded "$temporary"
"$compiler" \
    -std=c11 \
    -Os \
    -Wall \
    -Wextra \
    -Werror \
    -municode \
    -Wl,--no-insert-timestamp \
    -Wl,--image-base,0x140000000 \
    -Wl,--nxcompat \
    -Wl,--dynamicbase \
    "${extra_flags[@]}" \
    -o "$injector_temporary" \
    "$injector_source"
x86_64-w64-mingw32-strip --strip-unneeded "$injector_temporary"
install -Dm0644 "$temporary" "$output_file"
install -Dm0644 "$injector_temporary" "$injector_output"
printf '%s\n' "$output_file"
sha256sum "$output_file"
printf '%s\n' "$injector_output"
sha256sum "$injector_output"
