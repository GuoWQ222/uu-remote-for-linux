/*
 * UU Remote Win64 input hook.
 *
 * GameViewerServer falls back to USER32.SendInput when its proprietary Windows
 * HID driver is unavailable. Wine consumes that call inside the Windows
 * server and does not create a real Linux input event. This DLL patches the
 * GameViewerServer.exe and streamer.dll import tables. It reroutes SendInput
 * packets to the authenticated native bridge, supplies Portal frames to GDI
 * capture on Wayland, and presents the native physical Ethernet adapter
 * through Wine's IP Helper API when Linux WOL is enabled.
 * Keeping the compatibility changes process-local avoids changing Linux
 * routing and prevents duplicate relative mouse movement through Wine/XTest.
 *
 * The hook is preloaded as a dependency of the project's native wevtapi shim;
 * the explicit same-architecture injector remains as a restart and
 * late-loaded-streamer fallback.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define UUIP_MAGIC 0x50495555u
#define UUIP_VERSION 1u
#define UUIP_HELLO 1u
#define UUIP_MOUSE 2u
#define UUIP_KEYBOARD 3u
#define HOOK_VERSION 15u
#define UUWF_MAGIC 0x46575555u
#define UUWF_VERSION 1u
#define UUWF_HEADER_SIZE 64u
#define UUWF_BUFFER_COUNT 2u
#define UUWF_STALE_TIMEOUT_MS 2000u
#define FOCUS_UNHOOK_GRACE_MS 350u
#define FOCUS_INTERNAL_GRACE_MS 700u
#define FOCUS_STORM_WINDOW_MS 250u
#define FOCUS_STORM_THRESHOLD 12
#define FOCUS_LATCH_MS 1500u
#define FOCUS_MODAL_REFRESH_MS 300u
#define FOCUS_INTERNAL_APPLY_GRACE_MS 300u
#define FOCUS_APPLY_COOLDOWN_MS 750u
#define FOCUS_WINDOW_SCAN_MS 250u
#define FOCUS_MODULE_SCAN_MS 1000u
#define FOCUS_SHOW_REQUEST_TTL_MS 30000u
#define EVENT_LOOP_FALSE_WAKE_THRESHOLD 32u
#define EVENT_LOOP_FALSE_WAKE_WINDOW_MS 100u
#define EVENT_LOOP_BREAK_BACKOFF_MS 1u
#define UI_HEALTH_PING_INTERVAL_MS 1000u
#define UI_HEALTH_TIMEOUT_MS 10000u
#define UI_HEALTH_RESUME_GAP_MS 3000u
#define FOCUS_MAX_WINDOWS 64u
#define FOCUS_MAX_MODULES 256u
#define WM_UU_FOCUS_APPLY (WM_APP + 0x4b1u)
#define WM_UU_HOME_SHOW (WM_APP + 0x4b2u)
#define WM_UU_UI_HEALTH (WM_APP + 0x4b3u)
#define WM_QT_SENDPOSTEDEVENTS (WM_USER + 1u)

enum focus_window_role {
    FOCUS_ROLE_UNKNOWN = 0,
    FOCUS_ROLE_HOME = 1,
    FOCUS_ROLE_VIDEO = 2,
    FOCUS_ROLE_DIALOG = 3
};

enum hook_process_kind {
    HOOK_PROCESS_NONE = 0,
    HOOK_PROCESS_SERVER = 1,
    HOOK_PROCESS_CONTROLLER = 2
};

typedef UINT(WINAPI *send_input_fn)(UINT, LPINPUT, int);
typedef BOOL(WINAPI *get_cursor_info_fn)(PCURSORINFO);
typedef BOOL(WINAPI *get_cursor_pos_fn)(LPPOINT);
typedef BOOL(WINAPI *bit_blt_fn)(
    HDC, int, int, int, int, HDC, int, int, DWORD
);
typedef BOOL(WINAPI *stretch_blt_fn)(
    HDC, int, int, int, int, HDC, int, int, int, int, DWORD
);
typedef LRESULT(WINAPI *dispatch_message_w_fn)(const MSG *);
typedef HHOOK(WINAPI *set_windows_hook_ex_w_fn)(
    int, HOOKPROC, HINSTANCE, DWORD
);
typedef BOOL(WINAPI *unhook_windows_hook_ex_fn)(HHOOK);
typedef BOOL(WINAPI *set_foreground_window_fn)(HWND);
typedef HWND(WINAPI *set_active_window_fn)(HWND);
typedef BOOL(WINAPI *bring_window_to_top_fn)(HWND);
typedef BOOL(WINAPI *set_window_pos_fn)(
    HWND, HWND, int, int, int, int, UINT
);
typedef BOOL(WINAPI *show_window_fn)(HWND, int);
typedef BOOL(WINAPI *peek_message_w_fn)(LPMSG, HWND, UINT, UINT, UINT);
typedef DWORD(WINAPI *msg_wait_for_multiple_objects_ex_fn)(
    DWORD, const HANDLE *, DWORD, DWORD, DWORD
);
typedef BOOL(WINAPI *post_message_w_fn)(HWND, UINT, WPARAM, LPARAM);
typedef ULONG(WINAPI *get_adapters_addresses_fn)(
    ULONG,
    ULONG,
    PVOID,
    PIP_ADAPTER_ADDRESSES,
    PULONG
);
typedef ULONG(WINAPI *get_adapters_info_fn)(PIP_ADAPTER_INFO, PULONG);
typedef NETIO_STATUS(WINAPI *get_if_table2_fn)(PMIB_IF_TABLE2 *);

static send_input_fn original_send_input;
static get_cursor_info_fn original_get_cursor_info;
static get_cursor_pos_fn original_get_cursor_pos;
static bit_blt_fn original_bit_blt;
static stretch_blt_fn original_stretch_blt;
static dispatch_message_w_fn original_dispatch_message_w;
static set_windows_hook_ex_w_fn original_set_windows_hook_ex_w;
static unhook_windows_hook_ex_fn original_unhook_windows_hook_ex;
static set_foreground_window_fn original_set_foreground_window;
static set_active_window_fn original_set_active_window;
static bring_window_to_top_fn original_bring_window_to_top;
static set_window_pos_fn original_set_window_pos;
static show_window_fn original_show_window;
static peek_message_w_fn original_peek_message_w;
static msg_wait_for_multiple_objects_ex_fn
    original_msg_wait_for_multiple_objects_ex;
static post_message_w_fn original_post_message_w;
static get_adapters_addresses_fn original_get_adapters_addresses;
static get_adapters_info_fn original_get_adapters_info;
static get_if_table2_fn original_get_if_table2;
static HMODULE hook_module;
static WCHAR endpoint_path[MAX_PATH];
static CHAR wol_config_path[MAX_PATH];
static CHAR wol_status_path[MAX_PATH];
static WCHAR frame_path[MAX_PATH];
static CHAR frame_status_path[MAX_PATH];
static CHAR focus_status_path[MAX_PATH];
static CHAR focus_show_request_path[MAX_PATH];
static CHAR controller_restart_request_path[MAX_PATH];
static WCHAR controller_root_path[MAX_PATH];
static enum hook_process_kind process_kind;
static INIT_ONCE winsock_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE wol_config_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK endpoint_lock = SRWLOCK_INIT;
static SOCKET bridge_socket = INVALID_SOCKET;
static struct sockaddr_in bridge_address;
static unsigned char bridge_token[16];
static volatile LONG sequence_number;
static ULONGLONG endpoint_checked_at;
static BOOL endpoint_valid;
static BOOL force_cursor_visible;
static BOOL send_input_hooked;
static BOOL cursor_info_hooked;
static BOOL cursor_pos_hooked;
static BOOL bit_blt_hooked;
static BOOL stretch_blt_hooked;
static HMODULE patched_streamer_module;
static BOOL streamer_send_input_hooked;
static BOOL streamer_cursor_info_hooked;
static BOOL streamer_bit_blt_hooked;
static BOOL streamer_stretch_blt_hooked;
static BOOL adapters_addresses_hooked;
static BOOL adapters_info_hooked;
static BOOL if_table2_hooked;
static BOOL hook_preloaded;
static volatile LONG adapters_addresses_calls;
static volatile LONG adapters_addresses_patched;
static volatile LONG adapters_info_calls;
static volatile LONG adapters_info_patched;
static volatile LONG if_table2_calls;
static volatile LONG if_table2_patched;
static SRWLOCK cursor_lock = SRWLOCK_INIT;
static POINT tracked_cursor_position;
static BOOL tracked_cursor_valid;
static SRWLOCK frame_lock = SRWLOCK_INIT;
static HANDLE frame_file = INVALID_HANDLE_VALUE;
static HANDLE frame_mapping;
static unsigned char *frame_view;
static SIZE_T frame_view_size;
static uint32_t frame_last_sequence;
static ULONGLONG frame_sequence_changed_at;
static volatile LONG frame_hook_calls;
static volatile LONG frame_hook_rendered;
static volatile LONG frame_hook_fallbacks;
static BOOL controller_dispatch_hooked;
static BOOL qt_dispatch_hooked;
static BOOL controller_set_hook_hooked;
static BOOL controller_unhook_hooked;
static BOOL qt_peek_message_hooked;
static BOOL qt_msg_wait_hooked;
static BOOL qt_post_message_hooked;
static HANDLE focus_worker_event;
static HANDLE focus_worker_thread;
static SRWLOCK focus_keyboard_hook_lock = SRWLOCK_INIT;
static HHOOK focus_keyboard_hook;
static HOOKPROC focus_keyboard_proc;
static HINSTANCE focus_keyboard_module;
static DWORD focus_keyboard_thread_id;
static BOOL focus_keyboard_unhook_pending;
static ULONGLONG focus_keyboard_unhook_at;
static volatile LONG focus_worker_running;
static volatile LONG focus_suppressed_activate;
static volatile LONG focus_suppressed_activate_app;
static volatile LONG focus_stabilized_nonclient;
static volatile LONG focus_keyboard_installed;
static volatile LONG focus_keyboard_reused;
static volatile LONG focus_keyboard_deferred;
static volatile LONG focus_keyboard_released;
static volatile LONG64 focus_last_internal_transition;
static SRWLOCK focus_window_lock = SRWLOCK_INIT;
static SRWLOCK focus_rate_lock = SRWLOCK_INIT;
static SRWLOCK focus_module_lock = SRWLOCK_INIT;
static volatile LONG focus_subclassed_windows;
static volatile LONG focus_subclass_installations;
static volatile LONG focus_external_subclass_chains;
static volatile LONG focus_nonclient_right_clicks_suppressed;
static volatile LONG focus_activation_api_mask;
static volatile LONG focus_transition_messages;
static volatile LONG focus_storm_pending;
static volatile LONG focus_storms_detected;
static volatile LONG focus_storms_resolved;
static volatile LONG focus_blocked_activations;
static volatile LONG focus_modal_latches;
static volatile LONG focus_post_modal_handoffs;
static volatile LONG focus_user_overrides;
static volatile LONG focus_internal_transitions_ignored;
static volatile LONG focus_apply_posted;
static volatile LONG focus_apply_rate_limited;
static volatile LONG focus_home_hidden_by_user;
static volatile LONG focus_home_reopen_blocked;
static volatile LONG focus_home_show_authorized;
static volatile LONG focus_worker_heartbeats;
static volatile LONG event_loop_empty_queue_wakes;
static volatile LONG event_loop_guard_breaks;
static volatile LONG event_loop_messages_dequeued;
static volatile LONG event_loop_posted_forwarded;
static volatile LONG event_loop_posted_coalesced;
static volatile LONG64 event_loop_last_guard_break;
static volatile LONG ui_health_pings_sent;
static volatile LONG ui_health_pings_acked;
static volatile LONG ui_health_timeouts;
static volatile LONG ui_health_recovery_requests;
static volatile LONG ui_health_window_invalidations;
static volatile LONG ui_health_progress_cancellations;
static volatile LONG ui_health_no_livelock_suppressions;
static volatile LONG64 ui_health_ping_sent_at;
static volatile LONG64 ui_health_last_worker_tick;
static volatile LONG ui_health_recovery_requested;
static volatile LONG ui_health_ping_generation;
static volatile LONG ui_health_ping_guard_breaks;
static volatile LONG ui_health_ping_messages_dequeued;
static PVOID volatile ui_health_ping_window;
static volatile LONG focus_arbitration_pending;
static volatile LONG focus_module_scan_pending;
static volatile LONG focus_preferred_role;
static volatile LONG focus_latch_active;
static volatile LONG64 focus_latch_until;
static volatile LONG64 focus_last_internal_apply;
static volatile LONG64 focus_last_apply_posted;
static PVOID volatile focus_preferred_window;
static PVOID volatile focus_visible_remote_window;
static volatile LONG focus_window_generation;
static ULONGLONG focus_rate_window_started;
static LONG focus_rate_window_count;
static BOOL focus_modal_was_visible;
static DWORD event_loop_tls_index = TLS_OUT_OF_INDEXES;

struct event_loop_thread_state {
    BOOL last_peek_empty;
    DWORD consecutive_false_wakes;
    ULONGLONG false_wake_burst_started;
};

struct qt_posted_wakeup_entry {
    HWND window;
    BOOL pending;
};

#define QT_POSTED_WAKEUP_MAX_WINDOWS 16u
static SRWLOCK qt_posted_wakeup_lock = SRWLOCK_INIT;
static struct qt_posted_wakeup_entry
    qt_posted_wakeups[QT_POSTED_WAKEUP_MAX_WINDOWS];

struct focus_window_entry {
    HWND window;
    WNDPROC original_proc;
    WNDPROC external_proc;
    enum focus_window_role role;
};

static struct focus_window_entry focus_windows[FOCUS_MAX_WINDOWS];
static HMODULE focus_scanned_modules[FOCUS_MAX_MODULES];

static void write_wol_hook_status(void);
static BOOL wide_contains_ordinal_ci(
    const WCHAR *text,
    const WCHAR *needle
);

struct wol_config {
    BOOL enabled;
    ULONG reference_address;
    unsigned char mac[6];
    CHAR reference_ip[16];
    CHAR native_ip[16];
    CHAR gateway[16];
};

static struct wol_config configured_wol;
static IP_ADAPTER_GATEWAY_ADDRESS_LH synthetic_gateway_address;
static SOCKADDR_IN synthetic_gateway_sockaddr;
static CHAR synthetic_adapter_name[] = "uu-remote-linux-ethernet";
static WCHAR synthetic_adapter_description[] =
    L"UU Remote Linux Ethernet";

#pragma pack(push, 1)
struct packet_header {
    uint32_t magic;
    uint8_t version;
    uint8_t kind;
    uint16_t size;
    uint32_t sequence;
    unsigned char token[16];
};

struct hello_packet {
    struct packet_header header;
    uint32_t pid;
    uint32_t hook_version;
};

struct mouse_packet {
    struct packet_header header;
    int32_t dx;
    int32_t dy;
    uint32_t mouse_data;
    uint32_t flags;
    uint32_t time;
    uint64_t extra_info;
};

struct keyboard_packet {
    struct packet_header header;
    uint16_t virtual_key;
    uint16_t scan_code;
    uint32_t flags;
    uint32_t time;
    uint64_t extra_info;
};

struct wayland_frame_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t frame_size;
    uint32_t buffer_count;
    volatile uint32_t active_buffer;
    volatile uint32_t sequence;
    unsigned char reserved[24];
};
#pragma pack(pop)

typedef char wayland_frame_header_must_be_64_bytes[
    sizeof(struct wayland_frame_header) == UUWF_HEADER_SIZE ? 1 : -1
];

static BOOL CALLBACK initialize_winsock(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID *context
) {
    WSADATA data;
    (void)once;
    (void)parameter;
    (void)context;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return FALSE;
    }
    bridge_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return bridge_socket != INVALID_SOCKET;
}

static BOOL parse_hex_token(const WCHAR *text, unsigned char output[16]) {
    unsigned int index;
    if (lstrlenW(text) != 32) {
        return FALSE;
    }
    for (index = 0; index < 16; ++index) {
        WCHAR high = text[index * 2];
        WCHAR low = text[index * 2 + 1];
        unsigned int high_value;
        unsigned int low_value;
        if (high >= L'0' && high <= L'9') {
            high_value = (unsigned int)(high - L'0');
        } else if (high >= L'a' && high <= L'f') {
            high_value = (unsigned int)(high - L'a' + 10);
        } else if (high >= L'A' && high <= L'F') {
            high_value = (unsigned int)(high - L'A' + 10);
        } else {
            return FALSE;
        }
        if (low >= L'0' && low <= L'9') {
            low_value = (unsigned int)(low - L'0');
        } else if (low >= L'a' && low <= L'f') {
            low_value = (unsigned int)(low - L'a' + 10);
        } else if (low >= L'A' && low <= L'F') {
            low_value = (unsigned int)(low - L'A' + 10);
        } else {
            return FALSE;
        }
        output[index] = (unsigned char)((high_value << 4) | low_value);
    }
    return TRUE;
}

static void initialize_header(
    struct packet_header *header,
    uint8_t kind,
    uint16_t size
) {
    header->magic = UUIP_MAGIC;
    header->version = UUIP_VERSION;
    header->kind = kind;
    header->size = size;
    header->sequence = (uint32_t)InterlockedIncrement(&sequence_number);
    CopyMemory(header->token, bridge_token, sizeof(header->token));
}

static void send_hello(void) {
    struct hello_packet packet;
    if (!endpoint_valid || bridge_socket == INVALID_SOCKET) {
        return;
    }
    ZeroMemory(&packet, sizeof(packet));
    initialize_header(&packet.header, UUIP_HELLO, sizeof(packet));
    packet.pid = GetCurrentProcessId();
    packet.hook_version = HOOK_VERSION;
    sendto(
        bridge_socket,
        (const char *)&packet,
        sizeof(packet),
        0,
        (const struct sockaddr *)&bridge_address,
        sizeof(bridge_address)
    );
}

static void clear_tracked_cursor(void) {
    AcquireSRWLockExclusive(&cursor_lock);
    tracked_cursor_valid = FALSE;
    ReleaseSRWLockExclusive(&cursor_lock);
}

static BOOL refresh_endpoint(void) {
    ULONGLONG now = GetTickCount64();
    WCHAR token_text[64];
    unsigned char new_token[16];
    UINT port;
    BOOL changed;
    BOOL ready;

    AcquireSRWLockExclusive(&endpoint_lock);
    if (now - endpoint_checked_at < 1000) {
        ready = endpoint_valid;
        ReleaseSRWLockExclusive(&endpoint_lock);
        return ready;
    }
    endpoint_checked_at = now;

    port = GetPrivateProfileIntW(
        L"bridge", L"port", 0, endpoint_path
    );
    token_text[0] = L'\0';
    GetPrivateProfileStringW(
        L"bridge",
        L"token",
        L"",
        token_text,
        (DWORD)(sizeof(token_text) / sizeof(token_text[0])),
        endpoint_path
    );
    force_cursor_visible = GetPrivateProfileIntW(
        L"bridge", L"force_cursor", 1, endpoint_path
    ) != 0;
    if (
        port == 0 ||
        port > 65535 ||
        !parse_hex_token(token_text, new_token) ||
        !InitOnceExecuteOnce(&winsock_once, initialize_winsock, NULL, NULL)
    ) {
        endpoint_valid = FALSE;
        ReleaseSRWLockExclusive(&endpoint_lock);
        return FALSE;
    }

    changed = !endpoint_valid ||
        bridge_address.sin_port != htons((u_short)port) ||
        memcmp(bridge_token, new_token, sizeof(bridge_token)) != 0;
    ZeroMemory(&bridge_address, sizeof(bridge_address));
    bridge_address.sin_family = AF_INET;
    bridge_address.sin_port = htons((u_short)port);
    bridge_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CopyMemory(bridge_token, new_token, sizeof(bridge_token));
    endpoint_valid = TRUE;
    if (changed) {
        clear_tracked_cursor();
        send_hello();
    }
    ReleaseSRWLockExclusive(&endpoint_lock);
    return TRUE;
}

static LONG clamp_long(LONG value, LONG minimum, LONG maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static LONG clamp_wide(LONGLONG value, LONG minimum, LONG maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return (LONG)value;
}

static LONG normalized_to_pixel(LONG value, LONG origin, LONG extent) {
    LONGLONG normalized = clamp_long(value, 0, 65535);
    if (extent <= 1) {
        return origin;
    }
    return origin + (LONG)(
        (normalized * (LONGLONG)(extent - 1) + 32767) / 65535
    );
}

static void track_mouse_position(const MOUSEINPUT *input) {
    LONG origin_x;
    LONG origin_y;
    LONG width;
    LONG height;

    if (!(input->dwFlags & MOUSEEVENTF_MOVE)) {
        return;
    }
    if (input->dwFlags & MOUSEEVENTF_VIRTUALDESK) {
        origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {
        origin_x = 0;
        origin_y = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;

    AcquireSRWLockExclusive(&cursor_lock);
    if (input->dwFlags & MOUSEEVENTF_ABSOLUTE) {
        tracked_cursor_position.x = normalized_to_pixel(
            input->dx, origin_x, width
        );
        tracked_cursor_position.y = normalized_to_pixel(
            input->dy, origin_y, height
        );
        tracked_cursor_valid = TRUE;
    } else {
        if (
            !tracked_cursor_valid &&
            !GetCursorPos(&tracked_cursor_position)
        ) {
            ReleaseSRWLockExclusive(&cursor_lock);
            return;
        }
        tracked_cursor_position.x = clamp_wide(
            (LONGLONG)tracked_cursor_position.x + input->dx,
            origin_x,
            origin_x + width - 1
        );
        tracked_cursor_position.y = clamp_wide(
            (LONGLONG)tracked_cursor_position.y + input->dy,
            origin_y,
            origin_y + height - 1
        );
        tracked_cursor_valid = TRUE;
    }
    ReleaseSRWLockExclusive(&cursor_lock);
}

static BOOL read_tracked_cursor(LPPOINT point) {
    BOOL ready;
    AcquireSRWLockShared(&cursor_lock);
    ready = tracked_cursor_valid;
    if (ready) {
        *point = tracked_cursor_position;
    }
    ReleaseSRWLockShared(&cursor_lock);
    return ready;
}

static BOOL forward_mouse(const MOUSEINPUT *input) {
    struct mouse_packet packet;
    int sent;
    ZeroMemory(&packet, sizeof(packet));
    initialize_header(&packet.header, UUIP_MOUSE, sizeof(packet));
    packet.dx = input->dx;
    packet.dy = input->dy;
    packet.mouse_data = input->mouseData;
    packet.flags = input->dwFlags;
    packet.time = input->time;
    packet.extra_info = (uint64_t)input->dwExtraInfo;
    sent = sendto(
        bridge_socket,
        (const char *)&packet,
        sizeof(packet),
        0,
        (const struct sockaddr *)&bridge_address,
        sizeof(bridge_address)
    );
    return sent == (int)sizeof(packet);
}

static BOOL forward_keyboard(const KEYBDINPUT *input) {
    struct keyboard_packet packet;
    int sent;
    ZeroMemory(&packet, sizeof(packet));
    initialize_header(&packet.header, UUIP_KEYBOARD, sizeof(packet));
    packet.virtual_key = input->wVk;
    packet.scan_code = input->wScan;
    packet.flags = input->dwFlags;
    packet.time = input->time;
    packet.extra_info = (uint64_t)input->dwExtraInfo;
    sent = sendto(
        bridge_socket,
        (const char *)&packet,
        sizeof(packet),
        0,
        (const struct sockaddr *)&bridge_address,
        sizeof(bridge_address)
    );
    return sent == (int)sizeof(packet);
}

static UINT WINAPI hooked_send_input(
    UINT count,
    LPINPUT inputs,
    int input_size
) {
    UINT index;

    if (
        count == 0 ||
        !inputs ||
        input_size != (int)sizeof(INPUT) ||
        !refresh_endpoint()
    ) {
        return original_send_input
            ? original_send_input(count, inputs, input_size)
            : 0;
    }
    for (index = 0; index < count; ++index) {
        if (inputs[index].type == INPUT_MOUSE) {
            if (!forward_mouse(&inputs[index].mi)) {
                return original_send_input
                    ? original_send_input(count, inputs, input_size)
                    : index;
            }
            track_mouse_position(&inputs[index].mi);
        } else if (inputs[index].type == INPUT_KEYBOARD) {
            if (!forward_keyboard(&inputs[index].ki)) {
                return original_send_input
                    ? original_send_input(count, inputs, input_size)
                    : index;
            }
        } else {
            return original_send_input
                ? original_send_input(count, inputs, input_size)
                : index;
        }
    }
    return count;
}

static BOOL WINAPI hooked_get_cursor_info(PCURSORINFO cursor_info) {
    HCURSOR stable_cursor;
    BOOL result = original_get_cursor_info
        ? original_get_cursor_info(cursor_info)
        : FALSE;
    if (
        cursor_info &&
        cursor_info->cbSize >= sizeof(*cursor_info) &&
        refresh_endpoint() &&
        force_cursor_visible
    ) {
        cursor_info->flags = CURSOR_SHOWING;
        stable_cursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        if (stable_cursor) {
            cursor_info->hCursor = stable_cursor;
        }
        if (read_tracked_cursor(&cursor_info->ptScreenPos)) {
            result = TRUE;
        } else if (!result) {
            GetCursorPos(&cursor_info->ptScreenPos);
            result = TRUE;
        }
    }
    return result;
}

static BOOL WINAPI hooked_get_cursor_pos(LPPOINT point) {
    BOOL result = original_get_cursor_pos
        ? original_get_cursor_pos(point)
        : FALSE;
    if (
        point &&
        refresh_endpoint() &&
        read_tracked_cursor(point)
    ) {
        return TRUE;
    }
    return result;
}

static void close_frame_mapping(void) {
    if (frame_view) {
        UnmapViewOfFile(frame_view);
        frame_view = NULL;
    }
    if (frame_mapping) {
        CloseHandle(frame_mapping);
        frame_mapping = NULL;
    }
    if (frame_file != INVALID_HANDLE_VALUE) {
        CloseHandle(frame_file);
        frame_file = INVALID_HANDLE_VALUE;
    }
    frame_view_size = 0;
    frame_last_sequence = 0;
    frame_sequence_changed_at = 0;
}

static BOOL frame_header_valid(
    const struct wayland_frame_header *header,
    SIZE_T mapped_size
) {
    ULONGLONG required;
    ULONGLONG calculated;
    if (
        !header ||
        header->magic != UUWF_MAGIC ||
        header->version != UUWF_VERSION ||
        header->header_size != UUWF_HEADER_SIZE ||
        header->width == 0 ||
        header->height == 0 ||
        header->width > 16384 ||
        header->height > 16384 ||
        header->stride < header->width * 4u ||
        header->buffer_count != UUWF_BUFFER_COUNT
    ) {
        return FALSE;
    }
    calculated = (ULONGLONG)header->stride * header->height;
    if (calculated != header->frame_size) {
        return FALSE;
    }
    required = (ULONGLONG)header->header_size +
        (ULONGLONG)header->buffer_count * header->frame_size;
    return required <= mapped_size;
}

static BOOL open_frame_mapping(void) {
    LARGE_INTEGER size;
    HANDLE file;
    HANDLE mapping;
    unsigned char *view;

    file = CreateFileW(
        frame_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (
        !GetFileSizeEx(file, &size) ||
        size.QuadPart < (LONGLONG)UUWF_HEADER_SIZE ||
        size.QuadPart > (LONGLONG)(2u * 16384u * 16384u * 4u + 4096u)
    ) {
        CloseHandle(file);
        return FALSE;
    }
    mapping = CreateFileMappingW(
        file, NULL, PAGE_READONLY, 0, 0, NULL
    );
    if (!mapping) {
        CloseHandle(file);
        return FALSE;
    }
    view = (unsigned char *)MapViewOfFile(
        mapping, FILE_MAP_READ, 0, 0, 0
    );
    if (
        !view ||
        !frame_header_valid(
            (const struct wayland_frame_header *)view,
            (SIZE_T)size.QuadPart
        )
    ) {
        if (view) {
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
        CloseHandle(file);
        return FALSE;
    }
    frame_file = file;
    frame_mapping = mapping;
    frame_view = view;
    frame_view_size = (SIZE_T)size.QuadPart;
    frame_last_sequence = 0;
    frame_sequence_changed_at = GetTickCount64();
    return TRUE;
}

static void write_frame_hook_status(void) {
    CHAR value[32];
    DWORD status = 0;

    if (!frame_status_path[0]) {
        return;
    }
    if (bit_blt_hooked) {
        status |= 1u;
    }
    if (stretch_blt_hooked) {
        status |= 2u;
    }
    if (streamer_bit_blt_hooked) {
        status |= 4u;
    }
    if (streamer_stretch_blt_hooked) {
        status |= 8u;
    }
    if (frame_view) {
        status |= 16u;
    }
    wsprintfA(value, "%lu", (unsigned long)GetCurrentProcessId());
    WritePrivateProfileStringA("hook", "pid", value, frame_status_path);
    wsprintfA(value, "%lu", (unsigned long)HOOK_VERSION);
    WritePrivateProfileStringA("hook", "version", value, frame_status_path);
    wsprintfA(value, "%lu", (unsigned long)status);
    WritePrivateProfileStringA("hook", "status_bits", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_hook_calls, 0, 0)
    );
    WritePrivateProfileStringA("capture", "calls", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_hook_rendered, 0, 0)
    );
    WritePrivateProfileStringA("capture", "rendered", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_hook_fallbacks, 0, 0)
    );
    WritePrivateProfileStringA("capture", "fallbacks", value, frame_status_path);
    wsprintfA(value, "%lu", (unsigned long)frame_last_sequence);
    WritePrivateProfileStringA("capture", "sequence", value, frame_status_path);
}

static BOOL refresh_frame_mapping(void) {
    const struct wayland_frame_header *header;
    uint32_t sequence;
    ULONGLONG now = GetTickCount64();

    if (!frame_view && !open_frame_mapping()) {
        return FALSE;
    }
    header = (const struct wayland_frame_header *)frame_view;
    if (!frame_header_valid(header, frame_view_size)) {
        close_frame_mapping();
        return FALSE;
    }
    MemoryBarrier();
    sequence = header->sequence;
    if (sequence && sequence != frame_last_sequence) {
        frame_last_sequence = sequence;
        frame_sequence_changed_at = now;
        return TRUE;
    }
    if (
        sequence &&
        now - frame_sequence_changed_at <= UUWF_STALE_TIMEOUT_MS
    ) {
        return TRUE;
    }
    close_frame_mapping();
    return FALSE;
}

static BOOL render_wayland_frame(
    HDC destination,
    int destination_x,
    int destination_y,
    int destination_width,
    int destination_height,
    HDC source,
    int source_x,
    int source_y,
    int source_width,
    int source_height,
    DWORD raster_operation
) {
    const struct wayland_frame_header *header;
    const unsigned char *pixels;
    BITMAPINFO bitmap;
    uint32_t active;
    int result;
    LONG calls;

    calls = InterlockedIncrement(&frame_hook_calls);
    if (
        !destination ||
        !source ||
        destination_width <= 0 ||
        destination_height <= 0 ||
        source_width <= 0 ||
        source_height <= 0 ||
        (raster_operation & 0x00ffffffu) != SRCCOPY ||
        GetObjectType(source) != OBJ_DC ||
        GetDeviceCaps(source, TECHNOLOGY) != DT_RASDISPLAY
    ) {
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }

    AcquireSRWLockExclusive(&frame_lock);
    if (!refresh_frame_mapping()) {
        ReleaseSRWLockExclusive(&frame_lock);
        InterlockedIncrement(&frame_hook_fallbacks);
        if ((calls % 120) == 1) {
            write_frame_hook_status();
        }
        return FALSE;
    }
    header = (const struct wayland_frame_header *)frame_view;
    MemoryBarrier();
    active = header->active_buffer;
    if (
        active >= header->buffer_count ||
        source_x < 0 ||
        source_y < 0 ||
        source_x >= (int)header->width ||
        source_y >= (int)header->height
    ) {
        ReleaseSRWLockExclusive(&frame_lock);
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }
    if (source_x + source_width > (int)header->width) {
        source_width = (int)header->width - source_x;
    }
    if (source_y + source_height > (int)header->height) {
        source_height = (int)header->height - source_y;
    }
    pixels = frame_view + header->header_size +
        (SIZE_T)active * header->frame_size;
    ZeroMemory(&bitmap, sizeof(bitmap));
    bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
    bitmap.bmiHeader.biWidth = (LONG)header->width;
    bitmap.bmiHeader.biHeight = -(LONG)header->height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    result = StretchDIBits(
        destination,
        destination_x,
        destination_y,
        destination_width,
        destination_height,
        source_x,
        source_y,
        source_width,
        source_height,
        pixels,
        &bitmap,
        DIB_RGB_COLORS,
        SRCCOPY
    );
    ReleaseSRWLockExclusive(&frame_lock);
    if (result == (int)GDI_ERROR || result == 0) {
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }
    InterlockedIncrement(&frame_hook_rendered);
    if ((calls % 120) == 1) {
        write_frame_hook_status();
    }
    return TRUE;
}

static BOOL WINAPI hooked_bit_blt(
    HDC destination,
    int destination_x,
    int destination_y,
    int width,
    int height,
    HDC source,
    int source_x,
    int source_y,
    DWORD raster_operation
) {
    if (render_wayland_frame(
            destination,
            destination_x,
            destination_y,
            width,
            height,
            source,
            source_x,
            source_y,
            width,
            height,
            raster_operation
        )) {
        return TRUE;
    }
    return original_bit_blt
        ? original_bit_blt(
            destination,
            destination_x,
            destination_y,
            width,
            height,
            source,
            source_x,
            source_y,
            raster_operation
        )
        : FALSE;
}

static BOOL WINAPI hooked_stretch_blt(
    HDC destination,
    int destination_x,
    int destination_y,
    int destination_width,
    int destination_height,
    HDC source,
    int source_x,
    int source_y,
    int source_width,
    int source_height,
    DWORD raster_operation
) {
    if (render_wayland_frame(
            destination,
            destination_x,
            destination_y,
            destination_width,
            destination_height,
            source,
            source_x,
            source_y,
            source_width,
            source_height,
            raster_operation
        )) {
        return TRUE;
    }
    return original_stretch_blt
        ? original_stretch_blt(
            destination,
            destination_x,
            destination_y,
            destination_width,
            destination_height,
            source,
            source_x,
            source_y,
            source_width,
            source_height,
            raster_operation
        )
        : FALSE;
}

static int hex_value(CHAR character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static BOOL parse_mac_address(
    const CHAR *text,
    unsigned char output[6]
) {
    unsigned int index;

    if (!text || lstrlenA(text) != 17) {
        return FALSE;
    }
    for (index = 0; index < 6; ++index) {
        int high = hex_value(text[index * 3]);
        int low = hex_value(text[index * 3 + 1]);
        if (high < 0 || low < 0) {
            return FALSE;
        }
        if (index < 5 && text[index * 3 + 2] != ':') {
            return FALSE;
        }
        output[index] = (unsigned char)((high << 4) | low);
    }
    return TRUE;
}

static BOOL CALLBACK initialize_wol_config(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID *context
) {
    CHAR enabled[8];
    CHAR mac[32];
    ULONG address;
    (void)once;
    (void)parameter;
    (void)context;

    ZeroMemory(&configured_wol, sizeof(configured_wol));
    if (!wol_config_path[0]) {
        return TRUE;
    }
    GetPrivateProfileStringA(
        "wol", "enabled", "0", enabled, sizeof(enabled), wol_config_path
    );
    GetPrivateProfileStringA(
        "wol",
        "reference_ip",
        "",
        configured_wol.reference_ip,
        sizeof(configured_wol.reference_ip),
        wol_config_path
    );
    GetPrivateProfileStringA(
        "wol",
        "native_ip",
        "",
        configured_wol.native_ip,
        sizeof(configured_wol.native_ip),
        wol_config_path
    );
    GetPrivateProfileStringA(
        "wol",
        "gateway",
        "",
        configured_wol.gateway,
        sizeof(configured_wol.gateway),
        wol_config_path
    );
    GetPrivateProfileStringA(
        "wol", "mac", "", mac, sizeof(mac), wol_config_path
    );
    address = inet_addr(configured_wol.reference_ip);
    configured_wol.enabled = (
        lstrcmpA(enabled, "1") == 0 &&
        address != INADDR_NONE &&
        address != INADDR_ANY &&
        parse_mac_address(mac, configured_wol.mac)
    );
    configured_wol.reference_address = address;
    return TRUE;
}

static BOOL wol_config_ready(void) {
    return (
        InitOnceExecuteOnce(
            &wol_config_once, initialize_wol_config, NULL, NULL
        ) &&
        configured_wol.enabled
    );
}

static BOOL mac_matches(
    const unsigned char *actual,
    ULONG length
) {
    return (
        length == sizeof(configured_wol.mac) &&
        memcmp(actual, configured_wol.mac, sizeof(configured_wol.mac)) == 0
    );
}

static PIP_ADAPTER_UNICAST_ADDRESS adapter_ipv4_address(
    PIP_ADAPTER_ADDRESSES adapter
) {
    PIP_ADAPTER_UNICAST_ADDRESS address;

    for (address = adapter->FirstUnicastAddress;
        address;
        address = address->Next) {
        if (
            address->Address.lpSockaddr &&
            address->Address.iSockaddrLength >= (INT)sizeof(SOCKADDR_IN) &&
            address->Address.lpSockaddr->sa_family == AF_INET
        ) {
            return address;
        }
    }
    return NULL;
}

static PIP_ADAPTER_ADDRESSES select_address_adapter(
    PIP_ADAPTER_ADDRESSES adapters
) {
    PIP_ADAPTER_ADDRESSES adapter;
    PIP_ADAPTER_ADDRESSES fallback = NULL;

    for (adapter = adapters; adapter; adapter = adapter->Next) {
        if (mac_matches(adapter->PhysicalAddress, adapter->PhysicalAddressLength)) {
            return adapter;
        }
        if (
            !fallback &&
            adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
            adapter_ipv4_address(adapter)
        ) {
            fallback = adapter;
        }
    }
    return fallback;
}

static ULONG WINAPI hooked_get_adapters_addresses(
    ULONG family,
    ULONG flags,
    PVOID reserved,
    PIP_ADAPTER_ADDRESSES adapters,
    PULONG size_pointer
) {
    ULONG result;
    PIP_ADAPTER_ADDRESSES adapter;
    PIP_ADAPTER_UNICAST_ADDRESS address;
    SOCKADDR_IN *ipv4;
    ULONG gateway;

    InterlockedIncrement(&adapters_addresses_calls);
    if (!original_get_adapters_addresses) {
        write_wol_hook_status();
        return ERROR_NOT_SUPPORTED;
    }
    result = original_get_adapters_addresses(
        family, flags, reserved, adapters, size_pointer
    );
    if (result != NO_ERROR || !adapters || !wol_config_ready()) {
        write_wol_hook_status();
        return result;
    }
    adapter = select_address_adapter(adapters);
    if (!adapter) {
        write_wol_hook_status();
        return result;
    }
    address = adapter_ipv4_address(adapter);
    if (!address) {
        write_wol_hook_status();
        return result;
    }
    ipv4 = (SOCKADDR_IN *)address->Address.lpSockaddr;
    ipv4->sin_family = AF_INET;
    ipv4->sin_addr.s_addr = configured_wol.reference_address;
    address->OnLinkPrefixLength = 15;
    adapter->IfType = IF_TYPE_ETHERNET_CSMACD;
    adapter->OperStatus = IfOperStatusUp;
    adapter->AdapterName = synthetic_adapter_name;
    adapter->Description = synthetic_adapter_description;
    adapter->FriendlyName = synthetic_adapter_description;
    adapter->Mtu = 1500;
    adapter->Ipv4Metric = 1;
    adapter->Flags |= IP_ADAPTER_IPV4_ENABLED;
    adapter->PhysicalAddressLength = sizeof(configured_wol.mac);
    CopyMemory(
        adapter->PhysicalAddress,
        configured_wol.mac,
        sizeof(configured_wol.mac)
    );
    gateway = inet_addr(configured_wol.gateway);
    if (gateway != INADDR_NONE && gateway != INADDR_ANY) {
        ZeroMemory(
            &synthetic_gateway_address,
            sizeof(synthetic_gateway_address)
        );
        ZeroMemory(
            &synthetic_gateway_sockaddr,
            sizeof(synthetic_gateway_sockaddr)
        );
        synthetic_gateway_sockaddr.sin_family = AF_INET;
        synthetic_gateway_sockaddr.sin_addr.s_addr = gateway;
        synthetic_gateway_address.Length =
            sizeof(synthetic_gateway_address);
        synthetic_gateway_address.Address.lpSockaddr =
            (LPSOCKADDR)&synthetic_gateway_sockaddr;
        synthetic_gateway_address.Address.iSockaddrLength =
            sizeof(synthetic_gateway_sockaddr);
        adapter->FirstGatewayAddress = &synthetic_gateway_address;
    }
    InterlockedIncrement(&adapters_addresses_patched);
    write_wol_hook_status();
    return result;
}

static PIP_ADAPTER_INFO select_info_adapter(PIP_ADAPTER_INFO adapters) {
    PIP_ADAPTER_INFO adapter;
    PIP_ADAPTER_INFO fallback = NULL;

    for (adapter = adapters; adapter; adapter = adapter->Next) {
        if (mac_matches(adapter->Address, adapter->AddressLength)) {
            return adapter;
        }
        if (!fallback && adapter->Type != IF_TYPE_SOFTWARE_LOOPBACK) {
            fallback = adapter;
        }
    }
    return fallback;
}

static ULONG WINAPI hooked_get_adapters_info(
    PIP_ADAPTER_INFO adapters,
    PULONG size_pointer
) {
    ULONG result;
    PIP_ADAPTER_INFO adapter;

    InterlockedIncrement(&adapters_info_calls);
    if (!original_get_adapters_info) {
        write_wol_hook_status();
        return ERROR_NOT_SUPPORTED;
    }
    result = original_get_adapters_info(adapters, size_pointer);
    if (result != NO_ERROR || !adapters || !wol_config_ready()) {
        write_wol_hook_status();
        return result;
    }
    adapter = select_info_adapter(adapters);
    if (!adapter) {
        write_wol_hook_status();
        return result;
    }
    adapter->Type = IF_TYPE_ETHERNET_CSMACD;
    lstrcpynA(
        adapter->AdapterName,
        synthetic_adapter_name,
        sizeof(adapter->AdapterName)
    );
    lstrcpynA(
        adapter->Description,
        "UU Remote Linux Ethernet",
        sizeof(adapter->Description)
    );
    adapter->AddressLength = sizeof(configured_wol.mac);
    CopyMemory(adapter->Address, configured_wol.mac, sizeof(configured_wol.mac));
    lstrcpynA(
        adapter->IpAddressList.IpAddress.String,
        configured_wol.reference_ip,
        sizeof(adapter->IpAddressList.IpAddress.String)
    );
    lstrcpynA(
        adapter->IpAddressList.IpMask.String,
        "255.254.0.0",
        sizeof(adapter->IpAddressList.IpMask.String)
    );
    lstrcpynA(
        adapter->GatewayList.IpAddress.String,
        configured_wol.gateway,
        sizeof(adapter->GatewayList.IpAddress.String)
    );
    adapter->CurrentIpAddress = &adapter->IpAddressList;
    adapter->DhcpEnabled = FALSE;
    InterlockedIncrement(&adapters_info_patched);
    write_wol_hook_status();
    return result;
}

static PMIB_IF_ROW2 select_if_row(PMIB_IF_TABLE2 table) {
    ULONG index;
    PMIB_IF_ROW2 fallback = NULL;

    for (index = 0; index < table->NumEntries; ++index) {
        PMIB_IF_ROW2 row = &table->Table[index];
        if (mac_matches(row->PhysicalAddress, row->PhysicalAddressLength)) {
            return row;
        }
        if (!fallback && row->Type != IF_TYPE_SOFTWARE_LOOPBACK) {
            fallback = row;
        }
    }
    return fallback;
}

static NETIO_STATUS WINAPI hooked_get_if_table2(PMIB_IF_TABLE2 *table) {
    NETIO_STATUS result;
    PMIB_IF_ROW2 row;

    InterlockedIncrement(&if_table2_calls);
    if (!original_get_if_table2) {
        write_wol_hook_status();
        return ERROR_NOT_SUPPORTED;
    }
    result = original_get_if_table2(table);
    if (result != NO_ERROR || !table || !*table || !wol_config_ready()) {
        write_wol_hook_status();
        return result;
    }
    row = select_if_row(*table);
    if (!row) {
        write_wol_hook_status();
        return result;
    }
    row->Type = IF_TYPE_ETHERNET_CSMACD;
    row->MediaType = NdisMedium802_3;
    row->PhysicalMediumType = NdisPhysicalMedium802_3;
    row->AccessType = NET_IF_ACCESS_BROADCAST;
    row->DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    row->OperStatus = IfOperStatusUp;
    row->AdminStatus = NET_IF_ADMIN_STATUS_UP;
    row->MediaConnectState = MediaConnectStateConnected;
    row->PhysicalAddressLength = sizeof(configured_wol.mac);
    CopyMemory(
        row->PhysicalAddress,
        configured_wol.mac,
        sizeof(configured_wol.mac)
    );
    CopyMemory(
        row->PermanentPhysicalAddress,
        configured_wol.mac,
        sizeof(configured_wol.mac)
    );
    row->InterfaceAndOperStatusFlags.HardwareInterface = TRUE;
    row->InterfaceAndOperStatusFlags.ConnectorPresent = TRUE;
    row->InterfaceAndOperStatusFlags.NotMediaConnected = FALSE;
    InterlockedIncrement(&if_table2_patched);
    write_wol_hook_status();
    return result;
}

static BOOL patch_import(
    HMODULE module,
    const char *dll_name,
    const char *function_name,
    void *replacement,
    void **original
) {
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_DATA_DIRECTORY directory;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;

    if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    directory = nt->OptionalHeader.DataDirectory[
        IMAGE_DIRECTORY_ENTRY_IMPORT
    ];
    if (!directory.VirtualAddress) {
        return FALSE;
    }
    descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(
        base + directory.VirtualAddress
    );
    for (; descriptor->Name; ++descriptor) {
        IMAGE_THUNK_DATA64 *names;
        IMAGE_THUNK_DATA64 *addresses;
        size_t index;
        const char *imported_dll = (const char *)(base + descriptor->Name);
        if (lstrcmpiA(imported_dll, dll_name) != 0) {
            continue;
        }
        if (!descriptor->OriginalFirstThunk) {
            return FALSE;
        }
        names = (IMAGE_THUNK_DATA64 *)(
            base + descriptor->OriginalFirstThunk
        );
        addresses = (IMAGE_THUNK_DATA64 *)(
            base + descriptor->FirstThunk
        );
        for (index = 0; names[index].u1.AddressOfData; ++index) {
            IMAGE_IMPORT_BY_NAME *imported_name;
            DWORD old_protection;
            void *previous;
            if (IMAGE_SNAP_BY_ORDINAL64(names[index].u1.Ordinal)) {
                continue;
            }
            imported_name = (IMAGE_IMPORT_BY_NAME *)(
                base + names[index].u1.AddressOfData
            );
            if (lstrcmpA((const char *)imported_name->Name, function_name)) {
                continue;
            }
            if (
                !VirtualProtect(
                    &addresses[index].u1.Function,
                    sizeof(addresses[index].u1.Function),
                    PAGE_READWRITE,
                    &old_protection
                )
            ) {
                return FALSE;
            }
            previous = InterlockedExchangePointer(
                (void *volatile *)&addresses[index].u1.Function,
                replacement
            );
            VirtualProtect(
                &addresses[index].u1.Function,
                sizeof(addresses[index].u1.Function),
                old_protection,
                &old_protection
            );
            FlushInstructionCache(
                GetCurrentProcess(),
                &addresses[index].u1.Function,
                sizeof(addresses[index].u1.Function)
            );
            if (previous != replacement) {
                *original = previous;
            }
            return TRUE;
        }
    }
    return FALSE;
}

static void event_loop_reset_false_wakes(
    struct event_loop_thread_state *state
) {
    if (!state) {
        return;
    }
    state->last_peek_empty = FALSE;
    state->consecutive_false_wakes = 0;
    state->false_wake_burst_started = 0;
}

static struct event_loop_thread_state *event_loop_thread_state(void) {
    struct event_loop_thread_state *state;

    if (event_loop_tls_index == TLS_OUT_OF_INDEXES) {
        return NULL;
    }
    state = (struct event_loop_thread_state *)TlsGetValue(
        event_loop_tls_index
    );
    if (state) {
        return state;
    }
    state = (struct event_loop_thread_state *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state)
    );
    if (!state || !TlsSetValue(event_loop_tls_index, state)) {
        if (state) {
            HeapFree(GetProcessHeap(), 0, state);
        }
        return NULL;
    }
    return state;
}

static BOOL event_loop_false_wake_should_break(
    struct event_loop_thread_state *state,
    DWORD wait_result,
    DWORD handle_count,
    ULONGLONG now
) {
    if (
        !state ||
        !state->last_peek_empty ||
        wait_result != WAIT_OBJECT_0 + handle_count
    ) {
        event_loop_reset_false_wakes(state);
        return FALSE;
    }
    if (
        !state->false_wake_burst_started ||
        now < state->false_wake_burst_started ||
        now - state->false_wake_burst_started >
            EVENT_LOOP_FALSE_WAKE_WINDOW_MS
    ) {
        state->false_wake_burst_started = now;
        state->consecutive_false_wakes = 0;
    }
    ++state->consecutive_false_wakes;
    if (
        state->consecutive_false_wakes <
        EVENT_LOOP_FALSE_WAKE_THRESHOLD
    ) {
        return FALSE;
    }
    event_loop_reset_false_wakes(state);
    return TRUE;
}

static BOOL qt_posted_event_window(HWND window) {
    WCHAR class_name[96];

    if (!window || !IsWindow(window)) {
        return FALSE;
    }
    class_name[0] = L'\0';
    if (!GetClassNameW(window, class_name, 96)) {
        return FALSE;
    }
    return wide_contains_ordinal_ci(
        class_name, L"QEventDispatcherWin32_Internal_Widget"
    );
}

static BOOL qt_posted_wakeup_mark(HWND window) {
    unsigned int index;
    unsigned int empty = QT_POSTED_WAKEUP_MAX_WINDOWS;
    BOOL should_forward = TRUE;

    AcquireSRWLockExclusive(&qt_posted_wakeup_lock);
    for (index = 0; index < QT_POSTED_WAKEUP_MAX_WINDOWS; ++index) {
        if (qt_posted_wakeups[index].window == window) {
            if (qt_posted_wakeups[index].pending) {
                should_forward = FALSE;
            } else {
                qt_posted_wakeups[index].pending = TRUE;
            }
            break;
        }
        if (
            !qt_posted_wakeups[index].window &&
            empty == QT_POSTED_WAKEUP_MAX_WINDOWS
        ) {
            empty = index;
        }
    }
    if (index == QT_POSTED_WAKEUP_MAX_WINDOWS && empty < index) {
        qt_posted_wakeups[empty].window = window;
        qt_posted_wakeups[empty].pending = TRUE;
    }
    ReleaseSRWLockExclusive(&qt_posted_wakeup_lock);
    return should_forward;
}

static void qt_posted_wakeup_clear(HWND window) {
    unsigned int index;

    if (!window) {
        return;
    }
    AcquireSRWLockExclusive(&qt_posted_wakeup_lock);
    for (index = 0; index < QT_POSTED_WAKEUP_MAX_WINDOWS; ++index) {
        if (qt_posted_wakeups[index].window == window) {
            qt_posted_wakeups[index].pending = FALSE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&qt_posted_wakeup_lock);
}

static BOOL WINAPI hooked_peek_message_w(
    LPMSG message,
    HWND window,
    UINT minimum,
    UINT maximum,
    UINT remove_message
) {
    struct event_loop_thread_state *state;
    BOOL result;

    if (!original_peek_message_w) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    result = original_peek_message_w(
        message, window, minimum, maximum, remove_message
    );
    if (
        window ||
        minimum ||
        maximum ||
        !(remove_message & PM_REMOVE)
    ) {
        return result;
    }
    state = event_loop_thread_state();
    if (!result) {
        if (state) {
            state->last_peek_empty = TRUE;
        }
        return FALSE;
    }
    event_loop_reset_false_wakes(state);
    InterlockedIncrement(&event_loop_messages_dequeued);
    if (
        message &&
        message->message == WM_QT_SENDPOSTEDEVENTS &&
        qt_posted_event_window(message->hwnd)
    ) {
        qt_posted_wakeup_clear(message->hwnd);
    }
    return TRUE;
}

static DWORD WINAPI hooked_msg_wait_for_multiple_objects_ex(
    DWORD handle_count,
    const HANDLE *handles,
    DWORD milliseconds,
    DWORD wake_mask,
    DWORD flags
) {
    struct event_loop_thread_state *state;
    DWORD result;

    if (!original_msg_wait_for_multiple_objects_ex) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return WAIT_FAILED;
    }
    result = original_msg_wait_for_multiple_objects_ex(
        handle_count, handles, milliseconds, wake_mask, flags
    );
    state = event_loop_thread_state();
    if (
        milliseconds != 0 ||
        !(wake_mask & QS_ALLINPUT) ||
        !(flags & MWMO_ALERTABLE)
    ) {
        event_loop_reset_false_wakes(state);
        return result;
    }
    if (
        state &&
        state->last_peek_empty &&
        result == WAIT_OBJECT_0 + handle_count
    ) {
        InterlockedIncrement(&event_loop_empty_queue_wakes);
    }
    if (!event_loop_false_wake_should_break(
            state, result, handle_count, GetTickCount64()
        )) {
        return result;
    }

    /*
     * Qt's zero-timeout probe is only asking whether useful work remains.
     * Wine can leave the queue wake bit asserted after PeekMessageW reported
     * an empty queue. Returning that stale signal forever prevents Qt from
     * reaching its normal blocking wait. After a bounded proof of the cycle,
     * report the semantically correct timeout once so Qt can leave the probe
     * loop. The 1 ms yield also prevents an outer Qt loop from immediately
     * consuming a full CPU core if another posted-event burst is in flight.
     */
    InterlockedIncrement(&event_loop_guard_breaks);
    InterlockedExchange64(
        &event_loop_last_guard_break, (LONG64)GetTickCount64()
    );
    Sleep(EVENT_LOOP_BREAK_BACKOFF_MS);
    return WAIT_TIMEOUT;
}

