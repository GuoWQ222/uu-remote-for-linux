#define WIN32_LEAN_AND_MEAN
#include <windows.h>

__declspec(dllexport) LRESULT WINAPI ProbeQtDispatch(const MSG *message) {
    return DispatchMessageW(message);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
