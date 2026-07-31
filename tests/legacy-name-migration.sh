#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly launcher="$project_root/bin/uu-remote-for-linux"
readonly fixture_bin="$project_root/tests/fixtures/bin"

test_root=$(mktemp -d)
legacy_helper_pid=

cleanup() {
    if [[ $legacy_helper_pid =~ ^[0-9]+$ ]] &&
        kill -0 "$legacy_helper_pid" 2>/dev/null; then
        kill "$legacy_helper_pid" 2>/dev/null || true
        wait "$legacy_helper_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT

export PATH="$fixture_bin:$PATH"
export XDG_DATA_HOME="$test_root/data"
export XDG_CONFIG_HOME="$test_root/config"
export XDG_STATE_HOME="$test_root/state"
export XDG_CACHE_HOME="$test_root/cache"
export UU_REMOTE_RUNTIME_DIR="$project_root/lib/uu-remote-for-linux"
export UU_REMOTE_FAKE_TRACE="$test_root/fake-tools.log"
unset UU_REMOTE_DISABLE_LEGACY_MIGRATION

legacy_id="uuyc-linux-controller"
new_id="uu-remote-for-linux"
legacy_helper_dir="$test_root/$legacy_id"
legacy_helper="$legacy_helper_dir/uuyc-sleep-bridge"
mkdir -p "$legacy_helper_dir"
cp /usr/bin/sleep "$legacy_helper"
"$legacy_helper" 120 &
legacy_helper_pid=$!
mkdir -p \
    "$XDG_DATA_HOME/$legacy_id/wineprefix/drive_c" \
    "$XDG_STATE_HOME/$legacy_id" \
    "$XDG_CACHE_HOME/$legacy_id" \
    "$XDG_CONFIG_HOME/autostart"
printf 'legacy-prefix\n' \
    >"$XDG_DATA_HOME/$legacy_id/wineprefix/drive_c/migration-marker"
printf 'legacy-state\n' >"$XDG_STATE_HOME/$legacy_id/state-marker"
printf 'legacy-cache\n' >"$XDG_CACHE_HOME/$legacy_id/cache-marker"
printf '[Desktop Entry]\nExec=uuyc-linux-controller\n' \
    >"$XDG_CONFIG_HOME/autostart/$legacy_id.desktop"

[[ $("$launcher" --version) == "$(<"$project_root/VERSION")" ]]

for _ in {1..100}; do
    kill -0 "$legacy_helper_pid" 2>/dev/null || break
    sleep 0.01
done
if kill -0 "$legacy_helper_pid" 2>/dev/null; then
    printf '旧版防休眠后台进程未被清理：%s\n' "$legacy_helper_pid" >&2
    kill "$legacy_helper_pid" 2>/dev/null || true
    exit 1
fi
wait "$legacy_helper_pid" 2>/dev/null || true
legacy_helper_pid=

test -f \
    "$XDG_DATA_HOME/$new_id/wineprefix/drive_c/migration-marker"
test -f "$XDG_STATE_HOME/$new_id/state-marker"
test -f "$XDG_CACHE_HOME/$new_id/cache-marker"
test -f "$XDG_CONFIG_HOME/autostart/$new_id.desktop"
grep -Fqx "Exec=$new_id" \
    "$XDG_CONFIG_HOME/autostart/$new_id.desktop"
test ! -e "$XDG_DATA_HOME/$legacy_id"
test ! -e "$XDG_STATE_HOME/$legacy_id"
test ! -e "$XDG_CACHE_HOME/$legacy_id"
test ! -e "$XDG_CONFIG_HOME/autostart/$legacy_id.desktop"
grep -q "SYSTEMCTL --user stop $legacy_id-client.service" \
    "$UU_REMOTE_FAKE_TRACE"
grep -q "$legacy_id-autostart.service" "$UU_REMOTE_FAKE_TRACE"
grep -q "$legacy_id-sleep.service" "$UU_REMOTE_FAKE_TRACE"

printf '旧项目名称对应的用户数据迁移检查通过。\n'