static BOOL WINAPI hooked_post_message_w(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    BOOL result;
    BOOL posted_event;

    if (!original_post_message_w) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    posted_event =
        message == WM_QT_SENDPOSTEDEVENTS &&
        wparam == 0 &&
        lparam == 0 &&
        qt_posted_event_window(window);
    if (posted_event && !qt_posted_wakeup_mark(window)) {
        InterlockedIncrement(&event_loop_posted_coalesced);
        return TRUE;
    }
    result = original_post_message_w(window, message, wparam, lparam);
    if (posted_event) {
        if (result) {
            InterlockedIncrement(&event_loop_posted_forwarded);
        } else {
            qt_posted_wakeup_clear(window);
        }
    }
    return result;
}

static DWORD controller_focus_hook_status(void) {
    DWORD status = 0;

    if (controller_dispatch_hooked) {
        status |= 1u;
    }
    if (qt_dispatch_hooked) {
        status |= 2u;
    }
    if (controller_set_hook_hooked) {
        status |= 4u;
    }
    if (controller_unhook_hooked) {
        status |= 8u;
    }
    if (InterlockedCompareExchange(&focus_worker_running, 0, 0)) {
        status |= 16u;
    }
    if (InterlockedCompareExchange(&focus_subclassed_windows, 0, 0) > 0) {
        status |= 32u;
    }
    if (
        (InterlockedCompareExchange(
            &focus_activation_api_mask, 0, 0
        ) & 31) == 31
    ) {
        status |= 64u;
    }
    if (qt_peek_message_hooked) {
        status |= 128u;
    }
    if (qt_msg_wait_hooked) {
        status |= 256u;
    }
    if (qt_post_message_hooked) {
        status |= 512u;
    }
    return status;
}

