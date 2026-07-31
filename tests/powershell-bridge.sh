#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe"

for command_name in wine wineboot wineserver; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'SKIP Wine WOL PowerShell 兼容桥实测（缺少 %s）\n' \
            "$command_name"
        exit 0
    }
done
[[ -s $bridge ]] || {
    printf 'WOL PowerShell 兼容桥不存在：%s\n' "$bridge" >&2
    exit 1
}

test_root=$(mktemp -d)
readonly prefix="$test_root/prefix"
readonly app_dir="$prefix/drive_c/GameViewer/bin"
readonly status="$prefix/drive_c/uu-remote-wol-powershell-status.ini"

cleanup() {
    WINEPREFIX="$prefix" WINEDEBUG=-all wineserver -k >/dev/null 2>&1 || true
    rm -rf -- "$test_root"
}
trap cleanup EXIT

WINEPREFIX="$prefix" WINEARCH=win64 WINEDEBUG=-all wineboot -u \
    >/dev/null 2>&1
install -Dm0644 "$bridge" "$app_dir/powershell.exe"
install -Dm0644 "$project_root/tests/fixtures/wol-ready.ini" \
    "$prefix/drive_c/uu-remote-wol-bridge.ini"

run_query() {
    local expected=$1 query=$2 output

    output=$(
        cd "$app_dir"
        WINEPREFIX="$prefix" WINEDEBUG=-all \
            wine cmd /d /c powershell \
                -WindowStyle Hidden -NoProfile -Command "$query" |
            tr -d '\r'
    )
    [[ $output == "$expected" ]] || {
        printf 'PowerShell bridge output mismatch: expected=%s actual=%s\n' \
            "$expected" "$output" >&2
        return 1
    }
}

run_query Enabled \
    'Get-NetAdapter | Get-NetAdapterPowerManagement | Select-Object -ExpandProperty AllowComputerToTurnOffDevice'
run_query 1 \
    'Get-NetAdapterAdvancedProperty -RegistryKeyword S5WakeOnLan | Select-Object -ExpandProperty RegistryValue'
run_query 1 \
    'Get-NetAdapterAdvancedProperty -RegistryKeyword EnablePME | Select-Object -ExpandProperty RegistryValue'
run_query Enabled \
    'Get-NetAdapter | Get-NetAdapterPowerManagement | Select-Object -ExpandProperty WakeOnMagicPacket'
run_query Enabled \
    'Get-NetAdapter | Get-NetAdapterPowerManagement | Select-Object -ExpandProperty WakeOnPattern'
run_query True \
    'Get-CimInstance -ClassName MSPower_DeviceWakeEnable | Select-Object -ExpandProperty Enable'
run_query True \
    'Get-CimInstance -ClassName MSNdis_DeviceWakeOnMagicPacketOnly | Select-Object -ExpandProperty EnableWakeOnMagicPacketOnly'

grep -Fq 'allow_turn_off=1' "$status"
grep -Fq 's5_wake=1' "$status"
grep -Fq 'enable_pme=1' "$status"
grep -Fq 'magic_packet=1' "$status"
grep -Fq 'wake_pattern=1' "$status"
grep -Fq 'device_wake=1' "$status"
grep -Fq 'magic_only=1' "$status"

printf 'Wine WOL PowerShell 电源属性兼容桥实测通过。\n'
