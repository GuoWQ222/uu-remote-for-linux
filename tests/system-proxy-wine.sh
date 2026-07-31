#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge"
readonly probe_source="$project_root/tests/fixtures/system-proxy-probe.c"

for command_name in \
    wine wineboot wineserver x86_64-w64-mingw32-gcc; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'SKIP Wine 系统代理实测（缺少 %s）\n' "$command_name"
        exit 0
    fi
done

test_root=$(mktemp -d)
export WINEPREFIX="$test_root/prefix"
export WINEDEBUG=-all

cleanup() {
    wineserver -k >/dev/null 2>&1 || true
    timeout 10s wineserver -w >/dev/null 2>&1 || true
    rm -rf -- "$test_root"
}
trap cleanup EXIT

WINEARCH=win64 wineboot -u >/dev/null 2>&1
for _ in {1..300}; do
    [[ -f $WINEPREFIX/system.reg ]] && break
    sleep 0.05
done
[[ -f $WINEPREFIX/system.reg ]]
x86_64-w64-mingw32-gcc -municode -O2 \
    "$probe_source" -o "$test_root/system-proxy-probe.exe" -lwinhttp

env -u all_proxy -u ALL_PROXY -u no_proxy -u NO_PROXY \
    UU_REMOTE_SYSTEM_PROXY_SOURCE=environment \
    http_proxy=http://127.0.0.1:7897 \
    https_proxy=http://127.0.0.1:7897 \
    "$bridge" sync \
    "$WINEPREFIX" wine "$test_root/status.json" "$test_root/proxy.log"

output=$(env \
    -u http_proxy -u https_proxy -u ftp_proxy -u all_proxy -u no_proxy \
    -u HTTP_PROXY -u HTTPS_PROXY -u FTP_PROXY -u ALL_PROXY -u NO_PROXY \
    wine "$test_root/system-proxy-probe.exe")
output=${output//$'\r'/}
grep -Fq 'autodetect=0' <<<"$output"
grep -Fq \
    'proxy=http=127.0.0.1:7897;https=127.0.0.1:7897' <<<"$output"

UU_REMOTE_SYSTEM_PROXY_SOURCE=none \
    "$bridge" sync \
    "$WINEPREFIX" wine "$test_root/status.json" "$test_root/proxy.log"
output=$(env \
    -u http_proxy -u https_proxy -u ftp_proxy -u all_proxy -u no_proxy \
    -u HTTP_PROXY -u HTTPS_PROXY -u FTP_PROXY -u ALL_PROXY -u NO_PROXY \
    wine "$test_root/system-proxy-probe.exe")
output=${output//$'\r'/}
grep -Fq 'autodetect=0' <<<"$output"
grep -Eq '^proxy=$' <<<"$output"

printf 'Wine WinHTTP 系统代理注册表实测通过。\n'