static BOOL focus_status_append(
    CHAR *buffer,
    size_t capacity,
    size_t *used,
    const CHAR *format,
    ...
) {
    va_list arguments;
    int written;

    if (!buffer || !used || *used >= capacity) {
        return FALSE;
    }
    va_start(arguments, format);
    written = vsnprintf(
        buffer + *used, capacity - *used, format, arguments
    );
    va_end(arguments);
    if (
        written < 0 ||
        (size_t)written >= capacity - *used
    ) {
        return FALSE;
    }
    *used += (size_t)written;
    return TRUE;
}

static BOOL write_focus_hook_status_atomic(void) {
    CHAR buffer[4096];
    CHAR temporary[MAX_PATH];
    HANDLE file;
    DWORD written = 0;
    size_t used = 0;
    BOOL ready;

    ready = focus_status_append(
        buffer,
        sizeof(buffer),
        &used,
        "[hook]\r\n"
        "pid=%lu\r\n"
        "version=%lu\r\n"
        "status_bits=%lu\r\n"
        "[focus]\r\n"
        "suppressed_activate=%ld\r\n"
        "suppressed_activate_app=%ld\r\n"
        "stabilized_nonclient=%ld\r\n"
        "internal_transitions_ignored=%ld\r\n"
        "apply_posted=%ld\r\n"
        "apply_rate_limited=%ld\r\n"
        "[keyboard_hook]\r\n"
        "installed=%ld\r\n"
        "reused=%ld\r\n"
        "deferred=%ld\r\n"
        "released=%ld\r\n"
        "[window_state]\r\n"
        "mode=wndproc-arbiter\r\n"
        "subclassed=%ld\r\n"
        "subclassed_total=%ld\r\n"
        "external_chains=%ld\r\n"
        "nonclient_right_clicks_suppressed=%ld\r\n"
        "activation_api_mask=%ld\r\n"
        "transitions=%ld\r\n"
        "storms_detected=%ld\r\n"
        "storms_resolved=%ld\r\n"
        "blocked_activations=%ld\r\n"
        "modal_latches=%ld\r\n"
        "post_modal_handoffs=%ld\r\n"
        "user_overrides=%ld\r\n"
        "preferred_role=%ld\r\n"
        "[home_window]\r\n"
        "hidden_by_user=%ld\r\n"
        "reopen_blocked=%ld\r\n"
        "show_authorized=%ld\r\n"
        "[event_loop]\r\n"
        "mode=qt-wine-empty-wake-guard\r\n"
        "empty_queue_wakes=%ld\r\n"
        "guard_breaks=%ld\r\n"
        "messages_dequeued=%ld\r\n"
        "posted_forwarded=%ld\r\n"
        "posted_coalesced=%ld\r\n"
        "last_guard_break_tick=%lld\r\n"
        "[ui_health]\r\n"
        "pings_sent=%ld\r\n"
        "pings_acked=%ld\r\n"
        "timeouts=%ld\r\n"
        "recovery_requests=%ld\r\n"
        "target_generation=%ld\r\n"
        "window_invalidations=%ld\r\n"
        "progress_cancellations=%ld\r\n"
        "no_livelock_suppressions=%ld\r\n"
        "[worker]\r\n"
        "heartbeats=%ld\r\n",
        (unsigned long)GetCurrentProcessId(),
        (unsigned long)HOOK_VERSION,
        (unsigned long)controller_focus_hook_status(),
        InterlockedCompareExchange(&focus_suppressed_activate, 0, 0),
        InterlockedCompareExchange(&focus_suppressed_activate_app, 0, 0),
        InterlockedCompareExchange(&focus_stabilized_nonclient, 0, 0),
        InterlockedCompareExchange(
            &focus_internal_transitions_ignored, 0, 0
        ),
        InterlockedCompareExchange(&focus_apply_posted, 0, 0),
        InterlockedCompareExchange(&focus_apply_rate_limited, 0, 0),
        InterlockedCompareExchange(&focus_keyboard_installed, 0, 0),
        InterlockedCompareExchange(&focus_keyboard_reused, 0, 0),
        InterlockedCompareExchange(&focus_keyboard_deferred, 0, 0),
        InterlockedCompareExchange(&focus_keyboard_released, 0, 0),
        InterlockedCompareExchange(&focus_subclassed_windows, 0, 0),
        InterlockedCompareExchange(&focus_subclass_installations, 0, 0),
        InterlockedCompareExchange(&focus_external_subclass_chains, 0, 0),
        InterlockedCompareExchange(
            &focus_nonclient_right_clicks_suppressed, 0, 0
        ),
        InterlockedCompareExchange(&focus_activation_api_mask, 0, 0),
        InterlockedCompareExchange(&focus_transition_messages, 0, 0),
        InterlockedCompareExchange(&focus_storms_detected, 0, 0),
        InterlockedCompareExchange(&focus_storms_resolved, 0, 0),
        InterlockedCompareExchange(&focus_blocked_activations, 0, 0),
        InterlockedCompareExchange(&focus_modal_latches, 0, 0),
        InterlockedCompareExchange(&focus_post_modal_handoffs, 0, 0),
        InterlockedCompareExchange(&focus_user_overrides, 0, 0),
        InterlockedCompareExchange(&focus_preferred_role, 0, 0),
        InterlockedCompareExchange(&focus_home_hidden_by_user, 0, 0),
        InterlockedCompareExchange(&focus_home_reopen_blocked, 0, 0),
        InterlockedCompareExchange(&focus_home_show_authorized, 0, 0),
        InterlockedCompareExchange(&event_loop_empty_queue_wakes, 0, 0),
        InterlockedCompareExchange(&event_loop_guard_breaks, 0, 0),
        InterlockedCompareExchange(&event_loop_messages_dequeued, 0, 0),
        InterlockedCompareExchange(&event_loop_posted_forwarded, 0, 0),
        InterlockedCompareExchange(&event_loop_posted_coalesced, 0, 0),
        (long long)InterlockedCompareExchange64(
            &event_loop_last_guard_break, 0, 0
        ),
        InterlockedCompareExchange(&ui_health_pings_sent, 0, 0),
        InterlockedCompareExchange(&ui_health_pings_acked, 0, 0),
        InterlockedCompareExchange(&ui_health_timeouts, 0, 0),
        InterlockedCompareExchange(&ui_health_recovery_requests, 0, 0),
        InterlockedCompareExchange(&ui_health_ping_generation, 0, 0),
        InterlockedCompareExchange(
            &ui_health_window_invalidations, 0, 0
        ),
        InterlockedCompareExchange(
            &ui_health_progress_cancellations, 0, 0
        ),
        InterlockedCompareExchange(
            &ui_health_no_livelock_suppressions, 0, 0
        ),
        InterlockedCompareExchange(&focus_worker_heartbeats, 0, 0)
    );
    if (!ready) {
        return FALSE;
    }
    lstrcpynA(temporary, focus_status_path, MAX_PATH - 5);
    lstrcatA(temporary, ".tmp");
    file = CreateFileA(
        temporary,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ready = WriteFile(file, buffer, (DWORD)used, &written, NULL) &&
        written == (DWORD)used;
    CloseHandle(file);
    if (
        ready &&
        MoveFileExA(
            temporary,
            focus_status_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )
    ) {
        return TRUE;
    }
    DeleteFileA(temporary);
    return FALSE;
}

static void write_focus_hook_status(void) {
    CHAR value[32];

    if (!focus_status_path[0]) {
        return;
    }
    InterlockedIncrement(&focus_worker_heartbeats);
    if (write_focus_hook_status_atomic()) {
        return;
    }
    wsprintfA(value, "%lu", (unsigned long)GetCurrentProcessId());
    WritePrivateProfileStringA("hook", "pid", value, focus_status_path);
    wsprintfA(value, "%lu", (unsigned long)HOOK_VERSION);
    WritePrivateProfileStringA("hook", "version", value, focus_status_path);
    wsprintfA(
        value,
        "%lu",
        (unsigned long)controller_focus_hook_status()
    );
    WritePrivateProfileStringA(
        "hook", "status_bits", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_suppressed_activate, 0, 0)
    );
    WritePrivateProfileStringA(
        "focus", "suppressed_activate", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_suppressed_activate_app, 0, 0)
    );
    WritePrivateProfileStringA(
        "focus", "suppressed_activate_app", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_stabilized_nonclient, 0, 0)
    );
    WritePrivateProfileStringA(
        "focus", "stabilized_nonclient", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &focus_internal_transitions_ignored, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "focus", "internal_transitions_ignored", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_apply_posted, 0, 0)
    );
    WritePrivateProfileStringA(
        "focus", "apply_posted", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_apply_rate_limited, 0, 0)
    );
    WritePrivateProfileStringA(
        "focus", "apply_rate_limited", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_keyboard_installed, 0, 0)
    );
    WritePrivateProfileStringA(
        "keyboard_hook", "installed", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_keyboard_reused, 0, 0)
    );
    WritePrivateProfileStringA(
        "keyboard_hook", "reused", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_keyboard_deferred, 0, 0)
    );
    WritePrivateProfileStringA(
        "keyboard_hook", "deferred", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_keyboard_released, 0, 0)
    );
    WritePrivateProfileStringA(
        "keyboard_hook", "released", value, focus_status_path
    );
    WritePrivateProfileStringA(
        "window_state", "mode", "wndproc-arbiter", focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_subclassed_windows, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "subclassed", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_subclass_installations, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "subclassed_total", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &focus_external_subclass_chains, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "window_state", "external_chains", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_activation_api_mask, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "activation_api_mask", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_transition_messages, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "transitions", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_storms_detected, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "storms_detected", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_storms_resolved, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "storms_resolved", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_blocked_activations, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "blocked_activations", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_modal_latches, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "modal_latches", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_post_modal_handoffs, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "post_modal_handoffs", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_user_overrides, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "user_overrides", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_preferred_role, 0, 0)
    );
    WritePrivateProfileStringA(
        "window_state", "preferred_role", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_home_hidden_by_user, 0, 0)
    );
    WritePrivateProfileStringA(
        "home_window", "hidden_by_user", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_home_reopen_blocked, 0, 0)
    );
    WritePrivateProfileStringA(
        "home_window", "reopen_blocked", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_home_show_authorized, 0, 0)
    );
    WritePrivateProfileStringA(
        "home_window", "show_authorized", value, focus_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&focus_worker_heartbeats, 0, 0)
    );
    WritePrivateProfileStringA(
        "worker", "heartbeats", value, focus_status_path
    );
}

