#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
launcher="$project_root/bin/uu-remote-for-linux"
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
    "$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector"
check "tray proxy Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-tray-proxy"
check "keyboard bridge Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge"
check "input bridge Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge"
check "Mutter fix manager syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix"
check "Mutter root helper syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix-root"
check "serial Mutter installer template syntax" bash -n \
    "$project_root/packaging/install-with-mutter-fix.sh.in"
check "native frame helper self-test" /usr/bin/python3 -c \
    'import ctypes, sys; library = ctypes.CDLL(sys.argv[1]); library.uu_frame_helper_self_test.restype = ctypes.c_uint32; raise SystemExit(0 if library.uu_frame_helper_self_test() == 1 else 1)' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so"
check "PipeWire cursor bridge self-test" \
    "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor" \
    --self-test
check "Mutter fix manager isolation" \
    "$project_root/tests/mutter-fix-manager.sh"
check "serial Mutter installer isolation" \
    "$project_root/tests/serial-installer.sh"
check "Mutter capture priority" \
    "$project_root/packaging/mutter/test-capture-priority.sh"
check "Mutter release bundle" \
    "$project_root/packaging/mutter/verify-bundle.sh"
check "NVDEC profiler Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler"
check "NVDEC profiler self-test" \
    "$project_root/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler" \
    --self-test
check "NVENC encoder policy Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy"
check "NVENC encoder policy cache schema and watcher" \
    "$project_root/tests/encoder-policy.sh"
check "NVENC encoder lifecycle concurrency" \
    "$project_root/tests/nvenc-lifecycle.sh"
check "NVENC D3D11 texture validation" \
    "$project_root/tests/nvenc-texture-validation.sh"
check "autostart bridge syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-autostart-bridge"
check "sleep bridge syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-sleep-bridge"
check "system proxy bridge Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge"
check "WOL bridge syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge"
check "WOL profiler Python syntax" /usr/bin/python3 -c \
    'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text())' \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-profiler"
check "WOL profiler self-test" \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-profiler" --self-test
check "WOL root configurator syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure"
check "update bridge syntax" bash -n \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge"
check "systemctl fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/systemctl"
check "systemd-run fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/systemd-run"
check "install fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/install"
check "tray proxy fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/uu-remote-tray-proxy"
check "zenity fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/zenity"
check "keyboard gsettings fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/keyboard-gsettings"
check "proxy gsettings fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/proxy-gsettings"
check "WOL profiler fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/wol-profiler"
check "NVDEC profiler fixture syntax" bash -n \
    "$project_root/tests/fixtures/bin/hwdecode-profiler"
check "NVDEC probe builder syntax" bash -n \
    "$project_root/scripts/build-nvdec-probe.sh"
check "DXGI probe builder syntax" bash -n \
    "$project_root/scripts/build-dxgi-probe.sh"
check "NVENC D3D11 probe builder syntax" bash -n \
    "$project_root/scripts/build-nvenc-d3d11-probe.sh"
check "NVENC Wine bridge builder syntax" bash -n \
    "$project_root/scripts/build-nvencode-bridge.sh"
check "update blocker builder syntax" bash -n \
    "$project_root/scripts/build-update-blocker.sh"
check "input hook builder syntax" bash -n \
    "$project_root/scripts/build-input-hook.sh"
check "native frame helper builder syntax" bash -n \
    "$project_root/scripts/build-frame-helper.sh"
check "PipeWire cursor builder syntax" bash -n \
    "$project_root/scripts/build-pipewire-cursor.sh"
check "input injector concurrency policy" \
    "$project_root/tests/input-injector-concurrency.sh"
check "PowerShell bridge builder syntax" bash -n \
    "$project_root/scripts/build-powershell-bridge.sh"
check "user installer syntax" bash -n "$project_root/scripts/install-user.sh"
check "shim builder syntax" bash -n "$project_root/scripts/build-wevtapi.sh"
check "deb builder syntax" bash -n "$project_root/packaging/build-deb.sh"
check "postinst syntax" bash -n "$project_root/packaging/debian/postinst"
check "postrm syntax" bash -n "$project_root/packaging/debian/postrm"
check "desktop entry" desktop-file-validate \
    "$project_root/share/applications/uu-remote-for-linux.desktop"
