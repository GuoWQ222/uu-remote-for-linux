#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>

typedef DWORD(WINAPI *initialize_hook_fn)(LPVOID);
typedef DWORD(WINAPI *focus_hook_status_fn)(void);
typedef DWORD(WINAPI *event_loop_guard_self_test_fn)(void);
typedef DWORD(WINAPI *sticky_null_guard_self_test_fn)(void);
typedef DWORD(WINAPI *ui_health_evidence_self_test_fn)(void);
typedef DWORD(WINAPI *executable_patch_self_test_fn)(void);

static volatile LONG app_deactivate_messages;
static volatile LONG window_deactivate_messages;
static volatile LONG nonclient_deactivate_messages;
static volatile LONG explicit_nonclient_deactivate_messages;
static volatile LONG nonclient_right_button_down_messages;
static volatile LONG home_size_messages;
static volatile LONG outer_window_proc_calls;
static volatile LONG explicit_probe_active;
static HWND explicit_probe_window;
static WNDPROC outer_window_proc_next;

static LRESULT CALLBACK outer_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    InterlockedIncrement(&outer_window_proc_calls);
    return CallWindowProcW(
        outer_window_proc_next, window, message, wparam, lparam
    );
}

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
        if (
            InterlockedCompareExchange(&explicit_probe_active, 0, 0) &&
            window == explicit_probe_window
        ) {
            InterlockedIncrement(&explicit_nonclient_deactivate_messages);
        }
    } else if (message == WM_NCRBUTTONDOWN) {
        InterlockedIncrement(&nonclient_right_button_down_messages);
    } else if (message == WM_SIZE) {
        InterlockedIncrement(&home_size_messages);
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
    BOOL track_nonclient = message == WM_NCACTIVATE && !wparam;

    ZeroMemory(&probe, sizeof(probe));
    probe.hwnd = window;
    probe.message = message;
    probe.wParam = wparam;
    probe.lParam = lparam;
    if (track_nonclient) {
        explicit_probe_window = window;
        InterlockedExchange(&explicit_nonclient_deactivate_messages, 0);
        InterlockedExchange(&explicit_probe_active, 1);
    }
    DispatchMessageW(&probe);
    if (track_nonclient) {
        InterlockedExchange(&explicit_probe_active, 0);
        explicit_probe_window = NULL;
    }
}