static BOOL process_belongs_to_controller(DWORD pid) {
    HANDLE process;
    WCHAR image_path[MAX_PATH];
    DWORD image_size = MAX_PATH;
    int root_length;
    BOOL result = FALSE;

    if (!pid) {
        return FALSE;
    }
    if (pid == GetCurrentProcessId()) {
        return TRUE;
    }
    if (!controller_root_path[0]) {
        return FALSE;
    }
    process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid
    );
    if (!process) {
        return FALSE;
    }
    if (QueryFullProcessImageNameW(process, 0, image_path, &image_size)) {
        root_length = lstrlenW(controller_root_path);
        result = image_size > (DWORD)root_length &&
            CompareStringOrdinal(
                image_path,
                root_length,
                controller_root_path,
                root_length,
                TRUE
            ) == CSTR_EQUAL &&
            (image_path[root_length] == L'\\' ||
                image_path[root_length] == L'/');
    }
    CloseHandle(process);
    return result;
}

static BOOL window_belongs_to_controller(HWND window) {
    DWORD pid = 0;

    if (!window || !IsWindow(window)) {
        return FALSE;
    }
    GetWindowThreadProcessId(window, &pid);
    return process_belongs_to_controller(pid);
}

static BOOL thread_belongs_to_controller(DWORD thread_id) {
    HANDLE thread;
    DWORD pid;

    if (!thread_id) {
        return FALSE;
    }
    thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id);
    if (!thread) {
        return FALSE;
    }
    pid = GetProcessIdOfThread(thread);
    CloseHandle(thread);
    return process_belongs_to_controller(pid);
}

