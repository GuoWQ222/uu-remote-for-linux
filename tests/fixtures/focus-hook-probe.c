#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>

typedef DWORD(WINAPI *initialize_hook_fn)(LPVOID);
typedef DWORD(WINAPI *focus_hook_status_fn)(void);

static volatile LONG app_deactivate_messages;
static volatile LONG window_deactivate_messages;
static volatile LONG nonclient_deactivate_messages;

static LRESULT CALLBACK probe_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    (void)window;
    (void)lparam;
    if (message == WM_ACTIVATEAPP && !wparam) {
        InterlockedIncrement(&app_deactivate_messages);
    } else if (
        message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE
    ) {
        InterlockedIncrement(&window_deactivate_messages);
    } else if (message == WM_NCACTIVATE && !wparam) {
        InterlockedIncrement(&nonclient_deactivate_messages);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static LRESULT CALLBACK keyboard_callback(
    int code,
    WPARAM wparam,
    LPARAM lparam
) {
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static void dispatch_probe_message(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    MSG probe;

    ZeroMemory(&probe, sizeof(probe));
    probe.hwnd = window;
    probe.message = message;
    probe.wParam = wparam;
    probe.lParam = lparam;
    DispatchMessageW(&probe);
}

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous,
    LPSTR command_line,
    int show_command
) {
    WNDCLASSW window_class;
    HWND main_window;
    HWND helper_window;
    HWND video_window;
    HWND modal_window;
    HMODULE qt_core;
    HMODULE hook_module;
    FARPROC procedure;
    initialize_hook_fn initialize_hook;
    focus_hook_status_fn focus_hook_status;
    HHOOK first_hook;
    HHOOK reused_hook;
    DWORD external_pid = 0;
    DWORD external_thread;
    int deferred;
    int reused;
    int released;
    int storms_detected;
    int storms_resolved;
    int blocked_activations;
    int modal_latches;
    int post_modal_handoffs;
    int index;
    int expected_app_deactivate = 0;

    (void)previous;
    (void)command_line;
    (void)show_command;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = probe_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"UURemoteFocusProbe";
    if (!RegisterClassW(&window_class)) {
        return 1;
    }
    main_window = CreateWindowExW(
        0,
        window_class.lpszClassName,
        L"\u7f51\u6613UU\u8fdc\u7a0b",
        WS_OVERLAPPEDWINDOW,
        20,
        20,
        320,
        200,
        NULL,
        NULL,
        instance,
        NULL
    );
    helper_window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        window_class.lpszClassName,
        L"UU Remote helper",
        WS_POPUP,
        40,
        40,
        120,
        80,
        main_window,
        NULL,
        instance,
        NULL
    );
    video_window = CreateWindowExW(
        0,
        window_class.lpszClassName,
        L"\u663e\u793a\u5c4f 1 (S85)",
        WS_OVERLAPPEDWINDOW,
        380,
        20,
        640,
        400,
        NULL,
        NULL,
        instance,
        NULL
    );
    modal_window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        window_class.lpszClassName,
        L"\u63a5\u7ba1\u8bbe\u5907",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        120,
        80,
        280,
        160,
        main_window,
        NULL,
        instance,
        NULL
    );
    if (!main_window || !helper_window || !video_window || !modal_window) {
        return 2;
    }
    qt_core = LoadLibraryW(L"Qt5Core.dll");
    hook_module = LoadLibraryW(L"uu-remote-input-hook.dll");
    if (!qt_core || !hook_module) {
        return 3;
    }
    procedure = GetProcAddress(
        hook_module, "UURemoteInputHookInitialize"
    );
    CopyMemory(&initialize_hook, &procedure, sizeof(initialize_hook));
    procedure = GetProcAddress(
        hook_module, "UURemoteFocusHookStatus"
    );
    CopyMemory(&focus_hook_status, &procedure, sizeof(focus_hook_status));
    if (
        !initialize_hook ||
        !focus_hook_status ||
        !initialize_hook(NULL) ||
        (focus_hook_status() & 127u) != 127u
    ) {
        return 4;
    }

    InterlockedExchange(&app_deactivate_messages, 0);
    InterlockedExchange(&window_deactivate_messages, 0);
    InterlockedExchange(&nonclient_deactivate_messages, 0);

    /* A real departure to another process must still reach Qt. */
    external_thread = GetWindowThreadProcessId(
        GetShellWindow(), &external_pid
    );
    if (
        external_thread &&
        external_pid &&
        external_pid != GetCurrentProcessId()
    ) {
        dispatch_probe_message(
            main_window,
            WM_ACTIVATEAPP,
            FALSE,
            (LPARAM)external_thread
        );
        expected_app_deactivate = 1;
        if (app_deactivate_messages != expected_app_deactivate) {
            return 5;
        }
    }
    /* Intra-controller transfers must not deactivate the remote viewer. */
    dispatch_probe_message(
        main_window,
        WM_ACTIVATEAPP,
        FALSE,
        (LPARAM)GetCurrentThreadId()
    );
    dispatch_probe_message(
        main_window,
        WM_ACTIVATE,
        MAKEWPARAM(WA_INACTIVE, FALSE),
        (LPARAM)helper_window
    );
    if (
        app_deactivate_messages != expected_app_deactivate ||
        window_deactivate_messages != 0
    ) {
        return 6;
    }

    /*
     * Exercise the real WndProc path, not only DispatchMessageW's IAT.  The
     * production Qt qwindows backend takes this direct path under Wine.
     */
    ShowWindow(main_window, SW_SHOWNA);
    ShowWindow(video_window, SW_SHOWNA);
    SetWindowPos(
        video_window,
        HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
    );
    BringWindowToTop(video_window);
    SetActiveWindow(video_window);
    SetForegroundWindow(video_window);
    for (index = 0; index < 40; ++index) {
        SendMessageW(
            main_window,
            WM_ACTIVATE,
            MAKEWPARAM(WA_INACTIVE, FALSE),
            (LPARAM)video_window
        );
        SendMessageW(
            video_window,
            WM_ACTIVATE,
            MAKEWPARAM(WA_ACTIVE, FALSE),
            (LPARAM)main_window
        );
    }
    Sleep(350);
    /* Programmatic attempts to re-raise the home window must be absorbed. */
    SetForegroundWindow(main_window);
    SetActiveWindow(main_window);
    BringWindowToTop(main_window);
    SetWindowPos(
        main_window,
        HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE
    );
    ShowWindow(main_window, SW_SHOW);

    /* The takeover dialog owns focus until it closes, then hands off once. */
    ShowWindow(modal_window, SW_SHOW);
    Sleep(350);
    ShowWindow(modal_window, SW_HIDE);
    Sleep(350);

    first_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, keyboard_callback, instance, 0
    );
    if (!first_hook || !UnhookWindowsHookEx(first_hook)) {
        return 7;
    }
    reused_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL, keyboard_callback, instance, 0
    );
    if (!reused_hook || reused_hook != first_hook) {
        return 8;
    }
    if (
        external_thread &&
        external_pid &&
        external_pid != GetCurrentProcessId()
    ) {
        dispatch_probe_message(
            main_window,
            WM_ACTIVATEAPP,
            FALSE,
            (LPARAM)external_thread
        );
        ++expected_app_deactivate;
        if (app_deactivate_messages != expected_app_deactivate) {
            return 12;
        }
    }
    ShowWindow(main_window, SW_SHOWNA);
    ShowWindow(helper_window, SW_SHOWNA);
    SetForegroundWindow(helper_window);
    Sleep(50);
    InterlockedExchange(&nonclient_deactivate_messages, 0);
    dispatch_probe_message(main_window, WM_NCACTIVATE, FALSE, 0);
    if (nonclient_deactivate_messages != 0) {
        return 9;
    }
    if (!UnhookWindowsHookEx(reused_hook)) {
        return 10;
    }
    Sleep(1400);
    deferred = GetPrivateProfileIntW(
        L"keyboard_hook",
        L"deferred",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    reused = GetPrivateProfileIntW(
        L"keyboard_hook",
        L"reused",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    released = GetPrivateProfileIntW(
        L"keyboard_hook",
        L"released",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    storms_detected = GetPrivateProfileIntW(
        L"window_state",
        L"storms_detected",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    storms_resolved = GetPrivateProfileIntW(
        L"window_state",
        L"storms_resolved",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    blocked_activations = GetPrivateProfileIntW(
        L"window_state",
        L"blocked_activations",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    modal_latches = GetPrivateProfileIntW(
        L"window_state",
        L"modal_latches",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    post_modal_handoffs = GetPrivateProfileIntW(
        L"window_state",
        L"post_modal_handoffs",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    if (
        deferred < 2 || reused < 1 || released < 1 ||
        storms_detected < 1 || storms_resolved < 1 ||
        blocked_activations < 1 || modal_latches < 1 ||
        post_modal_handoffs < 1
    ) {
        return 11;
    }
    DestroyWindow(modal_window);
    DestroyWindow(video_window);
    DestroyWindow(helper_window);
    DestroyWindow(main_window);
    FreeLibrary(qt_core);
    return 0;
}