if command -v appstreamcli >/dev/null 2>&1; then
    check "AppStream metadata" appstreamcli validate --no-net \
        "$project_root/share/metainfo/io.github.guowq222.uu_remote_for_linux.metainfo.xml"
else
    printf 'SKIP AppStream metadata（未安装 appstreamcli）\n'
fi
if command -v xmllint >/dev/null 2>&1; then
    check "AppStream XML" xmllint --noout \
        "$project_root/share/metainfo/io.github.guowq222.uu_remote_for_linux.metainfo.xml"
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
check "system proxy bridge integration" \
    "$project_root/tests/system-proxy-bridge.sh"
check "Wine WinHTTP system proxy" \
    "$project_root/tests/system-proxy-wine.sh"
check "WOL bridge integration" "$project_root/tests/wol-bridge.sh"
check "Wine WOL PowerShell bridge" "$project_root/tests/powershell-bridge.sh"
check "update bridge integration" "$project_root/tests/update-bridge.sh"
check "keyboard bridge integration" "$project_root/tests/keyboard-bridge.sh"
check "input bridge integration" "$project_root/tests/input-bridge.sh"
check "Wayland portal input bridge integration" \
    "$project_root/tests/wayland-input-bridge.sh"
check "Wine explicit input hook" "$project_root/tests/wine-input-hook.sh"
check "Wine controller focus stabilizer" \
    "$project_root/tests/wine-focus-hook.sh"

check "shim exists" test -s \
    "$project_root/lib/uu-remote-for-linux/wevtapi.dll"
check "decoder selector exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector"
check "native tray proxy exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-tray-proxy"
check "autostart bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-autostart-bridge"
check "sleep bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-sleep-bridge"
check "system proxy bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-system-proxy-bridge"
check "WOL bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge"
check "WOL root configurator exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure"
check "WOL automatic profiler exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-wol-profiler"
check "update bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge"
check "keyboard bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-keyboard-bridge"
check "input bridge exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-bridge"
check "Mutter fix manager exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix"
check "Mutter root helper exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix-root"
# The snippet intentionally defers "$1" expansion to the nested shell.
# shellcheck disable=SC2016
check "native frame helper is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB.*shared object.*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-frame-helper.so"
check "update compatibility profiles exist" test -s \
    "$project_root/lib/uu-remote-for-linux/update-compatibility.tsv"
check "WOL compatibility profiles exist" test -s \
    "$project_root/lib/uu-remote-for-linux/wol-compatibility.tsv"
check "NVDEC compatibility profiles exist" test -s \
    "$project_root/lib/uu-remote-for-linux/hwdecode-compatibility.tsv"
check "NVDEC automatic profiler exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-hwdecode-profiler"
check "DXGI adapter probe exists" test -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe"
# shellcheck disable=SC2016
check "DXGI adapter probe is Win64 console PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (console).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe"
# shellcheck disable=SC2016
check "DXGI adapter probe imports factory API" bash -c \
    'objdump -p "$1" | grep -q "CreateDXGIFactory1"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-dxgi-probe.exe"
check "NVENC encoder policy exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-encoder-policy"
check "NVENC D3D11 probe exists" test -s \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe"
# shellcheck disable=SC2016
check "NVENC D3D11 probe is Win64 console PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (console).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe"
# shellcheck disable=SC2016
check "NVENC D3D11 probe imports D3D11" bash -c \
    'objdump -p "$1" | grep -qi "D3D11CreateDevice"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvenc-d3d11-probe.exe"
check "NVDEC probe exists" test -x \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvdec-probe"
# shellcheck disable=SC2016
check "NVDEC probe is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB.*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-nvdec-probe"
# shellcheck disable=SC2016
check "PipeWire cursor bridge is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB.*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-pipewire-cursor"
# The snippets intentionally defer "$1" expansion to the nested shell.
# shellcheck disable=SC2016
check "shim is PE DLL" bash -c \
    'file "$1" | grep -q "PE32+ executable (DLL).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/wevtapi.dll"