static BOOL focus_transition_is_internal(const MSG *message);
static void focus_arbitrate_windows(void);
static void focus_queue_arbitration(void);
static void patch_controller_focus_imports(void);

static BOOL wide_contains_ordinal_ci(
    const WCHAR *text,
    const WCHAR *needle
) {
    int text_length;
    int needle_length;
    int offset;

    if (!text || !needle) {
        return FALSE;
    }
    text_length = lstrlenW(text);
    needle_length = lstrlenW(needle);
    if (!needle_length || needle_length > text_length) {
        return FALSE;
    }
    for (offset = 0; offset <= text_length - needle_length; ++offset) {
        if (
            CompareStringOrdinal(
                text + offset,
                needle_length,
                needle,
                needle_length,
                TRUE
            ) == CSTR_EQUAL
        ) {
            return TRUE;
        }
    }
    return FALSE;
}

static enum focus_window_role focus_window_role_for(HWND window) {
    WCHAR title[256];
    DWORD_PTR copied = 0;
    HWND owner;
    LONG_PTR style;
    LONG_PTR ex_style;

    title[0] = L'\0';
    SendMessageTimeoutW(
        window,
        WM_GETTEXT,
        256,
        (LPARAM)title,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        25,
        &copied
    );
    owner = GetWindow(window, GW_OWNER);
    style = GetWindowLongPtrW(window, GWL_STYLE);
    ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);

    if (
        wide_contains_ordinal_ci(
            title, L"\u63a5\u7ba1\u8bbe\u5907"
        ) ||
        wide_contains_ordinal_ci(title, L"take over") ||
        wide_contains_ordinal_ci(title, L"takeover")
    ) {
        return FOCUS_ROLE_DIALOG;
    }
    if (
        wide_contains_ordinal_ci(title, L"\u663e\u793a\u5c4f") ||
        wide_contains_ordinal_ci(title, L"display ") ||
        wide_contains_ordinal_ci(title, L"remote desktop")
    ) {
        return FOCUS_ROLE_VIDEO;
    }
    if (
        wide_contains_ordinal_ci(
            title, L"\u7f51\u6613UU\u8fdc\u7a0b"
        ) ||
        wide_contains_ordinal_ci(title, L"UU Remote")
    ) {
        return FOCUS_ROLE_HOME;
    }
    if (
        owner &&
        window_belongs_to_controller(owner) &&
        !(ex_style & WS_EX_TOOLWINDOW) &&
        (
            (ex_style & WS_EX_DLGMODALFRAME) ||
            !IsWindowEnabled(owner) ||
            (style & DS_MODALFRAME)
        )
    ) {
        return FOCUS_ROLE_DIALOG;
    }
    return FOCUS_ROLE_UNKNOWN;
}

static BOOL focus_window_entry(
    HWND window,
    WNDPROC *original_proc,
    enum focus_window_role *role
) {
    unsigned int index;
    BOOL found = FALSE;

    AcquireSRWLockShared(&focus_window_lock);
    for (index = 0; index < FOCUS_MAX_WINDOWS; ++index) {
        if (focus_windows[index].window != window) {
            continue;
        }
        if (original_proc) {
            *original_proc = focus_windows[index].original_proc;
        }
        if (role) {
            *role = focus_windows[index].role;
        }
        found = TRUE;
        break;
    }
    ReleaseSRWLockShared(&focus_window_lock);
    return found;
}

static void ui_health_clear_pending(LONG sent) {
    if (sent < 0) {
        sent = InterlockedCompareExchange(&ui_health_pings_sent, 0, 0);
    }
    InterlockedExchange(&ui_health_pings_acked, sent);
    InterlockedExchange64(&ui_health_ping_sent_at, 0);
    InterlockedExchangePointer(&ui_health_ping_window, NULL);
    InterlockedExchange(&ui_health_ping_generation, 0);
}

static void focus_remove_window(HWND window) {
    unsigned int index;
    BOOL removed = FALSE;
    LONG sent;

    AcquireSRWLockExclusive(&focus_window_lock);
    for (index = 0; index < FOCUS_MAX_WINDOWS; ++index) {
        if (focus_windows[index].window == window) {
            ZeroMemory(
                &focus_windows[index], sizeof(focus_windows[index])
            );
            InterlockedDecrement(&focus_subclassed_windows);
            InterlockedIncrement(&focus_window_generation);
            removed = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&focus_window_lock);
    if (
        removed &&
        window == (HWND)InterlockedCompareExchangePointer(
            &ui_health_ping_window, NULL, NULL
        )
    ) {
        sent = InterlockedCompareExchange(&ui_health_pings_sent, 0, 0);
        InterlockedIncrement(&ui_health_window_invalidations);
        ui_health_clear_pending(sent);
    }
    if (
        window == (HWND)InterlockedCompareExchangePointer(
            &focus_visible_remote_window, NULL, NULL
        )
    ) {
        InterlockedExchangePointer(&focus_visible_remote_window, NULL);
    }
}

static HWND focus_ui_health_window(LONG *generation) {
    HWND fallback = NULL;
    HWND home = NULL;
    unsigned int index;

    AcquireSRWLockShared(&focus_window_lock);
    for (index = 0; index < FOCUS_MAX_WINDOWS; ++index) {
        if (!focus_windows[index].window) {
            continue;
        }
        if (!fallback) {
            fallback = focus_windows[index].window;
        }
        if (focus_windows[index].role == FOCUS_ROLE_HOME) {
            home = focus_windows[index].window;
            break;
        }
    }
    if (generation) {
        *generation = InterlockedCompareExchange(
            &focus_window_generation, 0, 0
        );
    }
    ReleaseSRWLockShared(&focus_window_lock);
    return home ? home : fallback;
}

static BOOL ui_health_target_is_current(
    HWND window,
    LONG generation
) {
    HWND current;
    LONG current_generation = 0;

    if (!window || !IsWindow(window)) {
        return FALSE;
    }
    current = focus_ui_health_window(&current_generation);
    return current == window && current_generation == generation;
}

static BOOL ui_health_has_livelock_evidence(
    LONG guard_breaks_before,
    LONG guard_breaks_now,
    LONG messages_before,
    LONG messages_now
) {
    return guard_breaks_now != guard_breaks_before &&
        messages_now == messages_before;
}

static void request_controller_restart(
    LONG generation,
    LONG guard_breaks
) {
    CHAR payload[192];
    HANDLE file;
    DWORD written = 0;
    int length;

    if (
        !controller_restart_request_path[0] ||
        InterlockedCompareExchange(
            &ui_health_recovery_requested, 1, 0
        ) != 0
    ) {
        return;
    }
    length = wsprintfA(
        payload,
        "pid=%lu\r\n"
        "reason=event-loop-livelock\r\n"
        "hook_version=%lu\r\n"
        "guard_evidence=1\r\n"
        "window_generation=%ld\r\n"
        "guard_breaks=%ld\r\n",
        (unsigned long)GetCurrentProcessId(),
        (unsigned long)HOOK_VERSION,
        generation,
        guard_breaks
    );
    file = CreateFileA(
        controller_restart_request_path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        InterlockedExchange(&ui_health_recovery_requested, 0);
        return;
    }
    if (
        length > 0 &&
        WriteFile(file, payload, (DWORD)length, &written, NULL) &&
        written == (DWORD)length
    ) {
        FlushFileBuffers(file);
        InterlockedIncrement(&ui_health_recovery_requests);
    } else {
        InterlockedExchange(&ui_health_recovery_requested, 0);
    }
    CloseHandle(file);
}

static void focus_ui_health_tick(ULONGLONG now) {
    ULONGLONG previous_tick;
    ULONGLONG sent_at;
    LONG sent;
    LONG acked;
    LONG next;
    LONG generation;
    LONG guard_breaks_before;
    LONG guard_breaks_now;
    LONG messages_before;
    LONG messages_now;
    HWND window;

    previous_tick = (ULONGLONG)InterlockedExchange64(
        &ui_health_last_worker_tick, (LONG64)now
    );
    sent = InterlockedCompareExchange(&ui_health_pings_sent, 0, 0);
    acked = InterlockedCompareExchange(&ui_health_pings_acked, 0, 0);
    if (
        previous_tick &&
        now >= previous_tick &&
        now - previous_tick > UI_HEALTH_RESUME_GAP_MS
    ) {
        /* A suspended laptop is not a hung Qt event loop. */
        ui_health_clear_pending(sent);
        acked = sent;
    }
    if (sent != acked) {
        window = (HWND)InterlockedCompareExchangePointer(
            &ui_health_ping_window, NULL, NULL
        );
        generation = InterlockedCompareExchange(
            &ui_health_ping_generation, 0, 0
        );
        if (!ui_health_target_is_current(window, generation)) {
            InterlockedIncrement(&ui_health_window_invalidations);
            ui_health_clear_pending(sent);
            return;
        }
        messages_before = InterlockedCompareExchange(
            &ui_health_ping_messages_dequeued, 0, 0
        );
        messages_now = InterlockedCompareExchange(
            &event_loop_messages_dequeued, 0, 0
        );
        if (messages_now != messages_before) {
            InterlockedIncrement(&ui_health_progress_cancellations);
            ui_health_clear_pending(sent);
            return;
        }
        sent_at = (ULONGLONG)InterlockedCompareExchange64(
            &ui_health_ping_sent_at, 0, 0
        );
        if (
            sent_at &&
            now >= sent_at &&
            now - sent_at >= UI_HEALTH_TIMEOUT_MS
        ) {
            InterlockedIncrement(&ui_health_timeouts);
            guard_breaks_before = InterlockedCompareExchange(
                &ui_health_ping_guard_breaks, 0, 0
            );
            guard_breaks_now = InterlockedCompareExchange(
                &event_loop_guard_breaks, 0, 0
            );
            if (ui_health_has_livelock_evidence(
                    guard_breaks_before,
                    guard_breaks_now,
                    messages_before,
                    messages_now
                )) {
                request_controller_restart(generation, guard_breaks_now);
            } else {
                InterlockedIncrement(
                    &ui_health_no_livelock_suppressions
                );
                ui_health_clear_pending(sent);
            }
        }
        return;
    }
    if (
        InterlockedCompareExchange(&ui_health_recovery_requested, 0, 0)
    ) {
        return;
    }
    generation = 0;
    window = focus_ui_health_window(&generation);
    if (!window || !IsWindow(window)) {
        return;
    }
    next = sent + 1;
    if (next <= 0) {
        next = 1;
    }
    InterlockedExchangePointer(&ui_health_ping_window, window);
    InterlockedExchange(&ui_health_ping_generation, generation);
    InterlockedExchange(
        &ui_health_ping_guard_breaks,
        InterlockedCompareExchange(&event_loop_guard_breaks, 0, 0)
    );
    InterlockedExchange(
        &ui_health_ping_messages_dequeued,
        InterlockedCompareExchange(&event_loop_messages_dequeued, 0, 0)
    );
    InterlockedExchange64(&ui_health_ping_sent_at, (LONG64)now);
    InterlockedExchange(&ui_health_pings_sent, next);
    if (!PostMessageW(
            window,
            WM_UU_UI_HEALTH,
            (WPARAM)next,
            (LPARAM)generation
        )) {
        InterlockedIncrement(&ui_health_window_invalidations);
        ui_health_clear_pending(next);
    }
}

static void focus_note_window_chain(
    HWND window,
    WNDPROC external_proc,
    enum focus_window_role role
) {
    unsigned int index;
    BOOL changed = FALSE;
    BOOL role_changed = FALSE;

    AcquireSRWLockExclusive(&focus_window_lock);
    for (index = 0; index < FOCUS_MAX_WINDOWS; ++index) {
        if (focus_windows[index].window == window) {
            if (focus_windows[index].role != role) {
                role_changed = TRUE;
            }
            focus_windows[index].role = role;
            if (focus_windows[index].external_proc != external_proc) {
                focus_windows[index].external_proc = external_proc;
                changed = external_proc != NULL;
            }
            break;
        }
    }
    if (role_changed) {
        InterlockedIncrement(&focus_window_generation);
    }
    ReleaseSRWLockExclusive(&focus_window_lock);
    if (changed) {
        InterlockedIncrement(&focus_external_subclass_chains);
    }
}

static void focus_record_transition(HWND window) {
    ULONGLONG now = GetTickCount64();
    ULONGLONG last_internal_apply;

    InterlockedIncrement(&focus_transition_messages);
    last_internal_apply = (ULONGLONG)InterlockedCompareExchange64(
        &focus_last_internal_apply, 0, 0
    );
    if (
        last_internal_apply &&
        now >= last_internal_apply &&
        now - last_internal_apply <= FOCUS_INTERNAL_APPLY_GRACE_MS
    ) {
        InterlockedIncrement(&focus_internal_transitions_ignored);
        return;
    }
    AcquireSRWLockExclusive(&focus_rate_lock);
    if (
        !focus_rate_window_started ||
        now < focus_rate_window_started ||
        now - focus_rate_window_started > FOCUS_STORM_WINDOW_MS
    ) {
        focus_rate_window_started = now;
        focus_rate_window_count = 0;
    }
    ++focus_rate_window_count;
    if (
        focus_rate_window_count >= FOCUS_STORM_THRESHOLD &&
        InterlockedCompareExchange(&focus_storm_pending, 1, 0) == 0
    ) {
        InterlockedIncrement(&focus_storms_detected);
        InterlockedExchange(&focus_arbitration_pending, 1);
        if (window) {
            PostMessageW(window, WM_NULL, 0, 0);
        }
        if (focus_worker_event) {
            SetEvent(focus_worker_event);
        }
    }
    ReleaseSRWLockExclusive(&focus_rate_lock);
}

static void focus_clear_latch(void) {
    InterlockedExchange(&focus_latch_active, 0);
    InterlockedExchange64(&focus_latch_until, 0);
    InterlockedExchangePointer(&focus_preferred_window, NULL);
    InterlockedExchange(&focus_preferred_role, FOCUS_ROLE_UNKNOWN);
}

static BOOL focus_home_show_request_pending(void) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    FILETIME current_time;
    ULARGE_INTEGER modified;
    ULARGE_INTEGER current;
    ULONGLONG maximum_age;

    if (
        !focus_show_request_path[0] ||
        !GetFileAttributesExA(
            focus_show_request_path,
            GetFileExInfoStandard,
            &attributes
        )
    ) {
        return FALSE;
    }
    if (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        DeleteFileA(focus_show_request_path);
        return FALSE;
    }
    GetSystemTimeAsFileTime(&current_time);
    modified.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    modified.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    current.LowPart = current_time.dwLowDateTime;
    current.HighPart = current_time.dwHighDateTime;
    maximum_age = (ULONGLONG)FOCUS_SHOW_REQUEST_TTL_MS * 10000u;
    if (
        current.QuadPart >= modified.QuadPart &&
        current.QuadPart - modified.QuadPart > maximum_age
    ) {
        DeleteFileA(focus_show_request_path);
        return FALSE;
    }
    return TRUE;
}

