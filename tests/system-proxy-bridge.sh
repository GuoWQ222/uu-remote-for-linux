#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly bridge="$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge"
readonly fixture_gsettings="$project_root/tests/fixtures/bin/proxy-gsettings"

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
gsettings_root="$test_root/gsettings"
prefix="$test_root/prefix"
status="$test_root/status.json"
log="$test_root/proxy.log"
trace="$test_root/wine.log"
fake_wine="$test_root/wine"
mkdir -p "$gsettings_root" "$prefix"
touch "$prefix/system.reg"

cat >"$fake_wine" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
for variable in \
    http_proxy https_proxy ftp_proxy all_proxy no_proxy \
    HTTP_PROXY HTTPS_PROXY FTP_PROXY ALL_PROXY NO_PROXY; do
    [[ ! -v $variable ]] || {
        printf 'proxy environment leaked: %s\n' "$variable" >&2
        exit 9
    }
done
printf '%s\n' "$*" >>"${UU_REMOTE_SYSTEM_PROXY_WINE_TRACE:?}"
exit 0
EOF
chmod +x "$fake_wine"

export UU_REMOTE_SYSTEM_PROXY_SOURCE=gnome
export UU_REMOTE_SYSTEM_PROXY_GSETTINGS_BIN="$fixture_gsettings"
export UU_REMOTE_SYSTEM_PROXY_GSETTINGS_ROOT="$gsettings_root"
export UU_REMOTE_SYSTEM_PROXY_WINE_TRACE="$trace"
export HTTP_PROXY=http://should-not-leak.invalid:8888
export NO_PROXY=localhost

printf "'manual'\n" >"$gsettings_root/mode"
printf "true\n" >"$gsettings_root/use-same-proxy"
printf "['localhost', '*.example.test']\n" >"$gsettings_root/ignore-hosts"
printf "''\n" >"$gsettings_root/autoconfig-url"
printf "'127.0.0.1'\n" >"$gsettings_root/http-host"
printf "7897\n" >"$gsettings_root/http-port"
printf "''\n" >"$gsettings_root/https-host"
printf "0\n" >"$gsettings_root/https-port"
printf "''\n" >"$gsettings_root/ftp-host"
printf "0\n" >"$gsettings_root/ftp-port"
printf "''\n" >"$gsettings_root/socks-host"
printf "0\n" >"$gsettings_root/socks-port"

"$bridge" sync "$prefix" "$fake_wine" "$status" "$log"
grep -Fq \
    'Internet Settings\Connections /v DefaultConnectionSettings /t REG_BINARY /d 460000000000000003000000' \
    "$trace"
grep -Fq 'ProxyEnable /t REG_DWORD /d 1 /f' "$trace"
grep -Fq \
    'ProxyServer /t REG_SZ /d http=127.0.0.1:7897;https=127.0.0.1:7897;ftp=127.0.0.1:7897 /f' \
    "$trace"
grep -Fq \
    'ProxyOverride /t REG_SZ /d localhost;*.example.test;<local> /f' \
    "$trace"
grep -Fq '"mode": "manual"' "$status"
grep -Fq '"source": "gnome"' "$status"
"$bridge" describe "$status" | grep -Fq '已同步'

: >"$trace"
printf "'auto'\n" >"$gsettings_root/mode"
printf "'http://127.0.0.1:8080/proxy.pac'\n" \
    >"$gsettings_root/autoconfig-url"
"$bridge" sync "$prefix" "$fake_wine" "$status" "$log"
grep -Fq \
    'Internet Settings\Connections /v DefaultConnectionSettings /t REG_BINARY /d 460000000000000005000000' \
    "$trace"
grep -Fq 'ProxyEnable /t REG_DWORD /d 0 /f' "$trace"
grep -Fq \
    'AutoConfigURL /t REG_SZ /d http://127.0.0.1:8080/proxy.pac /f' \
    "$trace"
grep -Fq '"mode": "auto"' "$status"

: >"$trace"
printf "'none'\n" >"$gsettings_root/mode"
"$bridge" sync "$prefix" "$fake_wine" "$status" "$log"
grep -Fq \
    'Internet Settings\Connections /v DefaultConnectionSettings /t REG_BINARY /d 460000000000000001000000' \
    "$trace"
grep -Fq 'ProxyEnable /t REG_DWORD /d 0 /f' "$trace"
grep -Eq '^reg delete .* /v ProxyServer /f$' "$trace"
grep -Fq '"mode": "none"' "$status"

unset UU_REMOTE_SYSTEM_PROXY_SOURCE
export XDG_CURRENT_DESKTOP=unknown
export DESKTOP_SESSION=unknown
export http_proxy=http://user:secret@proxy.example.test:3128
export https_proxy=http://proxy.example.test:3129
export no_proxy=localhost,.internal.example.test
detected=$("$bridge" detect)
grep -Fq '"authentication_required": true' <<<"$detected"
grep -Fq \
    '"proxy_server": "http=proxy.example.test:3128;https=proxy.example.test:3129"' \
    <<<"$detected"
if grep -Fq 'secret' <<<"$detected"; then
    printf '代理密码泄漏到检测输出。\n' >&2
    exit 1
fi

kde_config="$test_root/kioslaverc"
export UU_REMOTE_SYSTEM_PROXY_SOURCE=kde
export UU_REMOTE_SYSTEM_PROXY_KDE_CONFIG="$kde_config"
cat >"$kde_config" <<'EOF'
[Proxy Settings]
ProxyType=1
httpProxy=http://127.0.0.1:7897
httpsProxy=http://127.0.0.1:7897
NoProxyFor=localhost,.example.test
EOF
detected=$("$bridge" detect)
grep -Fq '"mode": "manual"' <<<"$detected"
grep -Fq '"source": "kde"' <<<"$detected"

sed -i 's/ProxyType=1/ProxyType=3/' "$kde_config"
detected=$("$bridge" detect)
grep -Fq '"autodetect": true' <<<"$detected"
grep -Fq '"mode": "auto"' <<<"$detected"

sed -i 's/ProxyType=3/ProxyType=4/' "$kde_config"
detected=$("$bridge" detect)
grep -Fq '"source": "kde-environment"' <<<"$detected"
