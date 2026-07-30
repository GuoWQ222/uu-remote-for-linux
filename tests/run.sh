#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
launcher="$project_root/bin/uuyc-linux-controller"
readonly launcher

failures=0

check() {
    local label=$1
    shift
    if "$@"; then
        printf 'PASS %s\n' "$label"
    else
        printf 'FAIL %s\n' "$label" >&2
        failures=$((failures + 1))
    fi
}

check "launcher syntax" bash -n "$launcher"
check "decoder selector syntax" bash -n \
    "$project_root/lib/uuyc-linux-controller/uuyc-decoder-selector"
check "tray proxy Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uuyc-linux-controller/uuyc-tray-proxy"
check "keyboard bridge Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uuyc-linux-controller/uuyc-keyboard-bridge"
check "autostart bridge syntax" bash -n \
    "$project_root/lib/uuyc-linux-controller/uuyc-autostart-bridge"
check "sleep bridge syntax" bash -n \
    "$project_root/lib/uuyc-linux-controller/uuyc-sleep-bridge"
check "WOL bridge syntax" bash -n \
    "$project_root/lib/uuyc-linux-controller/uuyc-wol-bridge"
check "WOL root configurator syntax" bash -n \
    "$project_root/lib/uuyc-linux-controller/uuyc-wol-configure"
check "update bridge syntax" bash -n \
    "$project_root/lib/uuyc-linux-controller/uuyc-update-bridge"
check "systemctl fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/systemctl"
check "systemd-run fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/systemd-run"
check "tray proxy fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/uuyc-tray-proxy"
check "zenity fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/zenity"
check "keyboard gsettings fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/keyboard-gsettings"
check "NVDEC probe builder syntax" bash -n \
    "$project_root/scripts/build-nvdec-probe.sh"
check "update blocker builder syntax" bash -n \
    "$project_root/scripts/build-update-blocker.sh"
check "user installer syntax" bash -n "$project_root/scripts/install-user.sh"
check "shim builder syntax" bash -n "$project_root/scripts/build-wevtapi.sh"
check "deb builder syntax" bash -n "$project_root/packaging/build-deb.sh"
check "postinst syntax" bash -n "$project_root/packaging/debian/postinst"
check "postrm syntax" bash -n "$project_root/packaging/debian/postrm"
check "desktop entry" desktop-file-validate \
    "$project_root/share/applications/uuyc-linux-controller.desktop"
if command -v appstreamcli >/dev/null 2>&1; then
    check "AppStream metadata" appstreamcli validate --no-net \
        "$project_root/share/metainfo/io.github.guowq222.uuyc_linux_controller.metainfo.xml"
else
    printf 'SKIP AppStream metadata（未安装 appstreamcli）\n'
fi
if command -v xmllint >/dev/null 2>&1; then
    check "AppStream XML" xmllint --noout \
        "$project_root/share/metainfo/io.github.guowq222.uuyc_linux_controller.metainfo.xml"
else
    printf 'SKIP AppStream XML（未安装 xmllint）\n'
fi
check "help command" "$launcher" --help
check "version command" "$launcher" --version
check "diagnose without Wine" "$launcher" --diagnose
check "mock Wine integration" "$project_root/tests/integration-mock.sh"
check "native tray proxy" "$project_root/tests/tray-proxy.sh"
check "autostart bridge integration" "$project_root/tests/autostart-bridge.sh"
check "sleep bridge integration" "$project_root/tests/sleep-bridge.sh"
check "WOL bridge integration" "$project_root/tests/wol-bridge.sh"
check "update bridge integration" "$project_root/tests/update-bridge.sh"
check "keyboard bridge integration" "$project_root/tests/keyboard-bridge.sh"

check "shim exists" test -s \
    "$project_root/lib/uuyc-linux-controller/wevtapi.dll"
check "decoder selector exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-decoder-selector"
check "native tray proxy exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-tray-proxy"
check "autostart bridge exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-autostart-bridge"
check "sleep bridge exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-sleep-bridge"
check "WOL bridge exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-wol-bridge"
check "WOL root configurator exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-wol-configure"
check "update bridge exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-update-bridge"
check "keyboard bridge exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-keyboard-bridge"
check "update compatibility profiles exist" test -s \
    "$project_root/lib/uuyc-linux-controller/update-compatibility.tsv"