static BOOL focus_consume_home_show_request(void) {
    if (
        !focus_home_show_request_pending() ||
        !DeleteFileA(focus_show_request_path)
    ) {
        return FALSE;
    }
    InterlockedExchange(&focus_home_hidden_by_user, 0);
    InterlockedIncrement(&focus_home_show_authorized);
    focus_clear_latch();
    return TRUE;
}

static BOOL focus_show_command_makes_visible(int command) {
    switch (command) {
        case SW_SHOW:
        case SW_SHOWNORMAL:
        case SW_SHOWDEFAULT:
        case SW_RESTORE:
        case SW_SHOWMAXIMIZED:
        case SW_SHOWMINIMIZED:
        case SW_SHOWMINNOACTIVE:
        case SW_SHOWNOACTIVATE:
        case SW_SHOWNA:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL focus_home_raise_is_blocked(HWND window) {
    enum focus_window_role role = FOCUS_ROLE_UNKNOWN;
    HWND remote;

    if (
        !window ||
        !InterlockedCompareExchange(&focus_home_hidden_by_user, 0, 0) ||
        !focus_window_entry(window, NULL, &role) ||
        role != FOCUS_ROLE_HOME
    ) {
        return FALSE;
    }
    remote = (HWND)InterlockedCompareExchangePointer(
        &focus_visible_remote_window, NULL, NULL
    );
    if (!remote || !IsWindow(remote) || !IsWindowVisible(remote)) {
        InterlockedExchange(&focus_home_hidden_by_user, 0);
        return FALSE;
    }
    if (focus_consume_home_show_request()) {
        return FALSE;
    }
    InterlockedIncrement(&focus_home_reopen_blocked);
    InterlockedIncrement(&focus_blocked_activations);
    return TRUE;
}

static BOOL focus_deactivation_is_blocked(
    HWND window,
    const MSG *message
) {
    HWND preferred;
    HWND root;
    ULONGLONG until;
    ULONGLONG now;

    if (
        !window ||
        !message ||
        !InterlockedCompareExchange(&focus_latch_active, 0, 0) ||
        !focus_transition_is_internal(message)
    ) {
        return FALSE;
    }
    until = (ULONGLONG)InterlockedCompareExchange64(
        &focus_latch_until, 0, 0
    );
    now = GetTickCount64();
    if (!until || now >= until) {
        focus_clear_latch();
        return FALSE;
    }
    preferred = (HWND)InterlockedCompareExchangePointer(
        &focus_preferred_window, NULL, NULL
    );
    if (!preferred || !IsWindow(preferred)) {
        focus_clear_latch();
        return FALSE;
    }
    if (message->message == WM_ACTIVATEAPP) {
        return TRUE;
    }
    root = GetAncestor(window, GA_ROOT);
    return root && root == preferred;
}

static BOOL focus_activation_is_blocked(HWND window) {
    HWND root;
    HWND preferred;
    ULONGLONG until;
    ULONGLONG now;

    if (!window || !InterlockedCompareExchange(&focus_latch_active, 0, 0)) {
        return FALSE;
    }
    root = GetAncestor(window, GA_ROOT);
    if (!root || root != window || !window_belongs_to_controller(root)) {
        return FALSE;
    }
    preferred = (HWND)InterlockedCompareExchangePointer(
        &focus_preferred_window, NULL, NULL
    );
    if (!preferred || root == preferred) {
        return FALSE;
    }
    until = (ULONGLONG)InterlockedCompareExchange64(
        &focus_latch_until, 0, 0
    );
    now = GetTickCount64();
    if (!until || now >= until) {
        focus_clear_latch();
        return FALSE;
    }
    return TRUE;
}

static BOOL WINAPI hooked_set_foreground_window(HWND window) {
    if (focus_home_raise_is_blocked(window)) {
        return TRUE;
    }
    if (focus_activation_is_blocked(window)) {
        InterlockedIncrement(&focus_blocked_activations);
        return TRUE;
    }
    if (!original_set_foreground_window) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return original_set_foreground_window(window);
}

static HWND WINAPI hooked_set_active_window(HWND window) {
    if (focus_home_raise_is_blocked(window)) {
        return GetActiveWindow();
    }
    if (focus_activation_is_blocked(window)) {
        InterlockedIncrement(&focus_blocked_activations);
        return (HWND)InterlockedCompareExchangePointer(
            &focus_preferred_window, NULL, NULL
        );
    }
    if (!original_set_active_window) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return NULL;
    }
    return original_set_active_window(window);
}

static BOOL WINAPI hooked_bring_window_to_top(HWND window) {
    if (focus_home_raise_is_blocked(window)) {
        return TRUE;
    }
    if (focus_activation_is_blocked(window)) {
        InterlockedIncrement(&focus_blocked_activations);
        return TRUE;
    }
    if (!original_bring_window_to_top) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return original_bring_window_to_top(window);
}

static BOOL WINAPI hooked_set_window_pos(
    HWND window,
    HWND insert_after,
    int x,
    int y,
    int width,
    int height,
    UINT flags
) {
    if (focus_home_raise_is_blocked(window)) {
        flags &= ~SWP_SHOWWINDOW;
        flags |= SWP_NOACTIVATE | SWP_NOZORDER;
        insert_after = NULL;
    }
    if (focus_activation_is_blocked(window)) {
        flags |= SWP_NOACTIVATE | SWP_NOZORDER;
        insert_after = NULL;
        InterlockedIncrement(&focus_blocked_activations);
    }
    if (!original_set_window_pos) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return original_set_window_pos(
        window, insert_after, x, y, width, height, flags
    );
}

static BOOL WINAPI hooked_show_window(HWND window, int command) {
    if (
        focus_show_command_makes_visible(command) &&
        focus_home_raise_is_blocked(window)
    ) {
        return IsWindowVisible(window);
    }
    if (focus_activation_is_blocked(window)) {
        switch (command) {
            case SW_SHOW:
            case SW_SHOWNORMAL:
            case SW_SHOWDEFAULT:
            case SW_RESTORE:
            case SW_SHOWMAXIMIZED:
                command = SW_SHOWNOACTIVATE;
                InterlockedIncrement(&focus_blocked_activations);
                break;
            default:
                break;
        }
    }
    if (!original_show_window) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return original_show_window(window, command);
}

static BOOL focus_nonclient_right_click_is_unsafe(
    enum focus_window_role role,
    UINT message,
    WPARAM wparam
) {
    if (
        role != FOCUS_ROLE_HOME &&
        role != FOCUS_ROLE_VIDEO
    ) {
        return FALSE;
    }
    return
        message == WM_NCRBUTTONDOWN &&
        (wparam == HTCAPTION || wparam == HTSYSMENU);
}

static LRESULT CALLBACK focus_subclass_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    WNDPROC original_proc = NULL;
    enum focus_window_role role = FOCUS_ROLE_UNKNOWN;
    MSG probe;
    LRESULT result;

    if (
        !focus_window_entry(window, &original_proc, &role) ||
        !original_proc ||
        original_proc == focus_subclass_window_proc
    ) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_UU_HOME_SHOW) {
        if (
            role == FOCUS_ROLE_HOME &&
            focus_consume_home_show_request()
        ) {
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
            write_focus_hook_status();
        }
        return 0;
    }
    if (message == WM_UU_UI_HEALTH) {
        if (
            window == (HWND)InterlockedCompareExchangePointer(
                &ui_health_ping_window, NULL, NULL
            ) &&
            (LONG)wparam == InterlockedCompareExchange(
                &ui_health_pings_sent, 0, 0
            ) &&
            (LONG)lparam == InterlockedCompareExchange(
                &ui_health_ping_generation, 0, 0
            )
        ) {
            InterlockedExchange(&ui_health_pings_acked, (LONG)wparam);
            InterlockedExchange64(&ui_health_ping_sent_at, 0);
            InterlockedExchangePointer(&ui_health_ping_window, NULL);
        }
        return 0;
    }
    if (
        message == WM_NULL &&
        InterlockedExchange(&focus_arbitration_pending, 0)
    ) {
        focus_arbitrate_windows();
    }
    if (message == WM_UU_FOCUS_APPLY) {
        if (
            window == (HWND)InterlockedCompareExchangePointer(
                &focus_preferred_window, NULL, NULL
            ) &&
            IsWindowVisible(window) &&
            GetForegroundWindow() != window
        ) {
            InterlockedExchange64(
                &focus_last_internal_apply, (LONG64)GetTickCount64()
            );
            if (original_set_foreground_window) {
                original_set_foreground_window(window);
            }
        }
        return 0;
    }
    /*
     * Wine's default WM_NCRBUTTONDOWN handler captures the mouse and runs a
     * nested GetMessage loop until it receives WM_RBUTTONUP. UU's frameless
     * Qt title bars can receive the non-client press through Wine/X11 while
     * the matching release is consumed outside the Win32 queue. The nested
     * loop then owns the Qt UI thread forever and the unmapped/repainted
     * window becomes white. These two application-owned title bars do not
     * need Wine's native system menu, so keep the message out of DefWindowProc
     * instead of trying to repair an already-lost release event afterwards.
     */
    if (focus_nonclient_right_click_is_unsafe(role, message, wparam)) {
        InterlockedIncrement(&focus_nonclient_right_clicks_suppressed);
        return 0;
    }
    ZeroMemory(&probe, sizeof(probe));
    probe.hwnd = window;
    probe.message = message;
    probe.wParam = wparam;
    probe.lParam = lparam;

    if (
        role == FOCUS_ROLE_HOME &&
        (
            message == WM_CLOSE ||
            (message == WM_SHOWWINDOW && !wparam) ||
            (
                message == WM_WINDOWPOSCHANGED &&
                lparam &&
                (((WINDOWPOS *)lparam)->flags & SWP_HIDEWINDOW)
            )
        )
    ) {
        InterlockedExchange(&focus_home_hidden_by_user, 1);
    }
    if (
        role == FOCUS_ROLE_VIDEO &&
        message == WM_SHOWWINDOW
    ) {
        if (wparam) {
            InterlockedExchangePointer(
                &focus_visible_remote_window, window
            );
        } else if (
            window == (HWND)InterlockedCompareExchangePointer(
                &focus_visible_remote_window, NULL, NULL
            )
        ) {
            InterlockedExchangePointer(&focus_visible_remote_window, NULL);
        }
    }
    if (
        role == FOCUS_ROLE_HOME &&
        message == WM_SHOWWINDOW &&
        wparam &&
        !InterlockedCompareExchange(&focus_home_hidden_by_user, 0, 0) &&
        focus_show_request_path[0]
    ) {
        DeleteFileA(focus_show_request_path);
    }
    if (message == WM_ACTIVATE || message == WM_ACTIVATEAPP) {
        focus_record_transition(window);
    }
    if (
        message == WM_ACTIVATE &&
        LOWORD(wparam) == WA_INACTIVE &&
        focus_deactivation_is_blocked(window, &probe)
    ) {
        InterlockedIncrement(&focus_suppressed_activate);
        return 0;
    }
    if (
        message == WM_ACTIVATEAPP &&
        !wparam &&
        focus_transition_is_internal(&probe)
    ) {
        InterlockedIncrement(&focus_suppressed_activate_app);
        return 0;
    }
    if (
        message == WM_NCACTIVATE &&
        !wparam &&
        focus_deactivation_is_blocked(window, &probe)
    ) {
        wparam = TRUE;
        InterlockedIncrement(&focus_stabilized_nonclient);
    }
    if (message == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *position = (WINDOWPOS *)lparam;
        if (
            position &&
            (position->flags & SWP_SHOWWINDOW) &&
            focus_home_raise_is_blocked(window)
        ) {
            position->flags &= ~SWP_SHOWWINDOW;
            position->flags |= SWP_NOACTIVATE | SWP_NOZORDER;
            position->hwndInsertAfter = NULL;
        }
        if (position && focus_activation_is_blocked(window)) {
            position->flags |= SWP_NOACTIVATE | SWP_NOZORDER;
            position->hwndInsertAfter = NULL;
            InterlockedIncrement(&focus_blocked_activations);
        }
    }
    if (
        message == WM_MOUSEACTIVATE &&
        role != FOCUS_ROLE_DIALOG &&
        focus_activation_is_blocked(window)
    ) {
        focus_clear_latch();
        InterlockedIncrement(&focus_user_overrides);
    }

    result = CallWindowProcW(
        original_proc, window, message, wparam, lparam
    );
    if (message == WM_NCDESTROY) {
        focus_remove_window(window);
    }
    return result;
}

static BOOL focus_ensure_window_subclassed(
    HWND window,
    enum focus_window_role role
) {
    unsigned int index;
    WNDPROC original_proc;
    LONG_PTR previous;

    if (focus_window_entry(window, NULL, NULL)) {
        SetLastError(ERROR_SUCCESS);
        previous = GetWindowLongPtrW(window, GWLP_WNDPROC);
        if (!previous && GetLastError() != ERROR_SUCCESS) {
            return FALSE;
        }
        if ((WNDPROC)previous == focus_subclass_window_proc) {
            focus_note_window_chain(window, NULL, role);
            return TRUE;
        }

        /*
         * A framework may legitimately install an outer WndProc after us.
         * It normally keeps our procedure as its next link. Moving our hook
         * above it would create hook -> outer -> hook recursion and spin the
         * UI thread forever. Preserve the framework-owned outer link;
         * messages that it chains continue to reach this hook exactly once.
         */
        focus_note_window_chain(window, (WNDPROC)previous, role);
        return TRUE;
    }
    SetLastError(ERROR_SUCCESS);
    original_proc = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (!original_proc && GetLastError() != ERROR_SUCCESS) {
        return FALSE;
    }

    AcquireSRWLockExclusive(&focus_window_lock);
    for (index = 0; index < FOCUS_MAX_WINDOWS; ++index) {
        if (!focus_windows[index].window) {
            focus_windows[index].window = window;
            focus_windows[index].original_proc = original_proc;
            focus_windows[index].external_proc = NULL;
            focus_windows[index].role = role;
            InterlockedIncrement(&focus_window_generation);
            break;
        }
    }
    ReleaseSRWLockExclusive(&focus_window_lock);
    if (index == FOCUS_MAX_WINDOWS) {
        return FALSE;
    }
    InterlockedIncrement(&focus_subclassed_windows);

    SetLastError(ERROR_SUCCESS);
    previous = SetWindowLongPtrW(
        window,
        GWLP_WNDPROC,
        (LONG_PTR)focus_subclass_window_proc
    );
    if (!previous && GetLastError() != ERROR_SUCCESS) {
        focus_remove_window(window);
        return FALSE;
    }
    InterlockedIncrement(&focus_subclass_installations);
    return TRUE;
}

