#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly source_archive="$project_root/third_party/sources/nvcuda-uu-remote-v0.8.tar.xz"
readonly fixture="$project_root/tests/fixtures/nvdec-d3d11-probe.c"
readonly hwdecode="$project_root/lib/uu-remote-for-linux/hwdecode"

for command in tar x86_64-w64-mingw32-gcc wine; do
    command -v "$command" >/dev/null || {
        printf '缺少 NVDEC 回归测试命令：%s\n' "$command" >&2
        exit 77
    }
done
[[ -r $source_archive ]] || {
    printf '缺少 nvcuda 对应源码包：%s\n' "$source_archive" >&2
    exit 77
}

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
tar -xf "$source_archive" -C "$test_root"
mkdir -p "$test_root/app"

x86_64-w64-mingw32-gcc \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$test_root/nvcuda-uu-remote-v0.8/include" \
    "$fixture" \
    -o "$test_root/app/nvdec-d3d11-probe.exe" \
    -ld3d11 -ldxgi -luuid
install -m 0644 "$hwdecode/dxvk/x64/d3d11.dll" "$test_root/app/d3d11.dll"
install -m 0644 "$hwdecode/dxvk/x64/dxgi.dll" "$test_root/app/dxgi.dll"
install -m 0644 \
    "$hwdecode/wine/x86_64-windows/nvcuda.dll" \
    "$test_root/app/nvcuda.dll"

export WINEPREFIX="$test_root/wineprefix"
export WINEARCH=win64
export WINEDLLPATH="$hwdecode/wine/x86_64-unix"
export WINEDLLOVERRIDES='d3d11=n;dxgi=n;nvcuda=b'
export WINEDEBUG=-all
export DXVK_LOG_LEVEL=none

wine "$test_root/app/nvdec-d3d11-probe.exe"
UU_REMOTE_CUDA_DEVICE=999 \
    wine "$test_root/app/nvdec-d3d11-probe.exe" --expect-invalid-device