check "NVDEC probe exists" test -x \
    "$project_root/lib/uuyc-linux-controller/uuyc-nvdec-probe"
# shellcheck disable=SC2016
check "NVDEC probe is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB.*x86-64"' _ \
    "$project_root/lib/uuyc-linux-controller/uuyc-nvdec-probe"
# The snippets intentionally defer "$1" expansion to the nested shell.
# shellcheck disable=SC2016
check "shim is PE DLL" bash -c \
    'file "$1" | grep -q "PE32+ executable (DLL).*x86-64"' _ \
    "$project_root/lib/uuyc-linux-controller/wevtapi.dll"
# shellcheck disable=SC2016
check "shim exports" bash -c \
    'objdump -p "$1" | grep -q "EvtOpenPublisherMetadata"' _ \
    "$project_root/lib/uuyc-linux-controller/wevtapi.dll"
# shellcheck disable=SC2016
check "update blocker is Win64 GUI PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (GUI).*x86-64"' _ \
    "$project_root/lib/uuyc-linux-controller/uuyc-update-blocker.exe"
# The snippets intentionally defer "$1" expansion to the nested shell.
# shellcheck disable=SC2016
check "hardware bridge manifest" bash -c \
    'cd "$1" && sha256sum --check MANIFEST.sha256' _ \
    "$project_root/lib/uuyc-linux-controller/hwdecode"
# shellcheck disable=SC2016
check "nvcuda bridge is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB shared object"' _ \
    "$project_root/lib/uuyc-linux-controller/hwdecode/wine/x86_64-unix/nvcuda.dll.so"
check "DXVK d3d11 is PE DLL" bash -c \
    'file "$1" | grep -q "PE32+ executable (DLL).*x86-64"' _ \
    "$project_root/lib/uuyc-linux-controller/hwdecode/dxvk/x64/d3d11.dll"
if command -v shellcheck >/dev/null 2>&1; then
    check "shellcheck" shellcheck \
        "$launcher" \
        "$project_root/lib/uuyc-linux-controller/uuyc-decoder-selector" \
        "$project_root/lib/uuyc-linux-controller/uuyc-autostart-bridge" \
        "$project_root/lib/uuyc-linux-controller/uuyc-sleep-bridge" \
        "$project_root/lib/uuyc-linux-controller/uuyc-wol-bridge" \
        "$project_root/lib/uuyc-linux-controller/uuyc-wol-configure" \
        "$project_root/lib/uuyc-linux-controller/uuyc-update-bridge" \
        "$project_root/scripts/install-user.sh" \
        "$project_root/scripts/build-nvdec-probe.sh" \
        "$project_root/scripts/build-update-blocker.sh" \
        "$project_root/scripts/build-wevtapi.sh" \
        "$project_root/packaging/build-deb.sh" \
        "$project_root/packaging/debian/postinst" \
        "$project_root/packaging/debian/postrm" \
        "$project_root/tests/integration-mock.sh" \
        "$project_root/tests/autostart-bridge.sh" \
        "$project_root/tests/sleep-bridge.sh" \
        "$project_root/tests/wol-bridge.sh" \
        "$project_root/tests/update-bridge.sh" \
        "$project_root/tests/keyboard-bridge.sh" \
        "$project_root/tests/run.sh" \
        "$project_root/tests/fixtures/bin/curl" \
        "$project_root/tests/fixtures/bin/autostart-wine" \
        "$project_root/tests/fixtures/bin/sleep-systemd-inhibit" \
        "$project_root/tests/fixtures/bin/uuyc-tray-proxy" \
        "$project_root/tests/fixtures/bin/zenity" \
        "$project_root/tests/fixtures/bin/keyboard-gsettings" \
        "$project_root/tests/fixtures/bin/wine" \
        "$project_root/tests/fixtures/bin/wineboot" \
        "$project_root/tests/fixtures/bin/winecfg" \
        "$project_root/tests/fixtures/bin/wineserver" \
        "$project_root/tests/fixtures/bin/systemctl" \
        "$project_root/tests/fixtures/bin/systemd-run"
else
    printf 'SKIP shellcheck（未安装）\n'
fi

if ((failures > 0)); then
    printf '%d 项检查失败。\n' "$failures" >&2
    exit 1
fi

printf '所有离线检查通过。\n'