struct focus_window_scan {
    HWND foreground;
    enum focus_window_role foreground_role;
    HWND dialog;
    HWND video;
    HWND home;
    HWND nonhome;
    LONG64 video_area;
    LONG64 nonhome_area;
};

static BOOL CALLBACK focus_scan_window(HWND window, LPARAM parameter) {
    struct focus_window_scan *scan = (struct focus_window_scan *)parameter;
    enum focus_window_role role;
    DWORD pid = 0;
    RECT rectangle;
    LONG64 area = 0;
    LONG_PTR ex_style;

    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) {
        return TRUE;
    }
    if (!focus_window_entry(window, NULL, &role)) {
        role = focus_window_role_for(window);
    }
    focus_ensure_window_subclassed(window, role);
    if (role == FOCUS_ROLE_HOME) {
        scan->home = window;
    }
    if (!IsWindowVisible(window)) {
        return TRUE;
    }
    if (window == GetForegroundWindow()) {
        scan->foreground = window;
        scan->foreground_role = role;
    }
    if (GetWindowRect(window, &rectangle)) {
        area = (LONG64)(rectangle.right - rectangle.left) *
            (LONG64)(rectangle.bottom - rectangle.top);
    }
    if (role == FOCUS_ROLE_DIALOG) {
        scan->dialog = window;
    } else if (role == FOCUS_ROLE_VIDEO && area >= scan->video_area) {
        scan->video = window;
        scan->video_area = area;
    } else if (role != FOCUS_ROLE_HOME) {
        ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (
            !GetWindow(window, GW_OWNER) &&
            !(ex_style & WS_EX_TOOLWINDOW) &&
            area >= scan->nonhome_area
        ) {
            scan->nonhome = window;
            scan->nonhome_area = area;
        }
    }
    return TRUE;
}

static void focus_apply_latch(
    HWND target,
    enum focus_window_role role,
    ULONGLONG duration
) {
    HWND previous;
    ULONGLONG now;
    ULONGLONG last_posted;

    if (!target || !IsWindow(target)) {
        return;
    }
    previous = (HWND)InterlockedCompareExchangePointer(
        &focus_preferred_window, NULL, NULL
    );
    (void)previous;
    InterlockedExchangePointer(&focus_preferred_window, target);
    InterlockedExchange(&focus_preferred_role, (LONG)role);
    InterlockedExchange64(
        &focus_latch_until, (LONG64)(GetTickCount64() + duration)
    );
    InterlockedExchange(&focus_latch_active, 1);

    if (GetForegroundWindow() == target) {
        return;
    }
    now = GetTickCount64();
    last_posted = (ULONGLONG)InterlockedCompareExchange64(
        &focus_last_apply_posted, 0, 0
    );
    if (
        last_posted &&
        now >= last_posted &&
        now - last_posted < FOCUS_APPLY_COOLDOWN_MS
    ) {
        InterlockedIncrement(&focus_apply_rate_limited);
        return;
    }
    if (PostMessageW(target, WM_UU_FOCUS_APPLY, 0, 0)) {
        InterlockedExchange64(&focus_last_apply_posted, (LONG64)now);
        InterlockedIncrement(&focus_apply_posted);
    }
}

static void focus_arbitrate_windows(void) {
    struct focus_window_scan scan;
    HWND target = NULL;
    enum focus_window_role role = FOCUS_ROLE_UNKNOWN;
    BOOL storm;
    BOOL modal_visible;
    ULONGLONG now = GetTickCount64();

    ZeroMemory(&scan, sizeof(scan));
    EnumWindows(focus_scan_window, (LPARAM)&scan);
    InterlockedExchangePointer(
        &focus_visible_remote_window,
        scan.video ? scan.video : scan.nonhome
    );
    if (
        scan.home &&
        focus_home_show_request_pending()
    ) {
        PostMessageW(scan.home, WM_UU_HOME_SHOW, 0, 0);
    }
    modal_visible = scan.dialog != NULL;
    if (modal_visible) {
        if (!focus_modal_was_visible) {
            InterlockedIncrement(&focus_modal_latches);
        }
        focus_apply_latch(
            scan.dialog, FOCUS_ROLE_DIALOG, FOCUS_MODAL_REFRESH_MS
        );
    } else if (focus_modal_was_visible) {
        target = scan.video ? scan.video : scan.nonhome;
        if (target) {
            InterlockedIncrement(&focus_post_modal_handoffs);
            focus_apply_latch(target, FOCUS_ROLE_VIDEO, FOCUS_LATCH_MS);
        }
    }
    focus_modal_was_visible = modal_visible;

    storm = InterlockedExchange(&focus_storm_pending, 0) != 0;
    if (storm) {
        if (scan.dialog) {
            target = scan.dialog;
            role = FOCUS_ROLE_DIALOG;
        } else if (scan.video) {
            target = scan.video;
            role = FOCUS_ROLE_VIDEO;
        } else if (scan.nonhome) {
            target = scan.nonhome;
            role = FOCUS_ROLE_VIDEO;
        } else if (scan.foreground) {
            target = scan.foreground;
            role = scan.foreground_role;
        } else {
            target = scan.home;
            role = FOCUS_ROLE_HOME;
        }
        if (target) {
            focus_apply_latch(target, role, FOCUS_LATCH_MS);
            InterlockedIncrement(&focus_storms_resolved);
        }
    }
    if (
        !modal_visible &&
        InterlockedCompareExchange(&focus_latch_active, 0, 0) &&
        now >= (ULONGLONG)InterlockedCompareExchange64(
            &focus_latch_until, 0, 0
        )
    ) {
        focus_clear_latch();
    }
}

struct focus_wakeup_scan {
    HWND window;
};

static BOOL CALLBACK focus_find_wakeup_window(
    HWND window,
    LPARAM parameter
) {
    struct focus_wakeup_scan *scan = (struct focus_wakeup_scan *)parameter;
    DWORD pid = 0;

    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId()) {
        scan->window = window;
        return FALSE;
    }
    return TRUE;
}

static void focus_queue_arbitration(void) {
    struct focus_wakeup_scan scan;
    HWND wake_window = NULL;
    DWORD wake_thread = 0;
    unsigned int index;

    InterlockedExchange(&focus_arbitration_pending, 1);
    AcquireSRWLockShared(&focus_window_lock);
    for (index = 0; index < FOCUS_MAX_WINDOWS; ++index) {
        if (focus_windows[index].window) {
            wake_window = focus_windows[index].window;
            break;
        }
    }
    ReleaseSRWLockShared(&focus_window_lock);

    if (!wake_window) {
        ZeroMemory(&scan, sizeof(scan));
        EnumWindows(focus_find_wakeup_window, (LPARAM)&scan);
        wake_window = scan.window;
    }
    if (wake_window) {
        wake_thread = GetWindowThreadProcessId(wake_window, NULL);
        if (wake_thread == GetCurrentThreadId()) {
            InterlockedExchange(&focus_arbitration_pending, 0);
            focus_arbitrate_windows();
        } else {
            PostMessageW(wake_window, WM_NULL, 0, 0);
        }
    }
}

static void focus_mark_activation_api(LONG bit) {
    LONG previous;
    LONG updated;

    do {
        previous = InterlockedCompareExchange(
            &focus_activation_api_mask, 0, 0
        );
        updated = previous | bit;
    } while (
        InterlockedCompareExchange(
            &focus_activation_api_mask, updated, previous
        ) != previous
    );
}

static BOOL focus_mark_module_for_scan(HMODULE module) {
    unsigned int index;
    unsigned int empty = FOCUS_MAX_MODULES;
    BOOL should_scan = FALSE;

    AcquireSRWLockExclusive(&focus_module_lock);
    for (index = 0; index < FOCUS_MAX_MODULES; ++index) {
        if (focus_scanned_modules[index] == module) {
            break;
        }
        if (!focus_scanned_modules[index] && empty == FOCUS_MAX_MODULES) {
            empty = index;
        }
    }
    if (index == FOCUS_MAX_MODULES && empty < FOCUS_MAX_MODULES) {
        focus_scanned_modules[empty] = module;
        should_scan = TRUE;
    }
    ReleaseSRWLockExclusive(&focus_module_lock);
    return should_scan;
}

static void patch_focus_activation_imports(HMODULE module) {
    if (
        !module ||
        module == hook_module ||
        !focus_mark_module_for_scan(module)
    ) {
        return;
    }
    if (patch_import(
        module,
        "USER32.dll",
        "SetForegroundWindow",
        (void *)hooked_set_foreground_window,
        (void **)&original_set_foreground_window
    )) {
        focus_mark_activation_api(1);
    }
    if (patch_import(
        module,
        "USER32.dll",
        "SetActiveWindow",
        (void *)hooked_set_active_window,
        (void **)&original_set_active_window
    )) {
        focus_mark_activation_api(2);
    }
    if (patch_import(
        module,
        "USER32.dll",
        "BringWindowToTop",
        (void *)hooked_bring_window_to_top,
        (void **)&original_bring_window_to_top
    )) {
        focus_mark_activation_api(4);
    }
    if (patch_import(
        module,
        "USER32.dll",
        "SetWindowPos",
        (void *)hooked_set_window_pos,
        (void **)&original_set_window_pos
    )) {
        focus_mark_activation_api(8);
    }
    if (patch_import(
        module,
        "USER32.dll",
        "ShowWindow",
        (void *)hooked_show_window,
        (void **)&original_show_window
    )) {
        focus_mark_activation_api(16);
    }
}

static void patch_all_focus_activation_imports(void) {
    HANDLE snapshot;
    MODULEENTRY32W module_entry;

    snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId()
    );
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    ZeroMemory(&module_entry, sizeof(module_entry));
    module_entry.dwSize = sizeof(module_entry);
    if (Module32FirstW(snapshot, &module_entry)) {
        do {
            patch_focus_activation_imports(module_entry.hModule);
        } while (Module32NextW(snapshot, &module_entry));
    }
    CloseHandle(snapshot);
}

static BOOL focus_transition_is_internal(const MSG *message) {
    HWND counterpart = NULL;
    HWND foreground;
    ULONGLONG last_transition;
    ULONGLONG now;

    if (message->message == WM_ACTIVATE && message->lParam) {
        counterpart = (HWND)message->lParam;
        return window_belongs_to_controller(counterpart);
    }
    if (message->message == WM_ACTIVATEAPP && message->lParam) {
        return thread_belongs_to_controller((DWORD)message->lParam);
    }
    foreground = GetForegroundWindow();
    if (foreground) {
        return window_belongs_to_controller(foreground);
    }
    last_transition = (ULONGLONG)InterlockedCompareExchange64(
        &focus_last_internal_transition, 0, 0
    );
    now = GetTickCount64();
    return last_transition &&
        now >= last_transition &&
        now - last_transition <= FOCUS_INTERNAL_GRACE_MS;
}

static LRESULT WINAPI hooked_dispatch_message_w(const MSG *message) {
    MSG adjusted;

    if (!original_dispatch_message_w || !message) {
        return 0;
    }
    if (InterlockedExchange(&focus_module_scan_pending, 0)) {
        patch_controller_focus_imports();
        patch_all_focus_activation_imports();
    }
    /*
     * Enumerating and subclassing Qt windows from the hook worker can block
     * behind synchronous UI-thread sends during an activation storm. Run the
     * arbitration pass at this safe point in the controller message loop.
     */
    if (InterlockedExchange(&focus_arbitration_pending, 0)) {
        focus_arbitrate_windows();
    }
    if (
        message->message == WM_ACTIVATE &&
        LOWORD(message->wParam) == WA_INACTIVE &&
        focus_deactivation_is_blocked(message->hwnd, message)
    ) {
        InterlockedIncrement(&focus_suppressed_activate);
        return 0;
    }
    if (
        message->message == WM_ACTIVATEAPP &&
        !message->wParam &&
        focus_transition_is_internal(message)
    ) {
        InterlockedIncrement(&focus_suppressed_activate_app);
        return 0;
    }
    if (
        message->message == WM_NCACTIVATE &&
        !message->wParam &&
        focus_deactivation_is_blocked(message->hwnd, message)
    ) {
        adjusted = *message;
        adjusted.wParam = TRUE;
        InterlockedIncrement(&focus_stabilized_nonclient);
        return original_dispatch_message_w(&adjusted);
    }
    return original_dispatch_message_w(message);
}

static HHOOK WINAPI hooked_set_windows_hook_ex_w(
    int hook_id,
    HOOKPROC callback,
    HINSTANCE module,
    DWORD thread_id
) {
    HHOOK result;

    if (!original_set_windows_hook_ex_w) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return NULL;
    }
    if (hook_id != WH_KEYBOARD_LL || !callback || thread_id != 0) {
        return original_set_windows_hook_ex_w(
            hook_id, callback, module, thread_id
        );
    }
    InterlockedExchange64(
        &focus_last_internal_transition, (LONG64)GetTickCount64()
    );
    AcquireSRWLockExclusive(&focus_keyboard_hook_lock);
    if (
        focus_keyboard_hook &&
        focus_keyboard_proc == callback &&
        focus_keyboard_module == module &&
        focus_keyboard_thread_id == thread_id
    ) {
        focus_keyboard_unhook_pending = FALSE;
        result = focus_keyboard_hook;
        InterlockedIncrement(&focus_keyboard_reused);
        ReleaseSRWLockExclusive(&focus_keyboard_hook_lock);
        if (focus_worker_event) {
            SetEvent(focus_worker_event);
        }
        return result;
    }
    ReleaseSRWLockExclusive(&focus_keyboard_hook_lock);
    result = original_set_windows_hook_ex_w(
        hook_id, callback, module, thread_id
    );
    if (!result) {
        return NULL;
    }
    AcquireSRWLockExclusive(&focus_keyboard_hook_lock);
    if (!focus_keyboard_hook) {
        focus_keyboard_hook = result;
        focus_keyboard_proc = callback;
        focus_keyboard_module = module;
        focus_keyboard_thread_id = thread_id;
        focus_keyboard_unhook_pending = FALSE;
        InterlockedIncrement(&focus_keyboard_installed);
    }
    ReleaseSRWLockExclusive(&focus_keyboard_hook_lock);
    return result;
}

static BOOL WINAPI hooked_unhook_windows_hook_ex(HHOOK hook) {
    if (!original_unhook_windows_hook_ex) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    AcquireSRWLockExclusive(&focus_keyboard_hook_lock);
    if (hook && hook == focus_keyboard_hook) {
        focus_keyboard_unhook_pending = TRUE;
        focus_keyboard_unhook_at =
            GetTickCount64() + FOCUS_UNHOOK_GRACE_MS;
        InterlockedExchange64(
            &focus_last_internal_transition, (LONG64)GetTickCount64()
        );
        InterlockedIncrement(&focus_keyboard_deferred);
        ReleaseSRWLockExclusive(&focus_keyboard_hook_lock);
        if (focus_worker_event) {
            SetEvent(focus_worker_event);
        }
        return TRUE;
    }
    ReleaseSRWLockExclusive(&focus_keyboard_hook_lock);
    return original_unhook_windows_hook_ex(hook);
}

