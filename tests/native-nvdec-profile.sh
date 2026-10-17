#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
python3 - "$project_root" "$test_root" <<'PY'
import importlib.machinery, importlib.util, pathlib, sys, re
root, out = map(pathlib.Path, sys.argv[1:])
loader = importlib.machinery.SourceFileLoader("nvdec_test", str(root/"lib/uu-remote-for-linux/uu-remote-hwdecode-profiler"))
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = module
loader.exec_module(module)
image = module.synthetic_pe(native=True, frame_displacement=0x40, second_relative=0xC00)
row, unchanged = module.derive_profile(image, "9.9.9.11")
assert unchanged == image
(out/"streamer.dll").write_bytes(image)
(out/"profiles.tsv").write_text(row+"\n")
source = (root/"bin/uu-remote-for-linux").read_text()
functions = ("load_hwdecode_profile", "load_or_migrate_hwdecode_profile", "file_sha256", "streamer_bytes_at", "deploy_streamer_patch", "remove_streamer_patch", "streamer_bridge_ready", "configure_hwdecode_environment")
(out/"functions.sh").write_text("\n\n".join(re.search(r"^"+name+r"\(\) \{.*?^\}", source, re.M|re.S).group() for name in functions))
PY
source "$test_root/functions.sh"
die() { printf '%s\n' "$*" >&2; exit 1; }
registry_value() { printf '9.9.9.11\n'; }
hwdecode_generated_profiles="$test_root/profiles.tsv"
installed_version_key="test-key"
streamer_profile_version=""
streamer_dll="$test_root/streamer.dll"
streamer_original="$test_root/streamer.original.dll"
decoder_cache="$test_root/cache.json"
decoder_cache_backup="$test_root/cache.backup.json"
original=$(file_sha256 "$streamer_dll")
deploy_streamer_patch
streamer_bridge_ready
test ! -e "$streamer_original"
test "$(file_sha256 "$streamer_dll")" = "$original"
remove_streamer_patch
test "$(file_sha256 "$streamer_dll")" = "$original"
printf '\0' >>"$streamer_dll"
if (deploy_streamer_patch) >"$test_root/tamper.out" 2>&1; then
    printf '原生 NVDEC 档案意外接受了篡改后的官方程序。\n' >&2
    exit 1
fi
hwdecode_wine_dir="$test_root/wine"
WINEDLLOVERRIDES=""
WINEDLLPATH=""
DXVK_CONFIG=""
configure_hwdecode_environment
[[ $DXVK_CONFIG == 'dxgi.hideNvidiaGpu = False' ]]
printf '原生 NVDEC 档案、无修改部署、关闭桥接和篡改拒绝测试通过。\n'
