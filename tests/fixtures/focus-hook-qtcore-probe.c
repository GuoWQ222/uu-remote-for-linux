#define WIN32_LEAN_AND_MEAN
#include <windows.h>

__declspec(dllexport) LRESULT WINAPI ProbeQtDispatch(const MSG *message) {
    return DispatchMessageW(message);
}

/* Keep the three Qt event-dispatcher USER32 imports visible to the hook test. */
__declspec(dllexport) DWORD WINAPI ProbeQtEventDispatcherImports(HWND window) {
    MSG message;
    DWORD result;

    PostMessageW(window, WM_NULL, 0, 0);
    PeekMessageW(&message, NULL, 0, 0, PM_REMOVE);
    result = MsgWaitForMultipleObjectsEx(
        0, NULL, 0, QS_ALLINPUT, MWMO_ALERTABLE
    );
    return result;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
