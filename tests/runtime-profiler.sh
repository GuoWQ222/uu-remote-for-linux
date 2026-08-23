#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly profiler="$project_root/lib/uu-remote-for-linux/uu-remote-runtime-profiler"
readonly compiler="${UU_REMOTE_MINGW_CC:-x86_64-w64-mingw32-gcc}"

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
bin_dir="$test_root/install/bin"
mkdir -p "$bin_dir"

cat >"$test_root/server.c" <<'EOF'
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
__declspec(dllimport) ULONG WINAPI GetIfTable2(PVOID *);
__declspec(dllimport) int streamer_marker(void);
int main(void) {
    INPUT input = {0}; POINT point; PVOID table = 0;
    ULONG size = 0;
    SendInput(1, &input, sizeof(input)); GetKeyState(VK_CAPITAL);
    GetCursorPos(&point); GetAdaptersAddresses(0, 0, 0, 0, &size);
    GetAdaptersInfo(0, &size); GetIfTable2(&table);
    return streamer_marker();
}
EOF
cat >"$test_root/streamer.c" <<'EOF'
#include <windows.h>
#include <dxgi.h>
__declspec(dllexport) int streamer_marker(void) {
    INPUT input = {0}; CURSORINFO info = {sizeof(info)}; HDC dc = GetDC(0);
    IDXGIFactory1 *factory = 0;
    SendInput(1, &input, sizeof(input)); GetCursorInfo(&info);
    BitBlt(dc, 0, 0, 1, 1, dc, 0, 0, SRCCOPY);
    StretchBlt(dc, 0, 0, 1, 1, dc, 0, 0, 1, 1, SRCCOPY);
    CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    return factory != 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved) {
    (void)instance; (void)reason; (void)reserved; return TRUE;
}
EOF
cat >"$test_root/qtcore.c" <<'EOF'
#include <windows.h>
__declspec(dllexport) int qt_core_marker(void) {
    MSG message; PostMessageW(0, WM_NULL, 0, 0);
    PeekMessageW(&message, 0, 0, 0, PM_NOREMOVE);
    DispatchMessageW(&message);
    return (int)MsgWaitForMultipleObjectsEx(0, 0, 0, QS_ALLINPUT, 0);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved) {
    (void)instance; (void)reason; (void)reserved; return TRUE;
}
EOF
cat >"$test_root/controller.c" <<'EOF'
#include <windows.h>
__declspec(dllimport) int qt_core_marker(void);
int main(void) {
    MSG message; HHOOK hook;
    DispatchMessageW(&message);
    hook = SetWindowsHookExW(WH_KEYBOARD_LL, 0, 0, 0);
    UnhookWindowsHookEx(hook);
    return qt_core_marker();
}
EOF

"$compiler" -shared -Os -Wl,--no-insert-timestamp \
    -Wl,--out-implib,"$test_root/libstreamer.a" \
    -o "$bin_dir/streamer.dll" "$test_root/streamer.c" \
    -luser32 -lgdi32 -ldxgi -luuid
"$compiler" -shared -Os -Wl,--no-insert-timestamp \
    -Wl,--out-implib,"$test_root/libQt5Core.a" \
    -o "$bin_dir/Qt5Core.dll" "$test_root/qtcore.c" -luser32
"$compiler" -Os -Wl,--no-insert-timestamp \
    -o "$bin_dir/GameViewerServer.exe" "$test_root/server.c" \
    -L"$test_root" -lstreamer -luser32 -liphlpapi
"$compiler" -Os -Wl,--no-insert-timestamp \
    -o "$bin_dir/GameViewer.exe" "$test_root/controller.c" \
    -L"$test_root" -lQt5Core -luser32

profile="$test_root/profile.json"
"$profiler" derive "$test_root/install" 4.99.0.1 "$profile" --strict \
    >"$test_root/derive.out"
"$profiler" verify "$test_root/install" 4.99.0.1 "$profile" --strict \
    >"$test_root/verify.out"
grep -Fq 'controller_module=Qt5Core.dll' "$test_root/derive.out"
test "$("$profiler" get "$profile" controller_runtime_module)" = Qt5Core.dll

cp "$bin_dir/GameViewer.exe" "$test_root/controller.good"
printf '\0' >>"$bin_dir/GameViewer.exe"
if "$profiler" match "$test_root/install" 4.99.0.1 "$profile" --strict \
    >"$test_root/tamper.out" 2>"$test_root/tamper.err"; then
    printf '运行时兼容档案接受了哈希变化的控制器。\n' >&2
    exit 1
fi
grep -Fq '运行时兼容档案与当前 UU 二进制或导入语义不匹配' \
    "$test_root/tamper.err"
mv "$test_root/controller.good" "$bin_dir/GameViewer.exe"

# A future UU build may move from Qt5 to Qt6 without changing the hook's
# required event-loop imports.  The module name must be derived from imports,
# never hard-coded in the launcher or injected DLL.
"$compiler" -shared -Os -Wl,--no-insert-timestamp \
    -Wl,--out-implib,"$test_root/libQt6Core.a" \
    -o "$bin_dir/Qt6Core.dll" "$test_root/qtcore.c" -luser32
"$compiler" -Os -Wl,--no-insert-timestamp \
    -o "$bin_dir/GameViewer.exe" "$test_root/controller.c" \
    -L"$test_root" -lQt6Core -luser32
qt6_profile="$test_root/qt6-profile.json"
"$profiler" derive "$test_root/install" 4.99.0.2 "$qt6_profile" --strict \
    >"$test_root/qt6-derive.out"
grep -Fq 'controller_module=Qt6Core.dll' "$test_root/qt6-derive.out"
test "$("$profiler" get "$qt6_profile" controller_runtime_module)" = \
    Qt6Core.dll

# A changed import contract must fail before a future client is enabled.
sed '/GetKeyState/d' "$test_root/server.c" >"$test_root/server-incompatible.c"
"$compiler" -Os -Wl,--no-insert-timestamp \
    -o "$bin_dir/GameViewerServer.exe" "$test_root/server-incompatible.c" \
    -L"$test_root" -lstreamer -luser32 -liphlpapi
if "$profiler" derive "$test_root/install" 5.0.0.0 \
    "$test_root/incompatible.json" --strict \
    >"$test_root/incompatible.out" 2>"$test_root/incompatible.err"; then
    printf '缺少 GetKeyState 的新版服务端被意外接受。\n' >&2
    exit 1
fi
grep -Fq 'GameViewerServer.exe 的 USER32.dll 缺少：GetKeyState' \
    "$test_root/incompatible.err"

printf '运行时兼容档案生成、哈希绑定和缺失语义拒绝测试通过。\n'