static BOOL create_home_show_request(void) {
    HANDLE request = CreateFileW(
        L"C:\\uu-remote-home-show.request",
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (request == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    CloseHandle(request);
    return TRUE;
}

static BOOL age_home_show_request(DWORD age_ms) {
    HANDLE request;
    FILETIME current_time;
    FILETIME modified_time;
    ULARGE_INTEGER modified;
    BOOL result;

    request = CreateFileW(
        L"C:\\uu-remote-home-show.request",
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (request == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    GetSystemTimeAsFileTime(&current_time);
    modified.LowPart = current_time.dwLowDateTime;
    modified.HighPart = current_time.dwHighDateTime;
    modified.QuadPart -= (ULONGLONG)age_ms * 10000u;
    modified_time.dwLowDateTime = modified.LowPart;
    modified_time.dwHighDateTime = modified.HighPart;
    result = SetFileTime(request, NULL, NULL, &modified_time);
    CloseHandle(request);
    return result;
}

static void pump_messages_for(DWORD duration_ms) {
    ULONGLONG deadline = GetTickCount64() + duration_ms;

    while (GetTickCount64() < deadline) {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
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
    event_loop_guard_self_test_fn event_loop_guard_self_test;
    sticky_null_guard_self_test_fn sticky_null_guard_self_test;
    ui_health_evidence_self_test_fn ui_health_evidence_self_test;
    executable_patch_self_test_fn executable_patch_self_test;
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
    int nonclient_right_clicks_suppressed;
    int home_reopen_blocked;
    int home_show_authorized;
    int home_repaint_pulses;
    int apply_posted;
    int heartbeat_before;
    int heartbeat_after;
    int health_pings_sent;
    int health_pings_acked;
    int health_pings_acked_after_pause;
    int health_timeouts;
    int consecutive_timeouts;
    int hard_stalls_detected;
    int arbitration_posts;
    int no_livelock_suppressions;
    LONG outer_calls_before;
    ULONGLONG right_click_started;
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
    ShowWindow(main_window, SW_SHOWNA);
    InterlockedExchange(&home_size_messages, 0);
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
    procedure = GetProcAddress(
        hook_module, "UURemoteEventLoopGuardSelfTest"
    );
    CopyMemory(
        &event_loop_guard_self_test,
        &procedure,
        sizeof(event_loop_guard_self_test)
    );
    procedure = GetProcAddress(
        hook_module, "UURemoteStickyNullGuardSelfTest"
    );
    CopyMemory(
        &sticky_null_guard_self_test,
        &procedure,
        sizeof(sticky_null_guard_self_test)
    );
    procedure = GetProcAddress(
        hook_module, "UURemoteUIHealthEvidenceSelfTest"
    );
    CopyMemory(
        &ui_health_evidence_self_test,
        &procedure,
        sizeof(ui_health_evidence_self_test)
    );
    procedure = GetProcAddress(
        hook_module, "UURemoteExecutablePatchSelfTest"
    );
    CopyMemory(
        &executable_patch_self_test,
        &procedure,
        sizeof(executable_patch_self_test)
    );
    if (
        !initialize_hook ||
        !focus_hook_status ||
        !event_loop_guard_self_test ||
        !event_loop_guard_self_test() ||
        !sticky_null_guard_self_test ||
        !sticky_null_guard_self_test() ||
        !ui_health_evidence_self_test ||
        !ui_health_evidence_self_test() ||
        !executable_patch_self_test ||
        !executable_patch_self_test() ||
        !initialize_hook(NULL) ||
        (focus_hook_status() & 1023u) != 1023u
    ) {
        return 4;
    }
    pump_messages_for(900);
    if (InterlockedCompareExchange(&home_size_messages, 0, 0) < 2) {
        return 32;
    }

    /*
     * Qt may install another WndProc after the hook.  That outer procedure
     * already chains to our hook, so the hook must never move itself above
     * the new procedure: doing so creates hook -> outer -> hook recursion.
     */
    SetLastError(ERROR_SUCCESS);
    outer_window_proc_next = (WNDPROC)SetWindowLongPtrW(
        main_window, GWLP_WNDPROC, (LONG_PTR)outer_window_proc
    );
    if (
        (!outer_window_proc_next && GetLastError() != ERROR_SUCCESS) ||
        outer_window_proc_next == outer_window_proc
    ) {
        return 13;
    }
    Sleep(200);
    if (
        (WNDPROC)GetWindowLongPtrW(main_window, GWLP_WNDPROC) !=
            outer_window_proc
    ) {
        return 14;
    }
    outer_calls_before = outer_window_proc_calls;
    SendMessageW(main_window, WM_NULL, 0, 0);
    if (outer_window_proc_calls != outer_calls_before + 1) {
        return 15;
    }

    /*
     * A frameless Qt caption press must never enter Wine's blocking default
     * handler: the X11 release can be lost and leave the UI thread waiting
     * forever. A non-caption right click remains available to the original
     * Qt procedure.
     */
    InterlockedExchange(&nonclient_right_button_down_messages, 0);
    outer_calls_before = outer_window_proc_calls;
    right_click_started = GetTickCount64();
    dispatch_probe_message(
        main_window,
        WM_NCRBUTTONDOWN,
        HTCAPTION,
        MAKELPARAM(40, 20)
    );
    if (
        GetTickCount64() - right_click_started > 250u ||
        nonclient_right_button_down_messages != 0 ||
        outer_window_proc_calls != outer_calls_before + 1
    ) {
        return 24;
    }
    dispatch_probe_message(
        main_window,
        WM_NCRBUTTONDOWN,
        HTCLIENT,
        MAKELPARAM(40, 40)
    );
    if (nonclient_right_button_down_messages != 1) {
        return 25;
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
    /*
     * App-wide false deactivation is invalid for an intra-process transfer
     * and stays suppressed. Per-window activation must reach Qt outside an
     * active storm latch so its window state cannot become permanently stale.
     */
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
        window_deactivate_messages != 1
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
    pump_messages_for(500);
    InterlockedExchange(&window_deactivate_messages, 0);
    dispatch_probe_message(
        video_window,
        WM_ACTIVATE,
        MAKEWPARAM(WA_INACTIVE, FALSE),
        (LPARAM)main_window
    );
    if (window_deactivate_messages != 0) {
        return 21;
    }
    /*
     * Closing the home window while a remote-video window remains visible
     * is user intent. UU's application-activation callback may repeatedly
     * try to show the home window; every automatic raise path must remain
     * blocked without disturbing the video window.
     */
    ShowWindow(main_window, SW_HIDE);
    Sleep(300);
    for (index = 0; index < 200; ++index) {
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
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
        );
        ShowWindow(main_window, SW_SHOW);
    }
    if (IsWindowVisible(main_window) || !IsWindowVisible(video_window)) {
        return 16;
    }

    /* An abandoned request must expire without reopening the home window. */
    if (
        !create_home_show_request() ||
        !age_home_show_request(60000u)
    ) {
        return 17;
    }
    pump_messages_for(700);
    if (
        IsWindowVisible(main_window) ||
        GetFileAttributesW(L"C:\\uu-remote-home-show.request") !=
            INVALID_FILE_ATTRIBUTES
    ) {
        return 18;
    }

    /*
     * The native tray's explicit --show request is only an authorization.
     * It must never map the native HWND on its own because that bypasses Qt's
     * QWidget visibility state and leaves WebView2 suspended behind a white
     * shell. The second GameViewer instance drives the real application show.
     */
    if (!create_home_show_request()) {
        return 22;
    }
    pump_messages_for(700);
    if (
        IsWindowVisible(main_window) ||
        GetFileAttributesW(L"C:\\uu-remote-home-show.request") ==
            INVALID_FILE_ATTRIBUTES
    ) {
        return 23;
    }
    InterlockedExchange(&home_size_messages, 0);
    ShowWindow(main_window, SW_SHOW);
    pump_messages_for(900);
    if (
        !IsWindowVisible(main_window) ||
        GetFileAttributesW(L"C:\\uu-remote-home-show.request") !=
            INVALID_FILE_ATTRIBUTES ||
        InterlockedCompareExchange(&home_size_messages, 0, 0) < 2
    ) {
        return 31;
    }

    /* Once no remote window remains visible, normal home showing is restored. */
    ShowWindow(main_window, SW_HIDE);
    ShowWindow(video_window, SW_HIDE);
    Sleep(300);
    ShowWindow(main_window, SW_SHOW);
    if (!IsWindowVisible(main_window)) {
        return 19;
    }
    ShowWindow(video_window, SW_SHOWNA);
    Sleep(300);

    /* The takeover dialog owns focus until it closes, then hands off once. */
    ShowWindow(modal_window, SW_SHOW);
    pump_messages_for(350);
    ShowWindow(modal_window, SW_HIDE);
    pump_messages_for(350);

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
        expected_app_deactivate = app_deactivate_messages + 1;
        dispatch_probe_message(
            main_window,
            WM_ACTIVATEAPP,
            FALSE,
            (LPARAM)external_thread
        );
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
    if (explicit_nonclient_deactivate_messages != 1) {
        return 33;
    }
    for (index = 0; index < 12; ++index) {
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
    pump_messages_for(200);
    InterlockedExchange(&nonclient_deactivate_messages, 0);
    dispatch_probe_message(video_window, WM_NCACTIVATE, FALSE, 0);
    if (explicit_nonclient_deactivate_messages != 0) {
        return 34;
    }
    if (!UnhookWindowsHookEx(reused_hook)) {
        return 10;
    }
    heartbeat_before = GetPrivateProfileIntW(
        L"worker",
        L"heartbeats",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    Sleep(1400);
    heartbeat_after = GetPrivateProfileIntW(
        L"worker",
        L"heartbeats",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    health_pings_sent = GetPrivateProfileIntW(
        L"ui_health",
        L"pings_sent",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    health_pings_acked = GetPrivateProfileIntW(
        L"ui_health",
        L"pings_acked",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
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
    nonclient_right_clicks_suppressed = GetPrivateProfileIntW(
        L"window_state",
        L"nonclient_right_clicks_suppressed",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    home_reopen_blocked = GetPrivateProfileIntW(
        L"home_window",
        L"reopen_blocked",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    home_show_authorized = GetPrivateProfileIntW(
        L"home_window",
        L"show_authorized",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    home_repaint_pulses = GetPrivateProfileIntW(
        L"home_window",
        L"repaint_pulses",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    apply_posted = GetPrivateProfileIntW(
        L"focus",
        L"apply_posted",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    if (
        deferred < 2 || reused < 1 || released < 1 ||
        storms_detected < 1 || storms_resolved < 1 ||
        blocked_activations < 1 || modal_latches < 1 ||
        post_modal_handoffs < 1 || home_reopen_blocked < 200 ||
        home_show_authorized < 1 || home_repaint_pulses < 2 ||
        nonclient_right_clicks_suppressed < 1 || apply_posted > 6
    ) {
        return 11;
    }
    if (heartbeat_before < 1 || heartbeat_after <= heartbeat_before) {
        return 20;
    }
    if (health_pings_sent < 1 || health_pings_acked < 1) {
        return 26;
    }

    /*
     * One isolated UI timeout without an observed event-loop livelock must
     * not produce a restart request. Incoming controlled sessions can
     * temporarily stop dispatching the selected top-level window in exactly
     * this way. A second consecutive timeout on the same HWND generation is
     * independent hard-stall evidence; the native tray still verifies that
     * the controlled RTC session is idle before restarting the controller.
     */
    DeleteFileW(L"C:\\uu-remote-controller-restart.request");
    /*
     * Leave more than one complete worker interval beyond the 10-second
     * threshold so a loaded Wine host cannot race the assertion below.
     */
    Sleep(13000);
    health_pings_acked_after_pause = GetPrivateProfileIntW(
        L"ui_health",
        L"pings_acked",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    health_timeouts = GetPrivateProfileIntW(
        L"ui_health",
        L"timeouts",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    arbitration_posts = GetPrivateProfileIntW(
        L"window_state",
        L"arbitration_posts",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    if (
        GetFileAttributesW(L"C:\\uu-remote-controller-restart.request") !=
        INVALID_FILE_ATTRIBUTES
    ) {
        return 27;
    }
    no_livelock_suppressions = GetPrivateProfileIntW(
        L"ui_health",
        L"no_livelock_suppressions",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    if (no_livelock_suppressions < 1) {
        return 28;
    }
    if (
        health_pings_acked_after_pause != health_pings_acked ||
        health_timeouts < 1
    ) {
        return 29;
    }
    if (arbitration_posts < 1 || arbitration_posts > 40) {
        return 30;
    }
    Sleep(12000);
    consecutive_timeouts = GetPrivateProfileIntW(
        L"ui_health",
        L"consecutive_timeouts",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    hard_stalls_detected = GetPrivateProfileIntW(
        L"ui_health",
        L"hard_stalls_detected",
        0,
        L"C:\\uu-remote-focus-hook-status.ini"
    );
    if (
        consecutive_timeouts < 2 || hard_stalls_detected < 1 ||
        GetFileAttributesW(L"C:\\uu-remote-controller-restart.request") ==
            INVALID_FILE_ATTRIBUTES
    ) {
        return 31;
    }
    DestroyWindow(modal_window);
    DestroyWindow(video_window);
    DestroyWindow(helper_window);
    DestroyWindow(main_window);
    FreeLibrary(qt_core);
    return 0;
}
