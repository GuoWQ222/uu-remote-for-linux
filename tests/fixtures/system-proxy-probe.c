#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

int wmain(void) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config;

    ZeroMemory(&config, sizeof(config));
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config)) {
        fwprintf(stderr, L"error=%lu\n", GetLastError());
        return 1;
    }

    wprintf(
        L"autodetect=%u\nproxy=%ls\nbypass=%ls\npac=%ls\n",
        config.fAutoDetect,
        config.lpszProxy ? config.lpszProxy : L"",
        config.lpszProxyBypass ? config.lpszProxyBypass : L"",
        config.lpszAutoConfigUrl ? config.lpszAutoConfigUrl : L""
    );
    GlobalFree(config.lpszProxy);
    GlobalFree(config.lpszProxyBypass);
    GlobalFree(config.lpszAutoConfigUrl);
    return 0;
}