# shellcheck disable=SC2016
check "shim exports" bash -c \
    'objdump -p "$1" | grep -q "EvtOpenPublisherMetadata"' _ \
    "$project_root/lib/uu-remote-for-linux/wevtapi.dll"
# shellcheck disable=SC2016
check "shim preloads input hook" bash -c \
    'objdump -p "$1" | grep -q "uu-remote-input-hook.dll"' _ \
    "$project_root/lib/uu-remote-for-linux/wevtapi.dll"
# shellcheck disable=SC2016
check "shim DllMain only marks input hook as preloaded" bash -c \
    'objdump -p "$1" | grep -q "UURemoteInputHookMarkPreloaded" && ! objdump -p "$1" | grep -q "UURemoteInputHookInitialize"' _ \
    "$project_root/lib/uu-remote-for-linux/wevtapi.dll"
# shellcheck disable=SC2016
check "update blocker is Win64 GUI PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (GUI).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe"
# shellcheck disable=SC2016
check "update blocker creates native handoff request" bash -c \
    'objdump -p "$1" | grep -q "CreateFileA" && strings -a "$1" | grep -q "uu-remote-upstream-update.request"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-update-blocker.exe"
# The snippets intentionally defer "$1" expansion to the nested shell.
# shellcheck disable=SC2016
check "input hook is Win64 DLL" bash -c \
    'file "$1" | grep -q "PE32+ executable (DLL).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports version" bash -c \
    'objdump -p "$1" | grep -q "UURemoteInputHookVersion"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports Wayland frame status" bash -c \
    'objdump -p "$1" | grep -q "UURemoteFrameHookStatus"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports Wayland frame bounds self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteFrameBoundsSelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports Wayland stable-frame self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteFrameSnapshotSelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports Wayland frame identity self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteFrameMappingIdentitySelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports controller focus status" bash -c \
    'objdump -p "$1" | grep -q "UURemoteFocusHookStatus"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports event-loop self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteEventLoopGuardSelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports sticky-null self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteStickyNullGuardSelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports UI-health evidence self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteUIHealthEvidenceSelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports executable-patch self-test" bash -c \
    'objdump -p "$1" | grep -q "UURemoteExecutablePatchSelfTest"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input hook exports WOL status" bash -c \
    'objdump -p "$1" | grep -q "UURemoteWolHookStatus"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-hook.dll"
# shellcheck disable=SC2016
check "input injector is Win64 PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (console).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
# shellcheck disable=SC2016
check "input injector exports version" bash -c \
    'objdump -p "$1" | grep -q "UURemoteInputInjectorVersion"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-input-injector.exe"
# shellcheck disable=SC2016
check "WOL PowerShell bridge is Win64 PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (console).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/uu-remote-powershell-bridge.exe"
# The snippets intentionally defer "$1" expansion to the nested shell.
# shellcheck disable=SC2016
check "hardware bridge manifest" bash -c \
    'cd "$1" && sha256sum --check MANIFEST.sha256' _ \
    "$project_root/lib/uu-remote-for-linux/hwdecode"
# shellcheck disable=SC2016
check "nvcuda bridge is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB shared object"' _ \
    "$project_root/lib/uu-remote-for-linux/hwdecode/wine/x86_64-unix/nvcuda.dll.so"
# shellcheck disable=SC2016
check "nvencodeapi bridge is ELF" bash -c \
    'file "$1" | grep -q "ELF 64-bit LSB shared object"' _ \
    "$project_root/lib/uu-remote-for-linux/hwdecode/wine/x86_64-unix/nvencodeapi64.dll.so"
# shellcheck disable=SC2016
check "nvencodeapi fake DLL is PE" bash -c \
    'file "$1" | grep -q "PE32+ executable (DLL).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/hwdecode/wine/x86_64-windows/nvencodeapi64.dll"
