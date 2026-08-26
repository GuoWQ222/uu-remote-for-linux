#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly manager="$project_root/lib/uu-remote-for-linux/uu-remote-x11-virtual-display"
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

mkdir -p "$test_root/bin" "$test_root/state"
printf '%s\n' disconnected >"$test_root/fake-state"

# The fixture is materialized with install so the test never edits repository
# files or relies on the host display state.
fixture_xrandr="$test_root/bin/xrandr"
fixture_nvidia="$test_root/bin/nvidia-settings"

sed \
    -e "s|@STATE@|$test_root/fake-state|g" \
    "$project_root/tests/fixtures/x11-virtual-display-xrandr" >"$fixture_xrandr"
sed \
    -e "s|@STATE@|$test_root/fake-state|g" \
    "$project_root/tests/fixtures/x11-virtual-display-nvidia-settings" \
    >"$fixture_nvidia"
chmod 0755 "$fixture_xrandr" "$fixture_nvidia"

export DISPLAY=:99
export XDG_SESSION_TYPE=x11
export UU_REMOTE_X11_VDISPLAY_STATE_DIR="$test_root/state"
export UU_REMOTE_X11_VDISPLAY_XRANDR="$fixture_xrandr"
export UU_REMOTE_X11_VDISPLAY_NVIDIA_SETTINGS="$fixture_nvidia"
export UU_REMOTE_X11_VDISPLAY_SYSTEM_EDID="$test_root/system/edid.bin"
export UU_REMOTE_X11_VDISPLAY_SYSTEM_XORG="$test_root/system/20-uu-remote.conf"

"$manager" self-test >"$test_root/self-test.json"
grep -Fq '"result": "pass"' "$test_root/self-test.json"

"$manager" preflight >"$test_root/preflight.json"
grep -Fq '"virtual_output": "DP-0"' "$test_root/preflight.json"
grep -Fq '"virtual_dpy": "DFP-1"' "$test_root/preflight.json"

"$manager" prepare >"$test_root/prepare.txt"
readonly staged="$test_root/state/prepared"
test -s "$staged/manifest.json"
test -s "$staged/edid.bin"
test -s "$staged/20-uu-remote.conf"
test "$(stat -c %s "$staged/edid.bin")" -eq 128
python3 - "$staged/edid.bin" <<'PY'
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
assert data[:8] == b"\x00\xff\xff\xff\xff\xff\xff\x00"
assert len(data) == 128
assert sum(data) % 256 == 0
assert data[54:56] == bytes([0x02, 0x3A])
PY
grep -Fq 'Option "ConnectedMonitor" "DFP-0, DFP-1, DFP-3"' \
    "$staged/20-uu-remote.conf"
grep -Fq "Option \"CustomEDID\" \"DFP-1:$test_root/system/edid.bin\"" \
    "$staged/20-uu-remote.conf"
find "$test_root/state/snapshots" -mindepth 2 -maxdepth 2 \
    -name manifest.json -type f | grep -q .

printf '%s\n' connected-off >"$test_root/fake-state"
"$manager" verify >"$test_root/verify-off.json"
"$manager" enable --mode 1920x1080 --right-of HDMI-1 \
    >"$test_root/enable.json"
grep -Fxq connected-on "$test_root/fake-state"
"$manager" verify --require-active >"$test_root/verify-on.json"
grep -Fq '"geometry": [' "$test_root/verify-on.json"
"$manager" disable >"$test_root/disable.json"
grep -Fxq connected-off "$test_root/fake-state"
"$manager" enable --mode 2560x1440 --projection duplicate \
    >"$test_root/duplicate.json"
grep -Fxq connected-duplicate "$test_root/fake-state"
grep -Fq '"projection": "duplicate"' "$test_root/duplicate.json"
"$manager" disable >/dev/null
grep -Fxq connected-off "$test_root/fake-state"
layout='[{"output":"HDMI-0","active":true,"primary":false,"mode":"2560x1440","rate":"74.97","rotation":"left","position":[0,0]},{"output":"HDMI-1","active":true,"primary":true,"mode":"2560x1440","rate":"59.95","rotation":"normal","position":[1440,0]}]'
"$manager" restore-layout --layout-json "$layout" >"$test_root/restored.json"
grep -Fq '"result": "layout-restored"' "$test_root/restored.json"
grep -Fxq connected-off "$test_root/fake-state"

if [[ $(id -u) -eq 0 ]]; then
    "$manager" apply-prepared --staged-dir "$staged"
    cmp "$staged/edid.bin" "$test_root/system/edid.bin"
    cmp "$staged/20-uu-remote.conf" "$test_root/system/20-uu-remote.conf"
    "$manager" remove-system
    test ! -e "$test_root/system/edid.bin"
    test ! -e "$test_root/system/20-uu-remote.conf"
else
    if "$manager" apply-prepared --staged-dir "$staged" \
        >"$test_root/apply.stdout" 2>"$test_root/apply.stderr"; then
        printf '非 root apply-prepared 意外成功。\n' >&2
        exit 1
    fi
    grep -Fq '必须以 root 运行' "$test_root/apply.stderr"
fi

printf 'X11 NVIDIA 虚拟显示器阶段 0/1 检查通过。\n'