static void patch_controller_focus_imports(void) {
    HMODULE main_module = GetModuleHandleW(NULL);
    HMODULE qt_core = GetModuleHandleW(L"Qt5Core.dll");

    if (!controller_dispatch_hooked) {
        controller_dispatch_hooked = patch_import(
            main_module,
            "USER32.dll",
            "DispatchMessageW",
            (void *)hooked_dispatch_message_w,
            (void **)&original_dispatch_message_w
        );
    }
    if (!controller_set_hook_hooked) {
        controller_set_hook_hooked = patch_import(
            main_module,
            "USER32.dll",
            "SetWindowsHookExW",
            (void *)hooked_set_windows_hook_ex_w,
            (void **)&original_set_windows_hook_ex_w
        );
    }
    if (!controller_unhook_hooked) {
        controller_unhook_hooked = patch_import(
            main_module,
            "USER32.dll",
            "UnhookWindowsHookEx",
            (void *)hooked_unhook_windows_hook_ex,
            (void **)&original_unhook_windows_hook_ex
        );
    }
    if (qt_core && !qt_dispatch_hooked) {
        qt_dispatch_hooked = patch_import(
            qt_core,
            "USER32.dll",
            "DispatchMessageW",
            (void *)hooked_dispatch_message_w,
            (void **)&original_dispatch_message_w
        );
    }
    if (qt_core && !qt_peek_message_hooked) {
        qt_peek_message_hooked = patch_import(
            qt_core,
            "USER32.dll",
            "PeekMessageW",
            (void *)hooked_peek_message_w,
            (void **)&original_peek_message_w
        );
    }
    if (qt_core && !qt_msg_wait_hooked) {
        qt_msg_wait_hooked = patch_import(
            qt_core,
            "USER32.dll",
            "MsgWaitForMultipleObjectsEx",
            (void *)hooked_msg_wait_for_multiple_objects_ex,
            (void **)&original_msg_wait_for_multiple_objects_ex
        );
    }
    if (qt_core && !qt_post_message_hooked) {
        qt_post_message_hooked = patch_import(
            qt_core,
            "USER32.dll",
            "PostMessageW",
            (void *)hooked_post_message_w,
            (void **)&original_post_message_w
        );
    }
}

static DWORD WINAPI focus_worker_main(LPVOID parameter) {
    ULONGLONG last_status_write = 0;
    ULONGLONG last_activation_scan = 0;
    ULONGLONG last_window_scan = 0;
    ULONGLONG last_health_ping = 0;
    (void)parameter;

    for (;;) {
        HHOOK release_hook = NULL;
        ULONGLONG now;

        WaitForSingleObject(focus_worker_event, FOCUS_WINDOW_SCAN_MS);
        now = GetTickCount64();
        AcquireSRWLockExclusive(&focus_keyboard_hook_lock);
        if (
            focus_keyboard_hook &&
            focus_keyboard_unhook_pending &&
            now >= focus_keyboard_unhook_at
        ) {
            release_hook = focus_keyboard_hook;
            focus_keyboard_hook = NULL;
            focus_keyboard_proc = NULL;
            focus_keyboard_module = NULL;
            focus_keyboard_thread_id = 0;
            focus_keyboard_unhook_pending = FALSE;
        }
        ReleaseSRWLockExclusive(&focus_keyboard_hook_lock);
        if (release_hook && original_unhook_windows_hook_ex) {
            original_unhook_windows_hook_ex(release_hook);
            InterlockedIncrement(&focus_keyboard_released);
        }
        if (now - last_health_ping >= UI_HEALTH_PING_INTERVAL_MS) {
            focus_ui_health_tick(now);
            last_health_ping = now;
        }
        if (now - last_status_write >= 1000u) {
            write_focus_hook_status();
            last_status_write = now;
        }
        if (now - last_activation_scan >= FOCUS_MODULE_SCAN_MS) {
            InterlockedExchange(&focus_module_scan_pending, 1);
            focus_queue_arbitration();
            last_activation_scan = now;
        }
        if (
            InterlockedCompareExchange(&focus_storm_pending, 0, 0) ||
            focus_modal_was_visible ||
            now - last_window_scan >= FOCUS_WINDOW_SCAN_MS
        ) {
            focus_queue_arbitration();
            last_window_scan = now;
        }
    }
    return 0;
}

static BOOL initialize_controller_focus_hook(void) {
    if (event_loop_tls_index == TLS_OUT_OF_INDEXES) {
        event_loop_tls_index = TlsAlloc();
    }
    if (event_loop_tls_index == TLS_OUT_OF_INDEXES) {
        return FALSE;
    }
    if (controller_restart_request_path[0]) {
        DeleteFileA(controller_restart_request_path);
    }
    focus_home_show_request_pending();
    patch_controller_focus_imports();
    patch_all_focus_activation_imports();
    focus_queue_arbitration();
    if (!focus_worker_event) {
        focus_worker_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    }
    if (
        focus_worker_event &&
        InterlockedCompareExchange(&focus_worker_running, 1, 0) == 0
    ) {
        focus_worker_thread = CreateThread(
            NULL, 0, focus_worker_main, NULL, 0, NULL
        );
        if (!focus_worker_thread) {
            InterlockedExchange(&focus_worker_running, 0);
        }
    }
    write_focus_hook_status();
    return (controller_focus_hook_status() & 927u) == 927u;
}

static BOOL patch_streamer_imports(void) {
    HMODULE module = GetModuleHandleW(L"streamer.dll");

    if (!module) {
        return FALSE;
    }
    if (module != patched_streamer_module) {
        patched_streamer_module = module;
        streamer_send_input_hooked = FALSE;
        streamer_cursor_info_hooked = FALSE;
        streamer_bit_blt_hooked = FALSE;
        streamer_stretch_blt_hooked = FALSE;
    }
    if (!streamer_send_input_hooked) {
        streamer_send_input_hooked = patch_import(
            module,
            "USER32.dll",
            "SendInput",
            (void *)hooked_send_input,
            (void **)&original_send_input
        );
    }
    if (!streamer_cursor_info_hooked) {
        streamer_cursor_info_hooked = patch_import(
            module,
            "USER32.dll",
            "GetCursorInfo",
            (void *)hooked_get_cursor_info,
            (void **)&original_get_cursor_info
        );
    }
    if (!streamer_bit_blt_hooked) {
        streamer_bit_blt_hooked = patch_import(
            module,
            "GDI32.dll",
            "BitBlt",
            (void *)hooked_bit_blt,
            (void **)&original_bit_blt
        );
    }
    if (!streamer_stretch_blt_hooked) {
        streamer_stretch_blt_hooked = patch_import(
            module,
            "GDI32.dll",
            "StretchBlt",
            (void *)hooked_stretch_blt,
            (void **)&original_stretch_blt
        );
    }
    return streamer_send_input_hooked &&
        streamer_cursor_info_hooked &&
        streamer_bit_blt_hooked &&
        streamer_stretch_blt_hooked;
}

static enum hook_process_kind identify_process(void) {
    WCHAR process_path[MAX_PATH];
    WCHAR *name;
    WCHAR *directory;
    if (!GetModuleFileNameW(NULL, process_path, MAX_PATH)) {
        return HOOK_PROCESS_NONE;
    }
    name = process_path + lstrlenW(process_path);
    while (name > process_path && name[-1] != L'\\' && name[-1] != L'/') {
        --name;
    }
    if (lstrcmpiW(name, L"GameViewerServer.exe") == 0) {
        return HOOK_PROCESS_SERVER;
    }
    if (lstrcmpiW(name, L"GameViewer.exe") == 0) {
        /*
         * The installation root contains a small launcher with the same file
         * name. Only the real Qt controller loads Qt5Core.dll; requiring it
         * prevents the focus hook from attaching to that outer launcher.
         */
        if (!GetModuleHandleW(L"Qt5Core.dll")) {
            return HOOK_PROCESS_NONE;
        }
        lstrcpynW(controller_root_path, process_path, MAX_PATH);
        directory = controller_root_path +
            (name - process_path > 0 ? (name - process_path - 1) : 0);
        *directory = L'\0';
        while (
            directory > controller_root_path &&
            directory[-1] != L'\\' && directory[-1] != L'/'
        ) {
            --directory;
        }
        if (lstrcmpiW(directory, L"bin") == 0 &&
            directory > controller_root_path) {
            directory[-1] = L'\0';
        }
        return HOOK_PROCESS_CONTROLLER;
    }
    return HOOK_PROCESS_NONE;
}

static void initialize_endpoint_path(void) {
    /*
     * The hook has two supported load locations: C:\ for the explicit
     * injector fallback and GameViewer\bin as an early dependency of the
     * native wevtapi compatibility DLL.  Keep all mutable bridge state in the
     * stable prefix root so both load paths share one authenticated endpoint
     * and one WOL configuration.
     */
    lstrcpynW(
        endpoint_path,
        L"C:\\uu-remote-input-bridge.endpoint",
        MAX_PATH
    );
    lstrcpynA(
        wol_config_path,
        "C:\\uu-remote-wol-bridge.ini",
        MAX_PATH
    );
    lstrcpynA(
        wol_status_path,
        "C:\\uu-remote-wol-hook-status.ini",
        MAX_PATH
    );
    lstrcpynW(
        frame_path,
        L"C:\\uu-remote-wayland-frame.bin",
        MAX_PATH
    );
    lstrcpynA(
        frame_status_path,
        "C:\\uu-remote-wayland-frame-status.ini",
        MAX_PATH
    );
    lstrcpynA(
        focus_status_path,
        "C:\\uu-remote-focus-hook-status.ini",
        MAX_PATH
    );
    lstrcpynA(
        focus_show_request_path,
        "C:\\uu-remote-home-show.request",
        MAX_PATH
    );
    lstrcpynA(
        controller_restart_request_path,
        "C:\\uu-remote-controller-restart.request",
        MAX_PATH
    );
}

__declspec(dllexport) DWORD WINAPI UURemoteInputHookVersion(void) {
    return HOOK_VERSION;
}

static DWORD wol_hook_status(void) {
    DWORD status = 0;

    if (adapters_addresses_hooked) {
        status |= 1u;
    }
    if (adapters_info_hooked) {
        status |= 2u;
    }
    if (if_table2_hooked) {
        status |= 4u;
    }
    if (wol_config_ready()) {
        status |= 8u;
    }
    return status;
}

static void write_wol_hook_status(void) {
    CHAR value[32];
    DWORD status;

    if (!wol_status_path[0]) {
        return;
    }
    status = wol_hook_status();
    wsprintfA(value, "%lu", (unsigned long)GetCurrentProcessId());
    WritePrivateProfileStringA("hook", "pid", value, wol_status_path);
    wsprintfA(value, "%lu", (unsigned long)HOOK_VERSION);
    WritePrivateProfileStringA("hook", "version", value, wol_status_path);
    wsprintfA(value, "%lu", (unsigned long)status);
    WritePrivateProfileStringA("hook", "status_bits", value, wol_status_path);
    WritePrivateProfileStringA(
        "hook",
        "preloaded",
        hook_preloaded ? "1" : "0",
        wol_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&adapters_addresses_calls, 0, 0)
    );
    WritePrivateProfileStringA(
        "calls", "addresses", value, wol_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&adapters_addresses_patched, 0, 0)
    );
    WritePrivateProfileStringA(
        "patched", "addresses", value, wol_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&adapters_info_calls, 0, 0)
    );
    WritePrivateProfileStringA("calls", "info", value, wol_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&adapters_info_patched, 0, 0)
    );
    WritePrivateProfileStringA("patched", "info", value, wol_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&if_table2_calls, 0, 0)
    );
    WritePrivateProfileStringA("calls", "if_table2", value, wol_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&if_table2_patched, 0, 0)
    );
    WritePrivateProfileStringA(
        "patched", "if_table2", value, wol_status_path
    );
}

__declspec(dllexport) DWORD WINAPI UURemoteWolHookStatus(void) {
    return wol_hook_status();
}

__declspec(dllexport) DWORD WINAPI UURemoteFrameHookStatus(void) {
    DWORD status = 0;
    if (bit_blt_hooked) {
        status |= 1u;
    }
    if (stretch_blt_hooked) {
        status |= 2u;
    }
    if (streamer_bit_blt_hooked) {
        status |= 4u;
    }
    if (streamer_stretch_blt_hooked) {
        status |= 8u;
    }
    if (frame_view) {
        status |= 16u;
    }
    return status;
}

__declspec(dllexport) DWORD WINAPI UURemoteFocusHookStatus(void) {
    return controller_focus_hook_status();
}

__declspec(dllexport) DWORD WINAPI UURemoteEventLoopGuardSelfTest(void) {
    struct event_loop_thread_state state;
    unsigned int index;

    ZeroMemory(&state, sizeof(state));
    for (index = 0; index + 1u < EVENT_LOOP_FALSE_WAKE_THRESHOLD; ++index) {
        state.last_peek_empty = TRUE;
        if (event_loop_false_wake_should_break(
                &state, WAIT_OBJECT_0, 0, 10u + index
            )) {
            return 0u;
        }
    }
    state.last_peek_empty = TRUE;
    if (!event_loop_false_wake_should_break(
            &state,
            WAIT_OBJECT_0,
            0,
            10u + EVENT_LOOP_FALSE_WAKE_THRESHOLD
        )) {
        return 0u;
    }
    if (
        state.last_peek_empty ||
        state.consecutive_false_wakes ||
        state.false_wake_burst_started
    ) {
        return 0u;
    }
    state.last_peek_empty = TRUE;
    if (event_loop_false_wake_should_break(
            &state, WAIT_TIMEOUT, 0, 200u
        )) {
        return 0u;
    }
    return state.last_peek_empty ? 0u : 1u;
}

__declspec(dllexport) DWORD WINAPI UURemoteUIHealthEvidenceSelfTest(void) {
    if (ui_health_has_livelock_evidence(3, 3, 10, 10)) {
        return 0u;
    }
    if (ui_health_has_livelock_evidence(3, 4, 10, 11)) {
        return 0u;
    }
    return ui_health_has_livelock_evidence(3, 4, 10, 10) ? 1u : 0u;
}

__declspec(dllexport) DWORD WINAPI UURemoteInputHookMarkPreloaded(
    LPVOID unused
) {
    (void)unused;
    hook_preloaded = TRUE;
    return wol_hook_status();
}

__declspec(dllexport) DWORD WINAPI UURemoteInputHookInitialize(LPVOID unused) {
    BOOL ready;

    if (process_kind == HOOK_PROCESS_CONTROLLER) {
        return initialize_controller_focus_hook() ? 1u : 0u;
    }
    if (process_kind != HOOK_PROCESS_SERVER) {
        return 0u;
    }
    if (unused) {
        hook_preloaded = TRUE;
    }
    patch_streamer_imports();
    ready = (
        send_input_hooked &&
        cursor_info_hooked &&
        cursor_pos_hooked &&
        refresh_endpoint()
    );
    write_wol_hook_status();
    write_frame_hook_status();
    return ready ? 1u : 0u;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    HMODULE main_module;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    hook_module = instance;
    DisableThreadLibraryCalls(instance);
    process_kind = identify_process();
    if (process_kind == HOOK_PROCESS_NONE) {
        return TRUE;
    }
    initialize_endpoint_path();
    if (process_kind == HOOK_PROCESS_CONTROLLER) {
        return TRUE;
    }
    main_module = GetModuleHandleW(NULL);
    send_input_hooked = patch_import(
        main_module,
        "USER32.dll",
        "SendInput",
        (void *)hooked_send_input,
        (void **)&original_send_input
    );
    cursor_info_hooked = patch_import(
        main_module,
        "USER32.dll",
        "GetCursorInfo",
        (void *)hooked_get_cursor_info,
        (void **)&original_get_cursor_info
    );
    cursor_pos_hooked = patch_import(
        main_module,
        "USER32.dll",
        "GetCursorPos",
        (void *)hooked_get_cursor_pos,
        (void **)&original_get_cursor_pos
    );
    bit_blt_hooked = patch_import(
        main_module,
        "GDI32.dll",
        "BitBlt",
        (void *)hooked_bit_blt,
        (void **)&original_bit_blt
    );
    stretch_blt_hooked = patch_import(
        main_module,
        "GDI32.dll",
        "StretchBlt",
        (void *)hooked_stretch_blt,
        (void **)&original_stretch_blt
    );
    adapters_addresses_hooked = patch_import(
        main_module,
        "IPHLPAPI.DLL",
        "GetAdaptersAddresses",
        (void *)hooked_get_adapters_addresses,
        (void **)&original_get_adapters_addresses
    );
    adapters_info_hooked = patch_import(
        main_module,
        "IPHLPAPI.DLL",
        "GetAdaptersInfo",
        (void *)hooked_get_adapters_info,
        (void **)&original_get_adapters_info
    );
    if_table2_hooked = patch_import(
        main_module,
        "IPHLPAPI.DLL",
        "GetIfTable2",
        (void *)hooked_get_if_table2,
        (void **)&original_get_if_table2
    );
    return TRUE;
}