# shellcheck disable=SC2016
check "DXVK d3d11 is PE DLL" bash -c \
    'file "$1" | grep -q "PE32+ executable (DLL).*x86-64"' _ \
    "$project_root/lib/uu-remote-for-linux/hwdecode/dxvk/x64/d3d11.dll"
if command -v shellcheck >/dev/null 2>&1; then
    check "shellcheck" shellcheck \
        "$launcher" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-decoder-selector" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-autostart-bridge" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-sleep-bridge" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-wol-bridge" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-wol-configure" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-update-bridge" \
        "$project_root/scripts/install-user.sh" \
        "$project_root/scripts/build-nvdec-probe.sh" \
        "$project_root/scripts/build-dxgi-probe.sh" \
        "$project_root/scripts/build-nvenc-d3d11-probe.sh" \
        "$project_root/scripts/build-nvencode-bridge.sh" \
        "$project_root/scripts/build-update-blocker.sh" \
        "$project_root/scripts/build-input-hook.sh" \
        "$project_root/scripts/build-frame-helper.sh" \
        "$project_root/scripts/build-pipewire-cursor.sh" \
        "$project_root/scripts/build-powershell-bridge.sh" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix" \
        "$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix-root" \
        "$project_root/packaging/install-with-mutter-fix.sh.in" \
        "$project_root/packaging/mutter/test-capture-priority.sh" \
        "$project_root/packaging/mutter/verify-bundle.sh" \
        "$project_root/tests/mutter-fix-manager.sh" \
        "$project_root/tests/serial-installer.sh" \
        "$project_root/scripts/build-wevtapi.sh" \
        "$project_root/packaging/build-deb.sh" \
        "$project_root/packaging/debian/postinst" \
        "$project_root/packaging/debian/postrm" \
        "$project_root/tests/integration-mock.sh" \
        "$project_root/tests/autostart-bridge.sh" \
        "$project_root/tests/sleep-bridge.sh" \
        "$project_root/tests/system-proxy-bridge.sh" \
        "$project_root/tests/system-proxy-wine.sh" \
        "$project_root/tests/wol-bridge.sh" \
        "$project_root/tests/update-bridge.sh" \
        "$project_root/tests/keyboard-bridge.sh" \
        "$project_root/tests/input-bridge.sh" \
        "$project_root/tests/wayland-input-bridge.sh" \
        "$project_root/tests/input-injector-concurrency.sh" \
        "$project_root/tests/nvenc-lifecycle.sh" \
        "$project_root/tests/nvenc-texture-validation.sh" \
        "$project_root/tests/wine-input-hook.sh" \
        "$project_root/tests/wine-focus-hook.sh" \
        "$project_root/tests/powershell-bridge.sh" \
        "$project_root/tests/run.sh" \
        "$project_root/tests/fixtures/bin/curl" \
        "$project_root/tests/fixtures/bin/autostart-wine" \
        "$project_root/tests/fixtures/bin/sleep-systemd-inhibit" \
        "$project_root/tests/fixtures/bin/uu-remote-tray-proxy" \
        "$project_root/tests/fixtures/bin/zenity" \
        "$project_root/tests/fixtures/bin/keyboard-gsettings" \
        "$project_root/tests/fixtures/bin/proxy-gsettings" \
        "$project_root/tests/fixtures/bin/hwdecode-profiler" \
        "$project_root/tests/fixtures/bin/wine" \
        "$project_root/tests/fixtures/bin/wineboot" \
        "$project_root/tests/fixtures/bin/winecfg" \
        "$project_root/tests/fixtures/bin/wineserver" \
        "$project_root/tests/fixtures/bin/systemctl" \
        "$project_root/tests/fixtures/bin/systemd-run" \
        "$project_root/tests/fixtures/bin/install"
else
    printf 'SKIP shellcheck（未安装）\n'
fi

if ((failures > 0)); then
    printf '%d 项检查失败。\n' "$failures" >&2
    exit 1
fi

printf '所有离线检查通过。\n'
