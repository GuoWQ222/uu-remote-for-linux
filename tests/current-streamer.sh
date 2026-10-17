#!/usr/bin/env bash
# Optional real-client acceptance test. Official binaries stay outside Git.
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
install_dir=${UU_REMOTE_TEST_OFFICIAL_INSTALL_DIR:-}
if [[ ! -r $install_dir/bin/streamer.dll ]]; then
    printf '请用 UU_REMOTE_TEST_OFFICIAL_INSTALL_DIR 指向待验证的官方安装目录。\n' >&2
    exit 77
fi
test_root=$(mktemp -d)
cleanup() {
    WINEPREFIX="$test_root/prefix" wineserver -k >/dev/null 2>&1 || true
    WINEPREFIX="$test_root/prefix" timeout 15s wineserver -w >/dev/null 2>&1 || true
    rm -rf -- "$test_root"
}
trap cleanup EXIT
lib="$project_root/lib/uu-remote-for-linux"
policy="$lib/uu-remote-encoder-policy"
mkdir -p "$test_root/app"
cp -- "$install_dir/bin/"*.dll "$test_root/app/"
cp -- "$install_dir/bin/StreamerCodecDetector.exe" "$test_root/app/"
cp -- "$lib/uu-remote-input-hook.dll" "$test_root/app/"
cp -- "$lib/uu-remote-dxgi-probe.exe" "$lib/uu-remote-nvenc-d3d11-probe.exe" "$test_root/app/"
cp -- "$lib/hwdecode/dxvk/x64/"*.dll "$test_root/app/"
export WINEPREFIX="$test_root/prefix" WINEARCH=win64 WINEDEBUG=-all
export WINEDLLPATH="$lib/hwdecode/wine"
export WINEDLLOVERRIDES='d3d11=n;dxgi=n;nvcuda=b;nvcuvid=b;nvencodeapi64=b;mscoree,mshtml=;winemenubuilder.exe=d'
export DXVK_LOG_LEVEL=none UU_REMOTE_CUDA_DEVICE=0
printf 'dxgi.hideNvidiaGpu = False\n' >"$test_root/dxvk.conf"
export DXVK_CONFIG_FILE="$test_root/dxvk.conf"
timeout 45s wineboot --init >"$test_root/boot.log" 2>&1
cp -- "$lib/hwdecode/wine/x86_64-windows/"*.dll "$test_root/prefix/drive_c/windows/system32/"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
    -o "$test_root/app/GameViewerServer.exe" "$project_root/tests/fixtures/current-streamer-probe.c"
# The fixture name selects the server hook, but no official service is started.
version=${UU_REMOTE_TEST_OFFICIAL_VERSION:-4.43.1.3614}
"$policy" derive-streamer-profile "$test_root/app/streamer.dll" "$version" "$test_root/profile.json"
"$policy" verify-streamer-profile "$test_root/app/streamer.dll" "$version" "$test_root/profile.json"
python3 - "$policy" "$test_root" <<'PY'
import json,pathlib,struct,subprocess,sys
policy, root = sys.argv[1], pathlib.Path(sys.argv[2])
saved = json.loads((root/'profile.json').read_text())
data = (root/'app/streamer.dll').read_bytes()
pe = struct.unpack_from('<I',data,0x3c)[0]
table = pe+24+struct.unpack_from('<H',data,pe+20)[0]
sections = [struct.unpack_from('<IIII',data,table+i*40+8)
            for i in range(struct.unpack_from('<H',data,pe+6)[0])]
def file_offset(rva):
    for _, base, size, raw in sections:
        if base<=rva<base+size:
            return raw+rva-base
    raise ValueError('unmapped RVA')
for key in ('encoder_accessor_rva', 'portrait_scaler_rva'):
    mutant = bytearray(data)
    mutant[file_offset(saved['probe'][key])] = 0x90
    binary, profile = root/(key+'.dll'), root/(key+'.json')
    binary.write_bytes(mutant)
    result = subprocess.run([policy,'derive-streamer-profile',str(binary),
                             saved['uu_version'],str(profile)],capture_output=True,text=True)
    assert result.returncode != 0 and not profile.exists(), key
    print('缺失语义拒绝通过：'+key)
PY
read -r scale capture update scaler slot < <(python3 - "$test_root/profile.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1]))['probe']
assert p['frame_adapter_mode']=='native-device-pointer'
assert p['nvenc_v8_impl']==p['nvenc_v11_impl']==0
print(*(hex(p[k]) for k in ('portrait_scale_rva','portrait_capture_rva','portrait_update_rva','portrait_scaler_rva','slot_rva')))
PY
)
cd "$test_root/app"
timeout 30s wine ./uu-remote-dxgi-probe.exe >"$test_root/adapters" 2>"$test_root/dxgi.log"
read -r device adapter < <(awk -F '\t' '$3==4318 { print $1, $2; exit }' "$test_root/adapters")
[[ $device =~ ^[0-9]+$ && $adapter =~ ^[0-9]+$ ]]
export UU_REMOTE_NVENC_ADAPTER_LUID="$adapter"
timeout 30s wine ./GameViewerServer.exe "$test_root/app/uu-remote-input-hook.dll" \
    "$scale" "$capture" "$update" "$scaler" "$slot" >"$test_root/hook.out" 2>"$test_root/hook.log"
tr -d '\r' <"$test_root/hook.out"
timeout 60s wine ./uu-remote-nvenc-d3d11-probe.exe >"$test_root/nvenc.out" 2>"$test_root/nvenc.log"
tr -d '\r' <"$test_root/nvenc.out"
for implementation in 33 0; do
    timeout 60s wine ./StreamerCodecDetector.exe --batch "$implementation" "$device" "$adapter" \
        >"$test_root/batch.out" 2>"$test_root/batch.log"
    tr -d '\r' <"$test_root/batch.out" >"$test_root/results"
    awk -F, '
      $1=="RESULT" && $2==1 && $3==1 && $4==8 && $5>=1920 && $6>=1080 && $7==1 { h264=1 }
      $1=="RESULT" && $2==2 && $3==1 && $4==8 && $5>=1920 && $6>=1080 && $7==1 { hevc=1 }
      $1=="BATCH_DONE" { done=1 }
      END { exit !(h264 && hevc && done) }
    ' "$test_root/results"
    printf '官方编解码探测通过：implementation=%s，H.264/HEVC，至少 1080p。\n' "$implementation"
done
printf '当前官方程序的补丁、GPU 接口及实际编解码验证通过。\n'
