/* Wine display notification companion for PJLAB. No display modes are set.
 * NtUserCallNoParam_DisplayModeChanged is the notification used by Wine's
 * own winex11.drv XRandR callback. This build is bound to its header ABI.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#ifndef PJLAB_WINE_VERSION
#error Build with the exact Wine version corresponding to the ntuser ABI
#endif
enum {
    NtUserCallNoParam_GetDesktopWindow,
    NtUserCallNoParam_GetDialogBaseUnits,
    NtUserCallNoParam_GetLastInputTime,
    NtUserCallNoParam_GetProgmanWindow,
    NtUserCallNoParam_GetShellWindow,
    NtUserCallNoParam_GetTaskmanWindow,
    NtUserCallNoParam_DisplayModeChanged
};
int main(int argc,char **argv) {
    const char *(__cdecl *version)(void)=(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"),"wine_get_version");
    ULONG_PTR (WINAPI *notify)(ULONG)=(void*)GetProcAddress(LoadLibraryA("win32u.dll"),"NtUserCallNoParam");
    if(!version || strcmp(version(),PJLAB_WINE_VERSION) || !notify) {
        fprintf(stderr,"Unsupported Wine display notification ABI; rebuild for installed Wine.\n");return 78;
    }
    SetProcessDPIAware();
    int before_w=GetSystemMetrics(SM_CXVIRTUALSCREEN),before_h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int query=argc>1 && !strcmp(argv[1],"--query");
    if(!query) {
        notify(NtUserCallNoParam_DisplayModeChanged);
        /* Applications also need a notification to recreate capture buffers.
         * The Wine internal refresh updates device data, but normally routes
         * notification through the desktop owner, which may be missing.
         */
        DWORD_PTR result=0;
        SendMessageTimeoutA(HWND_BROADCAST,WM_DISPLAYCHANGE,32,
            MAKELPARAM(GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)),
            SMTO_ABORTIFHUNG,500,&result);
    }
    int w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    printf("before=%dx%d after=%dx%d refreshed=%d wine=%s\n",before_w,before_h,w,h,!query,version());
    if(!query && argc==3) {
        int expected_w=0,expected_h=0;
        if(sscanf(argv[1],"%d",&expected_w)!=1 || sscanf(argv[2],"%d",&expected_h)!=1) return 64;
        if(expected_w!=w || expected_h!=h) return 2;
    }
    return 0;
}
