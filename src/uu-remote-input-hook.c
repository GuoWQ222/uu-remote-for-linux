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
#include <dxgi1_2.h>

#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UUIP_MAGIC 0x50495555u
#define UUIP_VERSION 1u
#define UUIP_HELLO 1u
#define UUIP_MOUSE 2u
#define UUIP_KEYBOARD 3u
#define HOOK_VERSION 48u
#define UUWF_MAGIC 0x46575555u
#define UUWF_VERSION 2u
#define UUWF_HEADER_SIZE 64u
#define UUWF_BUFFER_COUNT 2u
#define UUWF_PATH_CHECK_INTERVAL_MS 1000u
#define UUWF_MAX_DIMENSION 16384u
#define UUWF_MAX_PIXELS (32u * 1024u * 1024u)
#define UUWF_SNAPSHOT_ATTEMPTS 3u
#define UUCI_MAGIC 0x49435555u
#define UUCI_VERSION 1u
#define UUCI_HEADER_SIZE 64u
#define UUCI_MAX_DIMENSION 512u
#define UUCI_CHECK_INTERVAL_MS 8u
#define UUCI_CACHE_CAPACITY 64u
#define UUCI_CACHE_HARD_LIMIT 128u
#define UUCI_TRANSPARENT_SIZE 32u
#define EXECUTABLE_PATCH_SIZE 12u
#define EXECUTABLE_PATCH_MAX_THREADS 1024u
#define EXECUTABLE_PATCH_MAX_ATTEMPTS 32u
#define EXECUTABLE_PATCH_MAX_ROUTES 64u
#define FOCUS_UNHOOK_GRACE_MS 350u
#define FOCUS_INTERNAL_GRACE_MS 700u
#define FOCUS_STORM_WINDOW_MS 250u
#define FOCUS_STORM_THRESHOLD 12
#define FOCUS_LATCH_MS 1500u
#define FOCUS_INTERNAL_APPLY_GRACE_MS 300u
#define FOCUS_APPLY_COOLDOWN_MS 750u
#define FOCUS_WINDOW_SCAN_MS 250u
#define FOCUS_MODULE_SCAN_MS 1000u
#define FOCUS_SHOW_REQUEST_TTL_MS 30000u
#define EVENT_LOOP_FALSE_WAKE_THRESHOLD 32u
#define EVENT_LOOP_FALSE_WAKE_WINDOW_MS 100u
#define EVENT_LOOP_BREAK_BACKOFF_MS 1u
#define EVENT_LOOP_STICKY_NULL_THRESHOLD 32u
#define EVENT_LOOP_STICKY_NULL_WINDOW_MS 100u
#define EVENT_LOOP_STICKY_NULL_BACKOFF_MS 4u
#define UI_HEALTH_PING_INTERVAL_MS 1000u
#define UI_HEALTH_TIMEOUT_MS 10000u
#define UI_HEALTH_RESUME_GAP_MS 3000u
#define UI_HEALTH_HARD_STALL_TIMEOUTS 2
#define UI_HEALTH_RECOVERY_RETRY_MS 5000u
#define HOME_REPAINT_DELAY_MS 750u
#define HOME_REPAINT_RESTORE_DELAY_MS 80u
#define HOME_REPAINT_TIMER_ID ((UINT_PTR)0x55485250u)
#define FOCUS_MAX_WINDOWS 64u
#define FOCUS_MAX_MODULES 256u
#define WM_UU_FOCUS_APPLY (WM_APP + 0x4b1u)
#define WM_UU_UI_HEALTH (WM_APP + 0x4b3u)
#define WM_UU_FOCUS_ARBITRATE (WM_APP + 0x4b4u)
#define WM_UU_HOME_REPAINT (WM_APP + 0x4b5u)
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
typedef SHORT(WINAPI *get_key_state_fn)(int);
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
typedef HRESULT(WINAPI *create_dxgi_factory1_fn)(REFIID, void **);
typedef HRESULT(STDMETHODCALLTYPE *dxgi_enum_adapters_fn)(
    IDXGIFactory1 *, UINT, IDXGIAdapter **
);
typedef HRESULT(STDMETHODCALLTYPE *dxgi_enum_adapters1_fn)(
    IDXGIFactory1 *, UINT, IDXGIAdapter1 **
);
typedef HRESULT(STDMETHODCALLTYPE *dxgi_enum_outputs_fn)(
    IDXGIAdapter *, UINT, IDXGIOutput **
);
typedef HRESULT(STDMETHODCALLTYPE *dxgi_duplicate_output_fn)(
    IDXGIOutput1 *, IUnknown *, IDXGIOutputDuplication **
);
typedef HRESULT(STDMETHODCALLTYPE *dxgi_acquire_next_frame_fn)(
    IDXGIOutputDuplication *, UINT, DXGI_OUTDUPL_FRAME_INFO *,
    IDXGIResource **
);
typedef HRESULT(STDMETHODCALLTYPE *dxgi_get_frame_pointer_shape_fn)(
    IDXGIOutputDuplication *, UINT, void *, UINT *,
    DXGI_OUTDUPL_POINTER_SHAPE_INFO *
);

static send_input_fn original_send_input;
static get_key_state_fn original_get_key_state;
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
static create_dxgi_factory1_fn original_create_dxgi_factory1;
static dxgi_enum_adapters_fn original_dxgi_enum_adapters;
static dxgi_enum_adapters1_fn original_dxgi_enum_adapters1;
static dxgi_enum_outputs_fn original_dxgi_enum_outputs;
static dxgi_duplicate_output_fn original_dxgi_duplicate_output;
static dxgi_acquire_next_frame_fn original_dxgi_acquire_next_frame;
static dxgi_get_frame_pointer_shape_fn
    original_dxgi_get_frame_pointer_shape;
static HMODULE hook_module;
static WCHAR endpoint_path[MAX_PATH];
static CHAR wol_config_path[MAX_PATH];
static CHAR wol_status_path[MAX_PATH];
static WCHAR frame_path[MAX_PATH];
static WCHAR native_cursor_path[MAX_PATH];
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
static volatile LONG native_lock_keys;
static volatile LONG lock_state_valid;
static volatile LONG native_lock_mask;
static volatile LONG cursor_feedback_origin_x;
static volatile LONG cursor_feedback_origin_y;
static volatile LONG cursor_feedback_width;
static volatile LONG cursor_feedback_height;
static BOOL send_input_hooked;
static BOOL get_key_state_hooked;
static BOOL cursor_info_hooked;
static BOOL cursor_pos_hooked;
static BOOL bit_blt_hooked;
static BOOL stretch_blt_hooked;
static SRWLOCK streamer_patch_lock = SRWLOCK_INIT;
static HMODULE patched_streamer_module;
static BOOL streamer_send_input_hooked;
static BOOL streamer_cursor_info_hooked;
static BOOL streamer_bit_blt_hooked;
static BOOL streamer_stretch_blt_hooked;
static BOOL streamer_dxgi_factory_hooked;
static BOOL dxgi_factory_vtable_hooked;
static BOOL dxgi_adapter_vtable_hooked;
static BOOL dxgi_output_vtable_hooked;
static BOOL dxgi_duplication_vtable_hooked;
static volatile LONG dxgi_factories_created;
static volatile LONG dxgi_adapters_enumerated;
static volatile LONG dxgi_outputs_enumerated;
static volatile LONG dxgi_duplications_created;
static volatile LONG dxgi_acquire_calls;
static volatile LONG dxgi_cursor_announcements;
static volatile LONG dxgi_cursor_shape_requests;
static volatile LONG dxgi_cursor_shape_updates;
static volatile LONG dxgi_cursor_shape_fallbacks;
static volatile LONG dxgi_cursor_delivered_sequence;
static BOOL streamer_frame_adapter_hooked;
static DWORD configured_frame_adapter_luid;
static void *original_frame_adapter_id;
static volatile LONG frame_adapter_hook_calls;
static volatile LONG frame_adapter_wrapper_hook_calls;
static volatile LONG frame_adapter_slots_patched;
static volatile LONG frame_adapter_long_candidates;
static volatile LONG frame_adapter_wrapper_candidates;
static volatile LONG frame_adapter_rtti_named_candidates;
static volatile LONG frame_adapter_encoder_path_candidates;
static BOOL streamer_wrapper_frame_adapter_hooked;
static BOOL streamer_portrait_scale_limit_hooked;
static volatile LONG frame_portrait_scale_limit_candidates;
static volatile LONG frame_portrait_scale_limit_patches;
static BOOL streamer_portrait_capture_limit_hooked;
static volatile LONG frame_portrait_capture_limit_candidates;
static volatile LONG frame_portrait_capture_limit_patches;
static BOOL streamer_portrait_update_limit_hooked;
static volatile LONG frame_portrait_update_limit_candidates;
static volatile LONG frame_portrait_update_limit_patches;
static BOOL streamer_portrait_scaler_hooked;
static volatile LONG frame_portrait_scaler_candidates;
static volatile LONG frame_portrait_scaler_patches;
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
static BOOL tracked_cursor_virtual_desktop;
static SRWLOCK native_cursor_lock = SRWLOCK_INIT;
static HCURSOR native_cursor_handle;
static INIT_ONCE transparent_cursor_once = INIT_ONCE_STATIC_INIT;
static HCURSOR transparent_cursor_handle;
static uint64_t native_cursor_hash;
static ULONGLONG native_cursor_checked_at;
static volatile LONG native_cursor_active;
static volatile LONG native_cursor_calls;
static volatile LONG native_cursor_updates;
static volatile LONG native_cursor_fallbacks;
static volatile LONG native_cursor_transparent;
static volatile LONG native_cursor_sequence;
static volatile LONG native_cursor_width;
static volatile LONG native_cursor_height;
static volatile LONG native_cursor_hotspot_x;
static volatile LONG native_cursor_hotspot_y;
static SRWLOCK frame_lock = SRWLOCK_INIT;
static HANDLE frame_file = INVALID_HANDLE_VALUE;
static HANDLE frame_mapping;
static unsigned char *frame_view;
static SIZE_T frame_view_size;
static unsigned char *frame_snapshot;
static SIZE_T frame_snapshot_capacity;
static volatile LONG frame_snapshot_retries;
static volatile LONG frame_snapshot_failures;
static uint32_t frame_last_sequence;
static ULONGLONG frame_path_checked_at;
static volatile LONG frame_hook_calls;
static volatile LONG frame_hook_rendered;
static volatile LONG frame_hook_fallbacks;
static volatile LONG frame_destination_x;
static volatile LONG frame_destination_y;
static volatile LONG frame_destination_width;
static volatile LONG frame_destination_height;
static volatile LONG frame_destination_bitmap_width;
static volatile LONG frame_destination_bitmap_height;
static volatile LONG frame_destination_bitmap_stride;
static volatile LONG frame_destination_checksum;
static volatile LONG frame_destination_checksum_changes;
static volatile LONG frame_source_x;
static volatile LONG frame_source_y;
static volatile LONG frame_dib_source_y;
static volatile LONG frame_source_width;
static volatile LONG frame_source_height;
static volatile LONG frame_monitor_left;
static volatile LONG frame_monitor_top;
static volatile LONG frame_monitor_width;
static volatile LONG frame_monitor_height;
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
static volatile LONG focus_home_repaint_pending;
static volatile LONG focus_home_repaint_pulses;
static PVOID volatile focus_home_repaint_window;
static volatile LONG focus_home_repaint_width;
static volatile LONG focus_home_repaint_height;
static volatile LONG focus_worker_heartbeats;
static volatile LONG event_loop_empty_queue_wakes;
static volatile LONG event_loop_guard_breaks;
static volatile LONG64 event_loop_messages_dequeued;
static volatile LONG event_loop_posted_forwarded;
static volatile LONG event_loop_posted_coalesced;
static volatile LONG event_loop_sticky_nulls_detected;
static volatile LONG event_loop_sticky_nulls_filtered;
static volatile LONG event_loop_sticky_null_wake_breaks;
static volatile LONG64 event_loop_last_guard_break;
static volatile LONG64 event_loop_last_sticky_null;
static volatile LONG ui_health_pings_sent;
static volatile LONG ui_health_pings_acked;
static volatile LONG ui_health_timeouts;
static volatile LONG ui_health_recovery_requests;
static volatile LONG ui_health_window_invalidations;
static volatile LONG ui_health_no_livelock_suppressions;
static volatile LONG ui_health_consecutive_timeouts;
static volatile LONG ui_health_hard_stalls_detected;
static volatile LONG64 ui_health_ping_sent_at;
static volatile LONG64 ui_health_last_worker_tick;
static volatile LONG64 ui_health_last_ack_tick;
static volatile LONG64 ui_health_recovery_requested_at;
static volatile LONG ui_health_recovery_requested;
static volatile LONG ui_health_ping_generation;
static volatile LONG ui_health_ping_guard_breaks;
static volatile LONG ui_health_ping_sticky_nulls;
static PVOID volatile ui_health_ping_window;
static volatile LONG ui_health_stall_generation;
static PVOID volatile ui_health_stall_window;
static volatile LONG focus_arbitration_pending;
static volatile LONG focus_arbitration_posts;
static volatile LONG focus_arbitration_coalesced;
static volatile LONG focus_window_state_changes;
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
static volatile LONG64 focus_last_window_signature;
static DWORD event_loop_tls_index = TLS_OUT_OF_INDEXES;

struct event_loop_message_fingerprint {
    HWND window;
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
    DWORD time;
    POINT point;
};

struct event_loop_thread_state {
    BOOL last_peek_empty;
    DWORD consecutive_false_wakes;
    ULONGLONG false_wake_burst_started;
    struct event_loop_message_fingerprint last_removed_message;
    DWORD repeated_removed_messages;
    ULONGLONG repeated_message_burst_started;
    BOOL sticky_null_active;
    struct event_loop_message_fingerprint sticky_null_message;
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
static void write_frame_hook_status(void);
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
    int32_t origin_x;
    int32_t origin_y;
    unsigned char reserved[16];
};

struct native_cursor_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t sequence;
    uint32_t width;
    uint32_t height;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t pixel_size;
    unsigned char reserved[28];
};
#pragma pack(pop)

typedef char wayland_frame_header_must_be_64_bytes[
    sizeof(struct wayland_frame_header) == UUWF_HEADER_SIZE ? 1 : -1
];
typedef char native_cursor_header_must_be_64_bytes[
    sizeof(struct native_cursor_header) == UUCI_HEADER_SIZE ? 1 : -1
];

struct native_cursor_cache_entry {
    uint64_t hash;
    uint32_t width;
    uint32_t height;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    HCURSOR cursor;
    struct native_cursor_cache_entry *next;
};

static struct native_cursor_cache_entry
    native_cursor_cache[UUCI_CACHE_CAPACITY];
static struct native_cursor_cache_entry *native_cursor_cache_overflow;
static volatile LONG native_cursor_cache_entries;
static volatile LONG native_cursor_cache_overflows;
static volatile LONG native_cursor_cache_drops;

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
    tracked_cursor_virtual_desktop = FALSE;
    ReleaseSRWLockExclusive(&cursor_lock);
}

static LONG read_profile_long(
    LPCWSTR section,
    LPCWSTR key,
    LONG fallback,
    LPCWSTR path
) {
    WCHAR text[64];
    WCHAR *end;
    long value;

    text[0] = L'\0';
    GetPrivateProfileStringW(
        section,
        key,
        L"",
        text,
        (DWORD)(sizeof(text) / sizeof(text[0])),
        path
    );
    if (!text[0]) {
        return fallback;
    }
    end = NULL;
    value = wcstol(text, &end, 10);
    if (
        end == text ||
        (end && *end != L'\0') ||
        value < LONG_MIN ||
        value > LONG_MAX
    ) {
        return fallback;
    }
    return (LONG)value;
}

static BOOL refresh_endpoint(void) {
    ULONGLONG now = GetTickCount64();
    WCHAR token_text[64];
    unsigned char new_token[16];
    UINT port;
    UINT new_cursor_width;
    UINT new_cursor_height;
    LONG new_cursor_origin_x;
    LONG new_cursor_origin_y;
    LONG new_native_lock_keys;
    LONG new_lock_state_valid;
    LONG new_lock_mask;
    LONG old_cursor_origin_x;
    LONG old_cursor_origin_y;
    LONG old_cursor_width;
    LONG old_cursor_height;
    LONG old_native_lock_keys;
    LONG old_lock_state_valid;
    LONG old_lock_mask;
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
    new_native_lock_keys = GetPrivateProfileIntW(
        L"bridge", L"native_lock_keys", 0, endpoint_path
    ) != 0;
    new_lock_state_valid = GetPrivateProfileIntW(
        L"bridge", L"lock_state_valid", 0, endpoint_path
    ) != 0;
    new_lock_mask = GetPrivateProfileIntW(
        L"bridge", L"lock_mask", 0, endpoint_path
    ) & 0x7;
    new_cursor_origin_x = read_profile_long(
        L"bridge", L"cursor_origin_x", 0, endpoint_path
    );
    new_cursor_origin_y = read_profile_long(
        L"bridge", L"cursor_origin_y", 0, endpoint_path
    );
    new_cursor_width = GetPrivateProfileIntW(
        L"bridge", L"cursor_width", 0, endpoint_path
    );
    new_cursor_height = GetPrivateProfileIntW(
        L"bridge", L"cursor_height", 0, endpoint_path
    );
    if (
        new_cursor_width == 0 ||
        new_cursor_width > 65535 ||
        new_cursor_height == 0 ||
        new_cursor_height > 65535
    ) {
        new_cursor_width = 0;
        new_cursor_height = 0;
    }
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

    old_cursor_width = InterlockedCompareExchange(
        &cursor_feedback_width, 0, 0
    );
    old_cursor_height = InterlockedCompareExchange(
        &cursor_feedback_height, 0, 0
    );
    old_cursor_origin_x = InterlockedCompareExchange(
        &cursor_feedback_origin_x, 0, 0
    );
    old_cursor_origin_y = InterlockedCompareExchange(
        &cursor_feedback_origin_y, 0, 0
    );
    old_native_lock_keys = InterlockedCompareExchange(
        &native_lock_keys, 0, 0
    );
    old_lock_state_valid = InterlockedCompareExchange(
        &lock_state_valid, 0, 0
    );
    old_lock_mask = InterlockedCompareExchange(
        &native_lock_mask, 0, 0
    );
    changed = !endpoint_valid ||
        bridge_address.sin_port != htons((u_short)port) ||
        memcmp(bridge_token, new_token, sizeof(bridge_token)) != 0 ||
        old_cursor_origin_x != new_cursor_origin_x ||
        old_cursor_origin_y != new_cursor_origin_y ||
        old_cursor_width != (LONG)new_cursor_width ||
        old_cursor_height != (LONG)new_cursor_height ||
        old_native_lock_keys != new_native_lock_keys ||
        old_lock_state_valid != new_lock_state_valid ||
        old_lock_mask != new_lock_mask;
    ZeroMemory(&bridge_address, sizeof(bridge_address));
    bridge_address.sin_family = AF_INET;
    bridge_address.sin_port = htons((u_short)port);
    bridge_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CopyMemory(bridge_token, new_token, sizeof(bridge_token));
    InterlockedExchange(&cursor_feedback_origin_x, new_cursor_origin_x);
    InterlockedExchange(&cursor_feedback_origin_y, new_cursor_origin_y);
    InterlockedExchange(
        &cursor_feedback_width, (LONG)new_cursor_width
    );
    InterlockedExchange(
        &cursor_feedback_height, (LONG)new_cursor_height
    );
    InterlockedExchange(&native_lock_keys, new_native_lock_keys);
    InterlockedExchange(&lock_state_valid, new_lock_state_valid);
    InterlockedExchange(&native_lock_mask, new_lock_mask);
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
    BOOL virtual_desktop;
    LONG origin_x;
    LONG origin_y;
    LONG width;
    LONG height;

    if (!(input->dwFlags & MOUSEEVENTF_MOVE)) {
        return;
    }
    virtual_desktop = !!(input->dwFlags & MOUSEEVENTF_VIRTUALDESK);
    if (virtual_desktop) {
        /*
         * MOUSEEVENTF_VIRTUALDESK is semantic, not decorative: UU uses the
         * normalized value for the complete multi-monitor desktop.  Keep the
         * feedback in Wine's primary-relative virtual coordinate space.
         */
        origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {
        width = InterlockedCompareExchange(&cursor_feedback_width, 0, 0);
        height = InterlockedCompareExchange(&cursor_feedback_height, 0, 0);
        if (width > 0 && height > 0) {
            origin_x = 0;
            origin_y = 0;
        } else {
            origin_x = 0;
            origin_y = 0;
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
        }
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
        tracked_cursor_virtual_desktop = virtual_desktop;
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
        tracked_cursor_virtual_desktop = virtual_desktop;
    }
    ReleaseSRWLockExclusive(&cursor_lock);
}

static BOOL read_tracked_cursor(LPPOINT point, BOOL *virtual_desktop) {
    BOOL ready;
    AcquireSRWLockShared(&cursor_lock);
    ready = tracked_cursor_valid;
    if (ready) {
        *point = tracked_cursor_position;
        if (virtual_desktop) {
            *virtual_desktop = tracked_cursor_virtual_desktop;
        }
    }
    ReleaseSRWLockShared(&cursor_lock);
    return ready;
}

static BOOL actual_cursor_to_screen_local(
    const POINT *actual,
    LPPOINT local
) {
    LONG origin_x = InterlockedCompareExchange(
        &cursor_feedback_origin_x, 0, 0
    );
    LONG origin_y = InterlockedCompareExchange(
        &cursor_feedback_origin_y, 0, 0
    );
    LONG width = InterlockedCompareExchange(
        &cursor_feedback_width, 0, 0
    );
    LONG height = InterlockedCompareExchange(
        &cursor_feedback_height, 0, 0
    );
    LONGLONG right;
    LONGLONG bottom;

    if (!actual || !local || width <= 0 || height <= 0) {
        return FALSE;
    }
    right = (LONGLONG)origin_x + width;
    bottom = (LONGLONG)origin_y + height;
    if (
        actual->x < origin_x ||
        (LONGLONG)actual->x >= right ||
        actual->y < origin_y ||
        (LONGLONG)actual->y >= bottom
    ) {
        return FALSE;
    }
    local->x = actual->x - origin_x;
    local->y = actual->y - origin_y;
    return TRUE;
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

static LONG lock_key_bit(const KEYBDINPUT *input) {
    WORD virtual_key;
    WORD scan_code;

    if (!input || (input->dwFlags & KEYEVENTF_UNICODE)) {
        return 0;
    }
    virtual_key = input->wVk;
    if (virtual_key == VK_CAPITAL) {
        return 0x1;
    }
    if (virtual_key == VK_NUMLOCK) {
        return 0x2;
    }
    if (virtual_key == VK_SCROLL) {
        return 0x4;
    }
    if (!(input->dwFlags & KEYEVENTF_SCANCODE)) {
        return 0;
    }
    scan_code = input->wScan & 0xffu;
    if (scan_code == 0x3au) {
        return 0x1;
    }
    if (scan_code == 0x45u) {
        return 0x2;
    }
    return scan_code == 0x46u ? 0x4 : 0;
}

static void toggle_native_lock_state(LONG bit) {
    LONG previous;

    if (!bit || !InterlockedCompareExchange(&lock_state_valid, 0, 0)) {
        return;
    }
    do {
        previous = InterlockedCompareExchange(&native_lock_mask, 0, 0);
    } while (
        InterlockedCompareExchange(
            &native_lock_mask, previous ^ bit, previous
        ) != previous
    );
}

static SHORT WINAPI hooked_get_key_state(int virtual_key) {
    SHORT result = original_get_key_state
        ? original_get_key_state(virtual_key)
        : 0;
    LONG bit;

    if (virtual_key == VK_CAPITAL) {
        bit = 0x1;
    } else if (virtual_key == VK_NUMLOCK) {
        bit = 0x2;
    } else if (virtual_key == VK_SCROLL) {
        bit = 0x4;
    } else {
        return result;
    }
    if (
        !refresh_endpoint() ||
        !InterlockedCompareExchange(&native_lock_keys, 0, 0) ||
        !InterlockedCompareExchange(&lock_state_valid, 0, 0)
    ) {
        return result;
    }
    return (SHORT)(
        (result & (SHORT)~1) |
        ((InterlockedCompareExchange(&native_lock_mask, 0, 0) & bit) != 0)
    );
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
            LONG lock_bit = lock_key_bit(&inputs[index].ki);
            if (!forward_keyboard(&inputs[index].ki)) {
                return original_send_input
                    ? original_send_input(count, inputs, input_size)
                    : index;
            }
            if (lock_bit && !(inputs[index].ki.dwFlags & KEYEVENTF_KEYUP)) {
                toggle_native_lock_state(lock_bit);
            }
        } else {
            return original_send_input
                ? original_send_input(count, inputs, input_size)
                : index;
        }
    }
    return count;
}

static BOOL read_native_cursor_file(
    struct native_cursor_header *header,
    unsigned char **pixels_out
) {
    HANDLE file;
    LARGE_INTEGER size;
    DWORD received;
    unsigned char *pixels = NULL;
    BOOL result = FALSE;

    if (!header || !pixels_out || !native_cursor_path[0]) {
        return FALSE;
    }
    *pixels_out = NULL;
    file = CreateFileW(
        native_cursor_path,
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
        size.QuadPart < (LONGLONG)sizeof(*header) ||
        size.QuadPart >
            (LONGLONG)sizeof(*header) +
                (LONGLONG)UUCI_MAX_DIMENSION * UUCI_MAX_DIMENSION * 4
    ) {
        goto cleanup;
    }
    ZeroMemory(header, sizeof(*header));
    if (
        !ReadFile(file, header, sizeof(*header), &received, NULL) ||
        received != sizeof(*header) ||
        header->magic != UUCI_MAGIC ||
        header->version != UUCI_VERSION ||
        header->header_size != sizeof(*header) ||
        header->width == 0 ||
        header->width > UUCI_MAX_DIMENSION ||
        header->height == 0 ||
        header->height > UUCI_MAX_DIMENSION ||
        header->hotspot_x >= header->width ||
        header->hotspot_y >= header->height ||
        header->pixel_size != header->width * header->height * 4u ||
        size.QuadPart !=
            (LONGLONG)sizeof(*header) + header->pixel_size
    ) {
        goto cleanup;
    }
    pixels = HeapAlloc(GetProcessHeap(), 0, header->pixel_size);
    if (
        !pixels ||
        !ReadFile(file, pixels, header->pixel_size, &received, NULL) ||
        received != header->pixel_size
    ) {
        goto cleanup;
    }
    *pixels_out = pixels;
    pixels = NULL;
    result = TRUE;

cleanup:
    if (pixels) {
        HeapFree(GetProcessHeap(), 0, pixels);
    }
    CloseHandle(file);
    return result;
}

static uint64_t hash_native_cursor(
    const struct native_cursor_header *header,
    const unsigned char *pixels
) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t metadata[4];
    SIZE_T index;

    metadata[0] = header->width;
    metadata[1] = header->height;
    metadata[2] = header->hotspot_x;
    metadata[3] = header->hotspot_y;
    for (index = 0; index < sizeof(metadata); ++index) {
        hash ^= ((const unsigned char *)metadata)[index];
        hash *= UINT64_C(1099511628211);
    }
    for (index = 0; index < header->pixel_size; ++index) {
        hash ^= pixels[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static HCURSOR create_native_cursor(
    const struct native_cursor_header *header,
    const unsigned char *pixels
) {
    BITMAPINFO bitmap_info;
    ICONINFO icon_info;
    HDC screen;
    HBITMAP color = NULL;
    HBITMAP mask = NULL;
    unsigned char *mask_bits = NULL;
    void *bitmap_pixels = NULL;
    SIZE_T mask_stride;
    SIZE_T mask_size;
    HCURSOR cursor = NULL;

    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = (LONG)header->width;
    bitmap_info.bmiHeader.biHeight = -(LONG)header->height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    screen = GetDC(NULL);
    if (!screen) {
        return NULL;
    }
    color = CreateDIBSection(
        screen,
        &bitmap_info,
        DIB_RGB_COLORS,
        &bitmap_pixels,
        NULL,
        0
    );
    ReleaseDC(NULL, screen);
    if (!color || !bitmap_pixels) {
        goto cleanup;
    }
    CopyMemory(bitmap_pixels, pixels, header->pixel_size);

    mask_stride = ((header->width + 15u) / 16u) * 2u;
    mask_size = mask_stride * header->height;
    mask_bits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, mask_size);
    if (!mask_bits) {
        goto cleanup;
    }
    mask = CreateBitmap(
        (int)header->width,
        (int)header->height,
        1,
        1,
        mask_bits
    );
    if (!mask) {
        goto cleanup;
    }

    ZeroMemory(&icon_info, sizeof(icon_info));
    icon_info.fIcon = FALSE;
    icon_info.xHotspot = header->hotspot_x;
    icon_info.yHotspot = header->hotspot_y;
    icon_info.hbmMask = mask;
    icon_info.hbmColor = color;
    cursor = (HCURSOR)CreateIconIndirect(&icon_info);

cleanup:
    if (mask) {
        DeleteObject(mask);
    }
    if (color) {
        DeleteObject(color);
    }
    if (mask_bits) {
        HeapFree(GetProcessHeap(), 0, mask_bits);
    }
    return cursor;
}

static HCURSOR cached_native_cursor(
    const struct native_cursor_header *header,
    const unsigned char *pixels,
    uint64_t hash,
    BOOL *capacity_exhausted
) {
    SIZE_T index;
    SIZE_T target = UUCI_CACHE_CAPACITY;
    struct native_cursor_cache_entry *entry;
    struct native_cursor_cache_entry *allocated = NULL;
    HCURSOR cursor;

    *capacity_exhausted = FALSE;
    for (index = 0; index < UUCI_CACHE_CAPACITY; ++index) {
        entry = &native_cursor_cache[index];
        if (
            entry->cursor &&
            entry->hash == hash &&
            entry->width == header->width &&
            entry->height == header->height &&
            entry->hotspot_x == header->hotspot_x &&
            entry->hotspot_y == header->hotspot_y
        ) {
            return entry->cursor;
        }
        if (!entry->cursor) {
            target = index;
            break;
        }
    }
    if (target == UUCI_CACHE_CAPACITY) {
        /*
         * GetCursorInfo returns a borrowed handle with no release callback,
         * so destroying an entry that GameViewerServer may still retain is
         * unsafe.  Keep a bounded process-lifetime overflow list: this still
         * survives normal reconnect churn without reusing invalid handles,
         * while malformed or animated metadata cannot exhaust GDI objects
         * and heap memory indefinitely.
         */
        for (
            entry = native_cursor_cache_overflow;
            entry;
            entry = entry->next
        ) {
            if (
                entry->cursor &&
                entry->hash == hash &&
                entry->width == header->width &&
                entry->height == header->height &&
                entry->hotspot_x == header->hotspot_x &&
                entry->hotspot_y == header->hotspot_y
            ) {
                return entry->cursor;
            }
        }
        if (
            InterlockedCompareExchange(
                &native_cursor_cache_entries, 0, 0
            ) >= (LONG)UUCI_CACHE_HARD_LIMIT
        ) {
            InterlockedIncrement(&native_cursor_cache_drops);
            *capacity_exhausted = TRUE;
            return NULL;
        }
        allocated = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*allocated)
        );
        if (!allocated) {
            return NULL;
        }
        entry = allocated;
    } else {
        entry = &native_cursor_cache[target];
    }

    cursor = create_native_cursor(header, pixels);
    if (!cursor) {
        if (allocated) {
            HeapFree(GetProcessHeap(), 0, allocated);
        }
        return NULL;
    }
    entry->hash = hash;
    entry->width = header->width;
    entry->height = header->height;
    entry->hotspot_x = header->hotspot_x;
    entry->hotspot_y = header->hotspot_y;
    entry->cursor = cursor;
    if (allocated) {
        entry->next = native_cursor_cache_overflow;
        native_cursor_cache_overflow = entry;
        InterlockedIncrement(&native_cursor_cache_overflows);
    }
    InterlockedIncrement(&native_cursor_cache_entries);
    return cursor;
}

static HCURSOR refresh_native_cursor(void) {
    struct native_cursor_header header;
    unsigned char *pixels = NULL;
    HCURSOR cursor = NULL;
    uint64_t hash;
    ULONGLONG now = GetTickCount64();
    BOOL capacity_exhausted = FALSE;
    BOOL had_active;
    BOOL status_changed = FALSE;
    BOOL updated = FALSE;

    AcquireSRWLockExclusive(&native_cursor_lock);
    if (
        now - native_cursor_checked_at < UUCI_CHECK_INTERVAL_MS &&
        InterlockedCompareExchange(&native_cursor_active, 0, 0)
    ) {
        cursor = native_cursor_handle;
        ReleaseSRWLockExclusive(&native_cursor_lock);
        return cursor;
    }
    native_cursor_checked_at = now;
    if (!read_native_cursor_file(&header, &pixels)) {
        InterlockedExchange(&native_cursor_active, 0);
        ReleaseSRWLockExclusive(&native_cursor_lock);
        return NULL;
    }
    hash = hash_native_cursor(&header, pixels);
    had_active = InterlockedCompareExchange(&native_cursor_active, 0, 0) != 0;
    cursor = cached_native_cursor(
        &header, pixels, hash, &capacity_exhausted
    );
    if (!cursor) {
        if (capacity_exhausted && had_active && native_cursor_handle) {
            cursor = native_cursor_handle;
            status_changed = TRUE;
        }
        goto cleanup;
    }
    updated = !InterlockedCompareExchange(&native_cursor_active, 0, 0) ||
        native_cursor_hash != hash ||
        native_cursor_handle != cursor;
    native_cursor_handle = cursor;
    native_cursor_hash = hash;
    InterlockedExchange(&native_cursor_active, 1);
    InterlockedExchange(&native_cursor_sequence, (LONG)header.sequence);
    InterlockedExchange(&native_cursor_width, (LONG)header.width);
    InterlockedExchange(&native_cursor_height, (LONG)header.height);
    InterlockedExchange(&native_cursor_hotspot_x, (LONG)header.hotspot_x);
    InterlockedExchange(&native_cursor_hotspot_y, (LONG)header.hotspot_y);
    if (updated) {
        InterlockedIncrement(&native_cursor_updates);
    }

cleanup:
    if (pixels) {
        HeapFree(GetProcessHeap(), 0, pixels);
    }
    if (!cursor) {
        InterlockedExchange(&native_cursor_active, 0);
    }
    ReleaseSRWLockExclusive(&native_cursor_lock);
    if (updated || status_changed) {
        write_frame_hook_status();
    }
    return cursor;
}

static HRESULT fill_native_dxgi_cursor_shape(
    UINT buffer_size,
    void *buffer,
    UINT *required_size,
    DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape_info,
    uint32_t *sequence_out
) {
    struct native_cursor_header header;
    unsigned char *pixels = NULL;

    if (!required_size || !shape_info) {
        return E_INVALIDARG;
    }
    if (!read_native_cursor_file(&header, &pixels)) {
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    *required_size = header.pixel_size;
    ZeroMemory(shape_info, sizeof(*shape_info));
    shape_info->Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;
    shape_info->Width = header.width;
    shape_info->Height = header.height;
    shape_info->Pitch = header.width * 4u;
    shape_info->HotSpot.x = (LONG)header.hotspot_x;
    shape_info->HotSpot.y = (LONG)header.hotspot_y;
    if (!buffer || buffer_size < header.pixel_size) {
        HeapFree(GetProcessHeap(), 0, pixels);
        return DXGI_ERROR_MORE_DATA;
    }
    CopyMemory(buffer, pixels, header.pixel_size);
    HeapFree(GetProcessHeap(), 0, pixels);
    if (sequence_out) {
        *sequence_out = header.sequence;
    }
    return S_OK;
}

static BOOL CALLBACK initialize_transparent_cursor(
    PINIT_ONCE once,
    PVOID parameter,
    PVOID *context
) {
    BYTE and_mask[
        UUCI_TRANSPARENT_SIZE * UUCI_TRANSPARENT_SIZE / 8u
    ];
    BYTE xor_mask[
        UUCI_TRANSPARENT_SIZE * UUCI_TRANSPARENT_SIZE / 8u
    ];

    (void)once;
    (void)parameter;
    (void)context;
    /* AND=1 and XOR=0 leaves every destination pixel unchanged. */
    FillMemory(and_mask, sizeof(and_mask), 0xff);
    ZeroMemory(xor_mask, sizeof(xor_mask));
    transparent_cursor_handle = CreateCursor(
        hook_module,
        0,
        0,
        UUCI_TRANSPARENT_SIZE,
        UUCI_TRANSPARENT_SIZE,
        and_mask,
        xor_mask
    );
    return TRUE;
}

static HCURSOR embedded_transparent_cursor(void) {
    InitOnceExecuteOnce(
        &transparent_cursor_once,
        initialize_transparent_cursor,
        NULL,
        NULL
    );
    return transparent_cursor_handle;
}

static BOOL WINAPI hooked_get_cursor_info(PCURSORINFO cursor_info) {
    HCURSOR stable_cursor;
    POINT actual;
    POINT local;
    POINT tracked;
    BOOL tracked_virtual_desktop = FALSE;
    BOOL tracked_ready;
    InterlockedIncrement(&native_cursor_calls);
    BOOL result = original_get_cursor_info
        ? original_get_cursor_info(cursor_info)
        : FALSE;
    if (
        cursor_info &&
        cursor_info->cbSize >= sizeof(*cursor_info) &&
        refresh_endpoint()
    ) {
        if (!force_cursor_visible) {
            /*
             * Wayland Portal embeds the compositor cursor in the video.
             * UU's Windows controller retains its local arrow when the server
             * merely reports CURSOR_SUPPRESSED.  Publish a real transparent
             * cursor instead, causing the controller to replace that arrow
             * while the embedded compositor cursor remains authoritative.
             */
            stable_cursor = embedded_transparent_cursor();
            if (stable_cursor) {
                cursor_info->flags = CURSOR_SHOWING;
                cursor_info->hCursor = stable_cursor;
                InterlockedExchange(&native_cursor_transparent, 1);
            } else {
                cursor_info->flags = 0;
                cursor_info->hCursor = NULL;
                InterlockedExchange(&native_cursor_transparent, 0);
                InterlockedIncrement(&native_cursor_fallbacks);
            }
            InterlockedExchange(&native_cursor_active, 0);
            return TRUE;
        }
        InterlockedExchange(&native_cursor_transparent, 0);
        cursor_info->flags = CURSOR_SHOWING;
        stable_cursor = refresh_native_cursor();
        if (!stable_cursor) {
            InterlockedIncrement(&native_cursor_fallbacks);
            stable_cursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        }
        if (stable_cursor) {
            cursor_info->hCursor = stable_cursor;
        }
        tracked_ready = read_tracked_cursor(
            &tracked, &tracked_virtual_desktop
        );
        if (tracked_ready && tracked_virtual_desktop) {
            if (!result) {
                cursor_info->ptScreenPos = tracked;
                result = TRUE;
            }
        } else if (result) {
            actual = cursor_info->ptScreenPos;
            if (actual_cursor_to_screen_local(&actual, &local)) {
                cursor_info->ptScreenPos = local;
            }
        } else if (tracked_ready) {
            cursor_info->ptScreenPos = tracked;
            result = TRUE;
        } else if (!result) {
            result = GetCursorPos(&cursor_info->ptScreenPos);
        }
    }
    return result;
}

static BOOL WINAPI hooked_get_cursor_pos(LPPOINT point) {
    POINT actual;
    POINT local;
    POINT tracked;
    BOOL tracked_virtual_desktop = FALSE;
    BOOL tracked_ready;
    BOOL result = original_get_cursor_pos
        ? original_get_cursor_pos(point)
        : FALSE;
    if (point && refresh_endpoint()) {
        tracked_ready = read_tracked_cursor(
            &tracked, &tracked_virtual_desktop
        );
        if (tracked_ready && tracked_virtual_desktop) {
            if (!result) {
                *point = tracked;
                return TRUE;
            }
            return result;
        }
        if (result) {
            actual = *point;
            if (actual_cursor_to_screen_local(&actual, &local)) {
                *point = local;
                return TRUE;
            }
        }
        if (tracked_ready) {
            *point = tracked;
            return TRUE;
        }
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
    frame_path_checked_at = 0;
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
        header->width > UUWF_MAX_DIMENSION ||
        header->height > UUWF_MAX_DIMENSION ||
        (ULONGLONG)header->width * header->height > UUWF_MAX_PIXELS ||
        header->stride != header->width * 4u ||
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
        size.QuadPart >
            (LONGLONG)UUWF_HEADER_SIZE +
                (LONGLONG)UUWF_BUFFER_COUNT * UUWF_MAX_PIXELS * 4
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
    frame_path_checked_at = GetTickCount64();
    return TRUE;
}

static BOOL frame_mapping_matches_path(void) {
    BY_HANDLE_FILE_INFORMATION mapped_info;
    BY_HANDLE_FILE_INFORMATION current_info;
    HANDLE current;
    BOOL matches;

    if (frame_file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    current = CreateFileW(
        frame_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (current == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ZeroMemory(&mapped_info, sizeof(mapped_info));
    ZeroMemory(&current_info, sizeof(current_info));
    matches = GetFileInformationByHandle(frame_file, &mapped_info) &&
        GetFileInformationByHandle(current, &current_info) &&
        mapped_info.dwVolumeSerialNumber == current_info.dwVolumeSerialNumber &&
        mapped_info.nFileIndexHigh == current_info.nFileIndexHigh &&
        mapped_info.nFileIndexLow == current_info.nFileIndexLow;
    CloseHandle(current);
    return matches;
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
    WritePrivateProfileStringA(
        "cursor",
        "active",
        InterlockedCompareExchange(&native_cursor_active, 0, 0) ? "1" : "0",
        frame_status_path
    );
    WritePrivateProfileStringA(
        "cursor",
        "transparent",
        InterlockedCompareExchange(&native_cursor_transparent, 0, 0)
            ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_calls, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "calls", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_updates, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "updates", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_fallbacks, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "fallbacks", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_sequence, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "sequence", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_width, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "width", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_height, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "height", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_hotspot_x, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "hotspot_x", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_hotspot_y, 0, 0)
    );
    WritePrivateProfileStringA("cursor", "hotspot_y", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_cache_entries, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "cache_entries", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_cache_overflows, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "cache_overflows", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&native_cursor_cache_drops, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "cache_drops", value, frame_status_path
    );
    WritePrivateProfileStringA(
        "cursor",
        "dxgi_factory_iat",
        streamer_dxgi_factory_hooked ? "1" : "0",
        frame_status_path
    );
    WritePrivateProfileStringA(
        "cursor",
        "dxgi_factory_vtable",
        dxgi_factory_vtable_hooked ? "1" : "0",
        frame_status_path
    );
    WritePrivateProfileStringA(
        "cursor",
        "dxgi_adapter_vtable",
        dxgi_adapter_vtable_hooked ? "1" : "0",
        frame_status_path
    );
    WritePrivateProfileStringA(
        "cursor",
        "dxgi_output_vtable",
        dxgi_output_vtable_hooked ? "1" : "0",
        frame_status_path
    );
    WritePrivateProfileStringA(
        "cursor",
        "dxgi_duplication_vtable",
        dxgi_duplication_vtable_hooked ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_factories_created, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_factories", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_adapters_enumerated, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_adapters", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_outputs_enumerated, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_outputs", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_duplications_created, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_duplications", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_acquire_calls, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_acquire_calls", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_cursor_announcements, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_announcements", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_cursor_shape_requests, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_shape_requests", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_cursor_shape_updates, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_shape_updates", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&dxgi_cursor_shape_fallbacks, 0, 0)
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_shape_fallbacks", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &dxgi_cursor_delivered_sequence, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "cursor", "dxgi_sequence", value, frame_status_path
    );
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
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_x, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_x", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_y, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_y", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_width, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_width", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_height, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_height", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_bitmap_width, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_bitmap_width", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_bitmap_height, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_bitmap_height", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_destination_bitmap_stride, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "destination_bitmap_stride", value, frame_status_path
    );
    wsprintfA(
        value,
        "%lu",
        (unsigned long)InterlockedCompareExchange(
            &frame_destination_checksum, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "capture", "destination_checksum", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_destination_checksum_changes, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "capture", "destination_checksum_changes", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_source_x, 0, 0)
    );
    WritePrivateProfileStringA("capture", "source_x", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_source_y, 0, 0)
    );
    WritePrivateProfileStringA("capture", "source_y", value, frame_status_path);
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_dib_source_y, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "dib_source_y", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_source_width, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "source_width", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_source_height, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "source_height", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_monitor_left, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "monitor_left", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_monitor_top, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "monitor_top", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_monitor_width, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "monitor_width", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_monitor_height, 0, 0)
    );
    WritePrivateProfileStringA(
        "capture", "monitor_height", value, frame_status_path
    );
    wsprintfA(
        value, "%lu", (unsigned long)configured_frame_adapter_luid
    );
    WritePrivateProfileStringA(
        "encoder", "adapter_luid", value, frame_status_path
    );
    WritePrivateProfileStringA(
        "encoder",
        "gdi_adapter_hooked",
        streamer_frame_adapter_hooked ? "1" : "0",
        frame_status_path
    );
    WritePrivateProfileStringA(
        "encoder",
        "wrapper_adapter_hooked",
        streamer_wrapper_frame_adapter_hooked ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_adapter_hook_calls, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "adapter_queries", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_adapter_wrapper_hook_calls, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "wrapper_adapter_queries", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_adapter_slots_patched, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "adapter_slots_patched", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_adapter_long_candidates, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "long_candidates", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_adapter_wrapper_candidates, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "wrapper_candidates", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_adapter_rtti_named_candidates, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "rtti_named_candidates", value, frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_adapter_encoder_path_candidates, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "encoder_path_candidates", value, frame_status_path
    );
    WritePrivateProfileStringA(
        "encoder",
        "portrait_scale_limit_hooked",
        streamer_portrait_scale_limit_hooked ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_portrait_scale_limit_candidates, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_scale_limit_candidates", value,
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_portrait_scale_limit_patches, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_scale_limit_patches", value,
        frame_status_path
    );
    WritePrivateProfileStringA(
        "encoder",
        "portrait_capture_limit_hooked",
        streamer_portrait_capture_limit_hooked ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_portrait_capture_limit_candidates, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_capture_limit_candidates", value,
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_portrait_capture_limit_patches, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_capture_limit_patches", value,
        frame_status_path
    );
    WritePrivateProfileStringA(
        "encoder",
        "portrait_update_limit_hooked",
        streamer_portrait_update_limit_hooked ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_portrait_update_limit_candidates, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_update_limit_candidates", value,
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(
            &frame_portrait_update_limit_patches, 0, 0
        )
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_update_limit_patches", value,
        frame_status_path
    );
    WritePrivateProfileStringA(
        "encoder",
        "portrait_scaler_hooked",
        streamer_portrait_scaler_hooked ? "1" : "0",
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_portrait_scaler_candidates, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_scaler_candidates", value,
        frame_status_path
    );
    wsprintfA(
        value,
        "%ld",
        InterlockedCompareExchange(&frame_portrait_scaler_patches, 0, 0)
    );
    WritePrivateProfileStringA(
        "encoder", "portrait_scaler_patches", value,
        frame_status_path
    );
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
    if (
        now - frame_path_checked_at >= UUWF_PATH_CHECK_INTERVAL_MS
    ) {
        frame_path_checked_at = now;
        if (!frame_mapping_matches_path()) {
            close_frame_mapping();
            return FALSE;
        }
    }
    MemoryBarrier();
    sequence = header->sequence;
    if (sequence && sequence != frame_last_sequence) {
        frame_last_sequence = sequence;
    }
    return sequence != 0;
}

static DWORD sample_destination_bitmap(const BITMAP *bitmap) {
    const BYTE *bits;
    LONG height;
    LONG stride;
    LONG row_step;
    LONG byte_step;
    LONG row;
    DWORD hash = 2166136261u;

    if (!bitmap || !bitmap->bmBits || !bitmap->bmWidthBytes) {
        return 0;
    }
    height = bitmap->bmHeight < 0 ? -bitmap->bmHeight : bitmap->bmHeight;
    stride = bitmap->bmWidthBytes < 0 ?
        -bitmap->bmWidthBytes : bitmap->bmWidthBytes;
    if (height <= 0 || stride <= 0) {
        return 0;
    }
    bits = (const BYTE *)bitmap->bmBits;
    row_step = height > 32 ? height / 32 : 1;
    byte_step = stride > 128 ? stride / 128 : 1;
    for (row = 0; row < height; row += row_step) {
        LONG offset;
        const BYTE *line = bits + (SIZE_T)row * stride;

        for (offset = 0; offset < stride; offset += byte_step) {
            hash ^= line[offset];
            hash *= 16777619u;
        }
    }
    return hash;
}

struct frame_monitor_match {
    LONG width;
    LONG height;
    unsigned int count;
    RECT rectangle;
};

static BOOL CALLBACK match_frame_source_monitor(
    HMONITOR monitor,
    HDC monitor_dc,
    LPRECT rectangle,
    LPARAM parameter
) {
    struct frame_monitor_match *match =
        (struct frame_monitor_match *)parameter;
    LONG width;
    LONG height;
    (void)monitor;
    (void)monitor_dc;
    if (!match || !rectangle) {
        return TRUE;
    }
    width = rectangle->right - rectangle->left;
    height = rectangle->bottom - rectangle->top;
    if (width == match->width && height == match->height) {
        match->rectangle = *rectangle;
        ++match->count;
    }
    return TRUE;
}

/*
 * CreateDC("\\\\.\\DISPLAYn") exposes monitor-local source coordinates,
 * while GetDC(NULL) uses Wine's primary-relative virtual coordinates.  Portal
 * frames are a single virtual-desktop canvas, so translate the former into the
 * latter when the source DC uniquely identifies one physical monitor.
 */
static BOOL offset_frame_source_coordinates(
    int source_x,
    int source_y,
    LONG offset_x,
    LONG offset_y,
    int *translated_x,
    int *translated_y
) {
    LONGLONG wide_x = (LONGLONG)source_x + offset_x;
    LONGLONG wide_y = (LONGLONG)source_y + offset_y;

    if (
        !translated_x || !translated_y ||
        wide_x < INT_MIN || wide_x > INT_MAX ||
        wide_y < INT_MIN || wide_y > INT_MAX
    ) {
        return FALSE;
    }
    *translated_x = (int)wide_x;
    *translated_y = (int)wide_y;
    return TRUE;
}

static BOOL normalize_frame_source_coordinates(
    HDC source,
    int *source_x,
    int *source_y
) {
    struct frame_monitor_match match;
    LONG dc_width;
    LONG dc_height;
    if (!source || !source_x || !source_y || *source_x < 0 || *source_y < 0) {
        return TRUE;
    }
    dc_width = GetDeviceCaps(source, HORZRES);
    dc_height = GetDeviceCaps(source, VERTRES);
    if (dc_width <= 0 || dc_height <= 0) {
        return TRUE;
    }
    ZeroMemory(&match, sizeof(match));
    match.width = dc_width;
    match.height = dc_height;
    EnumDisplayMonitors(NULL, NULL, match_frame_source_monitor, (LPARAM)&match);
    if (match.count == 1u) {
        return offset_frame_source_coordinates(
            *source_x,
            *source_y,
            match.rectangle.left,
            match.rectangle.top,
            source_x,
            source_y
        );
    }
    return TRUE;
}

static BOOL clamp_frame_source_rectangle(
    uint32_t frame_width,
    uint32_t frame_height,
    int source_x,
    int source_y,
    int *source_width,
    int *source_height
) {
    int available_width;
    int available_height;

    if (
        !source_width || !source_height ||
        frame_width == 0 || frame_width > INT_MAX ||
        frame_height == 0 || frame_height > INT_MAX ||
        source_x < 0 || source_y < 0 ||
        source_x >= (int)frame_width || source_y >= (int)frame_height ||
        *source_width <= 0 || *source_height <= 0
    ) {
        return FALSE;
    }
    available_width = (int)frame_width - source_x;
    available_height = (int)frame_height - source_y;
    if (*source_width > available_width) {
        *source_width = available_width;
    }
    if (*source_height > available_height) {
        *source_height = available_height;
    }
    return *source_width > 0 && *source_height > 0;
}

static BOOL ensure_frame_snapshot(SIZE_T required) {
    unsigned char *snapshot;

    if (frame_snapshot && frame_snapshot_capacity >= required) {
        return TRUE;
    }
    snapshot = VirtualAlloc(
        NULL,
        required,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!snapshot) {
        return FALSE;
    }
    if (frame_snapshot) {
        VirtualFree(frame_snapshot, 0, MEM_RELEASE);
    }
    frame_snapshot = snapshot;
    frame_snapshot_capacity = required;
    return TRUE;
}

static BOOL snapshot_wayland_frame(
    const struct wayland_frame_header *header,
    const unsigned char *view,
    const unsigned char **pixels_out
) {
    const unsigned char *source;
    uint32_t attempt;
    uint32_t sequence_before;
    uint32_t sequence_after;
    uint32_t active_before;
    uint32_t active_after;

    if (
        !header || !view || !pixels_out || header->sequence == 0 ||
        !ensure_frame_snapshot((SIZE_T)header->frame_size)
    ) {
        InterlockedIncrement(&frame_snapshot_failures);
        return FALSE;
    }
    for (attempt = 0; attempt < UUWF_SNAPSHOT_ATTEMPTS; ++attempt) {
        sequence_before = header->sequence;
        MemoryBarrier();
        active_before = header->active_buffer;
        if (
            sequence_before == 0 ||
            active_before >= header->buffer_count
        ) {
            InterlockedIncrement(&frame_snapshot_failures);
            return FALSE;
        }
        source = view + header->header_size +
            (SIZE_T)active_before * header->frame_size;
        CopyMemory(frame_snapshot, source, header->frame_size);
        MemoryBarrier();
        active_after = header->active_buffer;
        sequence_after = header->sequence;
        if (
            sequence_before == sequence_after &&
            active_before == active_after
        ) {
            *pixels_out = frame_snapshot;
            return TRUE;
        }
        InterlockedIncrement(&frame_snapshot_retries);
    }
    InterlockedIncrement(&frame_snapshot_failures);
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
    int result;
    LONG calls;
    POINT source_point;
    HMONITOR source_monitor;
    MONITORINFO monitor_info;
    int64_t translated_source_x;
    int64_t translated_source_y;
    int dib_source_y;
    DWORD destination_checksum;
    LONG previous_checksum;
    HGDIOBJ destination_bitmap_object;
    BITMAP destination_bitmap;

    calls = InterlockedIncrement(&frame_hook_calls);
    InterlockedExchange(&frame_destination_x, destination_x);
    InterlockedExchange(&frame_destination_y, destination_y);
    InterlockedExchange(&frame_destination_width, destination_width);
    InterlockedExchange(&frame_destination_height, destination_height);
    InterlockedExchange(&frame_source_x, source_x);
    InterlockedExchange(&frame_source_y, source_y);
    InterlockedExchange(&frame_source_width, source_width);
    InterlockedExchange(&frame_source_height, source_height);
    destination_bitmap_object = destination ?
        GetCurrentObject(destination, OBJ_BITMAP) : NULL;
    ZeroMemory(&destination_bitmap, sizeof(destination_bitmap));
    if (
        destination_bitmap_object &&
        GetObjectW(
            destination_bitmap_object,
            sizeof(destination_bitmap),
            &destination_bitmap
        ) == sizeof(destination_bitmap)
    ) {
        InterlockedExchange(
            &frame_destination_bitmap_width, destination_bitmap.bmWidth
        );
        InterlockedExchange(
            &frame_destination_bitmap_height, destination_bitmap.bmHeight
        );
        InterlockedExchange(
            &frame_destination_bitmap_stride, destination_bitmap.bmWidthBytes
        );
    }
    source_point.x = source_x;
    source_point.y = source_y;
    source_monitor = MonitorFromPoint(source_point, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitor_info, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    if (source_monitor && GetMonitorInfoW(source_monitor, &monitor_info)) {
        InterlockedExchange(&frame_monitor_left, monitor_info.rcMonitor.left);
        InterlockedExchange(&frame_monitor_top, monitor_info.rcMonitor.top);
        InterlockedExchange(
            &frame_monitor_width,
            monitor_info.rcMonitor.right - monitor_info.rcMonitor.left
        );
        InterlockedExchange(
            &frame_monitor_height,
            monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top
        );
    }
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
    if (!normalize_frame_source_coordinates(source, &source_x, &source_y)) {
        ReleaseSRWLockExclusive(&frame_lock);
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }
    translated_source_x = (int64_t)source_x - (int64_t)header->origin_x;
    translated_source_y = (int64_t)source_y - (int64_t)header->origin_y;
    if (
        translated_source_x < 0 ||
        translated_source_y < 0 ||
        translated_source_x >= (int64_t)header->width ||
        translated_source_y >= (int64_t)header->height
    ) {
        ReleaseSRWLockExclusive(&frame_lock);
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }
    source_x = (int)translated_source_x;
    source_y = (int)translated_source_y;
    if (!clamp_frame_source_rectangle(
            header->width,
            header->height,
            source_x,
            source_y,
            &source_width,
            &source_height
        )) {
        ReleaseSRWLockExclusive(&frame_lock);
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }
    /*
     * StretchDIBits addresses the source rectangle from the DIB's lower edge,
     * including when a negative biHeight makes the scan lines top-down.  The
     * Portal canvas and monitor rectangles use a top-left origin.  Without
     * this conversion, a 2560-high dual-monitor canvas cropped to the
     * 1440-high landscape display starts at top-down row 1120: only its final
     * 320 content rows are copied, followed by 1120 black padding rows.
     */
    dib_source_y = (int)header->height - source_y - source_height;
    InterlockedExchange(&frame_dib_source_y, dib_source_y);
    if (!snapshot_wayland_frame(header, frame_view, &pixels)) {
        ReleaseSRWLockExclusive(&frame_lock);
        InterlockedIncrement(&frame_hook_fallbacks);
        return FALSE;
    }
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
        dib_source_y,
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
    destination_checksum = sample_destination_bitmap(&destination_bitmap);
    if (destination_checksum) {
        previous_checksum = InterlockedExchange(
            &frame_destination_checksum, (LONG)destination_checksum
        );
        if (
            previous_checksum &&
            previous_checksum != (LONG)destination_checksum
        ) {
            InterlockedIncrement(&frame_destination_checksum_changes);
        }
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

static BOOL bytes_equal(
    const unsigned char *actual,
    const unsigned char *expected,
    SIZE_T size
) {
    return actual && expected && memcmp(actual, expected, size) == 0;
}

static BOOL address_in_image_section(
    const unsigned char *base,
    const IMAGE_SECTION_HEADER *section,
    const void *address,
    SIZE_T size
) {
    const unsigned char *start;
    SIZE_T span;
    const unsigned char *value = (const unsigned char *)address;

    if (!base || !section || !address) {
        return FALSE;
    }
    start = base + section->VirtualAddress;
    span = section->Misc.VirtualSize;
    if (span < section->SizeOfRawData) {
        span = section->SizeOfRawData;
    }
    return value >= start && size <= span &&
        (SIZE_T)(value - start) <= span - size;
}

static DWORD WINAPI hooked_frame_adapter_id(void *frame) {
    typedef DWORD(WINAPI *frame_adapter_id_fn)(void *);
    frame_adapter_id_fn original =
        (frame_adapter_id_fn)original_frame_adapter_id;

    InterlockedIncrement(&frame_adapter_hook_calls);
    if (configured_frame_adapter_luid) {
        return configured_frame_adapter_luid;
    }
    return original ? original(frame) : 0u;
}

static ULONGLONG WINAPI hooked_wrapper_frame_adapter_id(void *frame) {
    (void)frame;
    InterlockedIncrement(&frame_adapter_hook_calls);
    InterlockedIncrement(&frame_adapter_wrapper_hook_calls);
    return (ULONGLONG)configured_frame_adapter_luid;
}

static BOOL patch_frame_adapter_slot(
    void **slot,
    void *replacement,
    void **original
) {
    DWORD old_protection;
    void *previous;

    if (!slot || !replacement || !VirtualProtect(
            slot, sizeof(*slot), PAGE_READWRITE, &old_protection
        )) {
        return FALSE;
    }
    previous = InterlockedExchangePointer(
        (void *volatile *)slot,
        replacement
    );
    VirtualProtect(slot, sizeof(*slot), old_protection, &old_protection);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    if (original && previous != replacement) {
        *original = previous;
    }
    if (previous != replacement) {
        InterlockedIncrement(&frame_adapter_slots_patched);
    }
    return TRUE;
}

static BOOL patch_dxgi_vtable_slot(
    void **slot,
    void *replacement,
    void **original
) {
    DWORD old_protection;
    void *previous;

    if (!slot || !replacement || !VirtualProtect(
            slot, sizeof(*slot), PAGE_READWRITE, &old_protection
        )) {
        return FALSE;
    }
    previous = InterlockedExchangePointer(
        (void *volatile *)slot,
        replacement
    );
    VirtualProtect(slot, sizeof(*slot), old_protection, &old_protection);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    if (original && previous != replacement) {
        *original = previous;
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE hooked_dxgi_get_frame_pointer_shape(
    IDXGIOutputDuplication *duplication,
    UINT buffer_size,
    void *buffer,
    UINT *required_size,
    DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape_info
) {
    HRESULT result;
    uint32_t sequence = 0;

    InterlockedIncrement(&dxgi_cursor_shape_requests);
    if (refresh_endpoint() && force_cursor_visible) {
        result = fill_native_dxgi_cursor_shape(
            buffer_size,
            buffer,
            required_size,
            shape_info,
            &sequence
        );
        if (result == S_OK) {
            InterlockedExchange(
                &dxgi_cursor_delivered_sequence,
                (LONG)sequence
            );
            InterlockedIncrement(&dxgi_cursor_shape_updates);
            return result;
        }
        if (result == DXGI_ERROR_MORE_DATA) {
            return result;
        }
        InterlockedIncrement(&dxgi_cursor_shape_fallbacks);
    }
    return original_dxgi_get_frame_pointer_shape
        ? original_dxgi_get_frame_pointer_shape(
            duplication,
            buffer_size,
            buffer,
            required_size,
            shape_info
        )
        : DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE hooked_dxgi_acquire_next_frame(
    IDXGIOutputDuplication *duplication,
    UINT timeout_in_milliseconds,
    DXGI_OUTDUPL_FRAME_INFO *frame_info,
    IDXGIResource **desktop_resource
) {
    HRESULT result;
    LONG sequence;
    LONG delivered;
    LONG width;
    LONG height;

    InterlockedIncrement(&dxgi_acquire_calls);
    result = original_dxgi_acquire_next_frame
        ? original_dxgi_acquire_next_frame(
            duplication,
            timeout_in_milliseconds,
            frame_info,
            desktop_resource
        )
        : DXGI_ERROR_INVALID_CALL;
    if (result != S_OK || !frame_info || !refresh_endpoint() ||
        !force_cursor_visible || !refresh_native_cursor()) {
        return result;
    }
    sequence = InterlockedCompareExchange(&native_cursor_sequence, 0, 0);
    delivered = InterlockedCompareExchange(
        &dxgi_cursor_delivered_sequence, 0, 0
    );
    width = InterlockedCompareExchange(&native_cursor_width, 0, 0);
    height = InterlockedCompareExchange(&native_cursor_height, 0, 0);
    if (
        sequence > 0 && sequence != delivered &&
        width > 0 && height > 0 &&
        width <= (LONG)UUCI_MAX_DIMENSION &&
        height <= (LONG)UUCI_MAX_DIMENSION
    ) {
        frame_info->PointerShapeBufferSize =
            (UINT)width * (UINT)height * 4u;
        if (frame_info->LastMouseUpdateTime.QuadPart == 0) {
            frame_info->LastMouseUpdateTime.QuadPart =
                (LONGLONG)GetTickCount64();
        }
        InterlockedIncrement(&dxgi_cursor_announcements);
    }
    return result;
}

static void patch_dxgi_duplication(
    IDXGIOutputDuplication *duplication
) {
    BOOL acquire_ready;
    BOOL shape_ready;

    if (!duplication || !duplication->lpVtbl) {
        return;
    }
    acquire_ready = patch_dxgi_vtable_slot(
        (void **)&duplication->lpVtbl->AcquireNextFrame,
        (void *)hooked_dxgi_acquire_next_frame,
        (void **)&original_dxgi_acquire_next_frame
    );
    shape_ready = patch_dxgi_vtable_slot(
        (void **)&duplication->lpVtbl->GetFramePointerShape,
        (void *)hooked_dxgi_get_frame_pointer_shape,
        (void **)&original_dxgi_get_frame_pointer_shape
    );
    if (acquire_ready && shape_ready) {
        dxgi_duplication_vtable_hooked = TRUE;
        InterlockedExchange(&dxgi_cursor_delivered_sequence, 0);
    }
}

static HRESULT STDMETHODCALLTYPE hooked_dxgi_duplicate_output(
    IDXGIOutput1 *output,
    IUnknown *device,
    IDXGIOutputDuplication **duplication
) {
    HRESULT result = original_dxgi_duplicate_output
        ? original_dxgi_duplicate_output(output, device, duplication)
        : DXGI_ERROR_INVALID_CALL;

    if (result == S_OK && duplication && *duplication) {
        InterlockedIncrement(&dxgi_duplications_created);
        patch_dxgi_duplication(*duplication);
    }
    return result;
}

static void patch_dxgi_output(IDXGIOutput *output) {
    static const GUID iid_output1 = {
        0x00cddea8,
        0x939b,
        0x4b83,
        {0xa3, 0x40, 0xa6, 0x85, 0x22, 0x66, 0x66, 0xcc}
    };
    IDXGIOutput1 *output1 = NULL;
    HRESULT result;

    if (!output || !output->lpVtbl) {
        return;
    }
    result = output->lpVtbl->QueryInterface(
        output,
        &iid_output1,
        (void **)&output1
    );
    if (result != S_OK || !output1 || !output1->lpVtbl) {
        return;
    }
    if (patch_dxgi_vtable_slot(
            (void **)&output1->lpVtbl->DuplicateOutput,
            (void *)hooked_dxgi_duplicate_output,
            (void **)&original_dxgi_duplicate_output
        )) {
        dxgi_output_vtable_hooked = TRUE;
    }
    output1->lpVtbl->Release(output1);
}

static HRESULT STDMETHODCALLTYPE hooked_dxgi_enum_outputs(
    IDXGIAdapter *adapter,
    UINT output_index,
    IDXGIOutput **output
) {
    HRESULT result = original_dxgi_enum_outputs
        ? original_dxgi_enum_outputs(adapter, output_index, output)
        : DXGI_ERROR_INVALID_CALL;

    if (result == S_OK && output && *output) {
        InterlockedIncrement(&dxgi_outputs_enumerated);
        patch_dxgi_output(*output);
    }
    return result;
}

static void patch_dxgi_adapter(IDXGIAdapter *adapter) {
    if (!adapter || !adapter->lpVtbl) {
        return;
    }
    if (patch_dxgi_vtable_slot(
            (void **)&adapter->lpVtbl->EnumOutputs,
            (void *)hooked_dxgi_enum_outputs,
            (void **)&original_dxgi_enum_outputs
        )) {
        dxgi_adapter_vtable_hooked = TRUE;
    }
}

static HRESULT STDMETHODCALLTYPE hooked_dxgi_enum_adapters(
    IDXGIFactory1 *factory,
    UINT adapter_index,
    IDXGIAdapter **adapter
) {
    HRESULT result = original_dxgi_enum_adapters
        ? original_dxgi_enum_adapters(factory, adapter_index, adapter)
        : DXGI_ERROR_INVALID_CALL;

    if (result == S_OK && adapter && *adapter) {
        InterlockedIncrement(&dxgi_adapters_enumerated);
        patch_dxgi_adapter(*adapter);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE hooked_dxgi_enum_adapters1(
    IDXGIFactory1 *factory,
    UINT adapter_index,
    IDXGIAdapter1 **adapter
) {
    HRESULT result = original_dxgi_enum_adapters1
        ? original_dxgi_enum_adapters1(factory, adapter_index, adapter)
        : DXGI_ERROR_INVALID_CALL;

    if (result == S_OK && adapter && *adapter) {
        InterlockedIncrement(&dxgi_adapters_enumerated);
        patch_dxgi_adapter((IDXGIAdapter *)*adapter);
    }
    return result;
}

static void patch_dxgi_factory(IDXGIFactory1 *factory) {
    BOOL enum_ready;
    BOOL enum1_ready;

    if (!factory || !factory->lpVtbl) {
        return;
    }
    enum_ready = patch_dxgi_vtable_slot(
        (void **)&factory->lpVtbl->EnumAdapters,
        (void *)hooked_dxgi_enum_adapters,
        (void **)&original_dxgi_enum_adapters
    );
    enum1_ready = patch_dxgi_vtable_slot(
        (void **)&factory->lpVtbl->EnumAdapters1,
        (void *)hooked_dxgi_enum_adapters1,
        (void **)&original_dxgi_enum_adapters1
    );
    if (enum_ready && enum1_ready) {
        dxgi_factory_vtable_hooked = TRUE;
    }
}

static HRESULT WINAPI hooked_create_dxgi_factory1(
    REFIID interface_id,
    void **factory
) {
    HRESULT result = original_create_dxgi_factory1
        ? original_create_dxgi_factory1(interface_id, factory)
        : DXGI_ERROR_INVALID_CALL;

    if (result == S_OK && factory && *factory) {
        InterlockedIncrement(&dxgi_factories_created);
        patch_dxgi_factory((IDXGIFactory1 *)*factory);
    }
    return result;
}

struct executable_patch_thread {
    HANDLE handle;
    DWORD id;
};

struct executable_patch_route {
    void *volatile function;
    void *volatile replacement;
    void *volatile allocation_base;
    DWORD allocation_type;
    DWORD image_timestamp;
    DWORD image_size;
};

static SRWLOCK executable_patch_lock = SRWLOCK_INIT;
static struct executable_patch_route
    executable_patch_routes[EXECUTABLE_PATCH_MAX_ROUTES];
static volatile LONG executable_patch_route_count;
static volatile LONG executable_patch_redirects;
static volatile LONG executable_patch_test_gate_delay_ms;
static PVOID executable_patch_exception_handler_handle;

static BOOL executable_patch_image_identity(
    void *allocation_base,
    DWORD *timestamp,
    DWORD *image_size
) {
    IMAGE_DOS_HEADER dos_header;
    IMAGE_NT_HEADERS64 nt_headers;
    SIZE_T read_size;

    if (
        !ReadProcessMemory(
            GetCurrentProcess(),
            allocation_base,
            &dos_header,
            sizeof(dos_header),
            &read_size
        ) ||
        read_size != sizeof(dos_header) ||
        dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
        dos_header.e_lfanew <= 0 ||
        dos_header.e_lfanew > 1024 * 1024
    ) {
        return FALSE;
    }
    if (
        !ReadProcessMemory(
            GetCurrentProcess(),
            (unsigned char *)allocation_base + dos_header.e_lfanew,
            &nt_headers,
            sizeof(nt_headers),
            &read_size
        ) ||
        read_size != sizeof(nt_headers) ||
        nt_headers.Signature != IMAGE_NT_SIGNATURE ||
        nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    *timestamp = nt_headers.FileHeader.TimeDateStamp;
    *image_size = nt_headers.OptionalHeader.SizeOfImage;
    return TRUE;
}

static LONG CALLBACK executable_patch_exception_handler(
    EXCEPTION_POINTERS *exception
) {
    LONG index;

    if (
        !exception || !exception->ExceptionRecord ||
        !exception->ContextRecord ||
        exception->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT
    ) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    for (index = 0; index < (LONG)EXECUTABLE_PATCH_MAX_ROUTES; ++index) {
        void *function = InterlockedCompareExchangePointer(
            &executable_patch_routes[index].function, NULL, NULL
        );

        if (exception->ExceptionRecord->ExceptionAddress == function) {
            void *replacement = InterlockedCompareExchangePointer(
                &executable_patch_routes[index].replacement, NULL, NULL
            );

            if (!replacement) {
                break;
            }
            exception->ContextRecord->Rip = (DWORD64)(uintptr_t)replacement;
            InterlockedIncrement(&executable_patch_redirects);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void collect_stale_executable_patch_routes_locked(void) {
    LONG index;

    for (index = 0; index < (LONG)EXECUTABLE_PATCH_MAX_ROUTES; ++index) {
        struct executable_patch_route *route =
            &executable_patch_routes[index];
        void *function = InterlockedCompareExchangePointer(
            &route->function, NULL, NULL
        );
        void *allocation_base;
        MEMORY_BASIC_INFORMATION memory;
        BOOL live;

        if (!function) {
            continue;
        }
        allocation_base = InterlockedCompareExchangePointer(
            &route->allocation_base, NULL, NULL
        );
        live = (
            VirtualQuery(function, &memory, sizeof(memory)) == sizeof(memory) &&
            memory.State == MEM_COMMIT &&
            memory.AllocationBase == allocation_base &&
            memory.Type == route->allocation_type
        );
        if (live && memory.Type == MEM_IMAGE) {
            DWORD timestamp;
            DWORD image_size;

            live = executable_patch_image_identity(
                allocation_base, &timestamp, &image_size
            ) &&
                timestamp == route->image_timestamp &&
                image_size == route->image_size;
        }
        if (live) {
            continue;
        }
        InterlockedExchangePointer(&route->function, NULL);
        MemoryBarrier();
        InterlockedExchangePointer(&route->replacement, NULL);
        InterlockedExchangePointer(&route->allocation_base, NULL);
        route->allocation_type = 0;
        route->image_timestamp = 0;
        route->image_size = 0;
        InterlockedDecrement(&executable_patch_route_count);
    }
    if (
        InterlockedCompareExchange(
            &executable_patch_route_count, 0, 0
        ) == 0 &&
        executable_patch_exception_handler_handle
    ) {
        PVOID handler = executable_patch_exception_handler_handle;

        executable_patch_exception_handler_handle = NULL;
        RemoveVectoredExceptionHandler(handler);
    }
}

static void collect_stale_executable_patch_routes(void) {
    AcquireSRWLockExclusive(&executable_patch_lock);
    collect_stale_executable_patch_routes_locked();
    ReleaseSRWLockExclusive(&executable_patch_lock);
}

static BOOL register_executable_patch_route(
    void *function,
    void *replacement
) {
    LONG index;
    LONG free_index = -1;
    MEMORY_BASIC_INFORMATION memory;
    DWORD image_timestamp = 0;
    DWORD image_size = 0;

    if (
        VirtualQuery(function, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT
    ) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    if (
        memory.Type == MEM_IMAGE &&
        !executable_patch_image_identity(
            memory.AllocationBase, &image_timestamp, &image_size
        )
    ) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }
    collect_stale_executable_patch_routes_locked();

    if (!executable_patch_exception_handler_handle) {
        executable_patch_exception_handler_handle =
            AddVectoredExceptionHandler(
                1u, executable_patch_exception_handler
            );
        if (!executable_patch_exception_handler_handle) {
            return FALSE;
        }
    }
    for (index = 0; index < (LONG)EXECUTABLE_PATCH_MAX_ROUTES; ++index) {
        void *registered_function = InterlockedCompareExchangePointer(
            &executable_patch_routes[index].function, NULL, NULL
        );

        if (registered_function == function) {
            InterlockedExchangePointer(
                &executable_patch_routes[index].replacement, replacement
            );
            return TRUE;
        }
        if (!registered_function && free_index < 0) {
            free_index = index;
        }
    }
    if (free_index < 0) {
        SetLastError(ERROR_TOO_MANY_NAMES);
        return FALSE;
    }
    InterlockedExchangePointer(
        &executable_patch_routes[free_index].allocation_base,
        memory.AllocationBase
    );
    executable_patch_routes[free_index].allocation_type = memory.Type;
    executable_patch_routes[free_index].image_timestamp = image_timestamp;
    executable_patch_routes[free_index].image_size = image_size;
    InterlockedExchangePointer(
        &executable_patch_routes[free_index].replacement, replacement
    );
    MemoryBarrier();
    InterlockedExchangePointer(
        &executable_patch_routes[free_index].function, function
    );
    MemoryBarrier();
    InterlockedIncrement(&executable_patch_route_count);
    return TRUE;
}

static BOOL executable_patch_thread_recorded(
    const struct executable_patch_thread *threads,
    SIZE_T count,
    DWORD id
) {
    SIZE_T index;

    for (index = 0u; index < count; ++index) {
        if (threads[index].id == id) {
            return TRUE;
        }
    }
    return FALSE;
}

static void resume_executable_patch_threads(
    struct executable_patch_thread *threads,
    SIZE_T count
) {
    while (count) {
        --count;
        ResumeThread(threads[count].handle);
        CloseHandle(threads[count].handle);
        threads[count].handle = NULL;
    }
}

static BOOL suspend_executable_patch_threads(
    struct executable_patch_thread *threads,
    SIZE_T *count
) {
    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_thread_id = GetCurrentThreadId();
    unsigned int pass;

    *count = 0u;
    for (pass = 0u; pass < 4u; ++pass) {
        THREADENTRY32 entry;
        HANDLE snapshot;
        BOOL added = FALSE;
        BOOL have_entry;

        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            goto failed;
        }
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);
        have_entry = Thread32First(snapshot, &entry);
        if (!have_entry) {
            DWORD error = GetLastError();
            CloseHandle(snapshot);
            SetLastError(error);
            goto failed;
        }
        while (have_entry) {
            HANDLE thread;
            DWORD suspend_count;

            if (
                entry.th32OwnerProcessID != process_id ||
                entry.th32ThreadID == current_thread_id ||
                executable_patch_thread_recorded(
                    threads, *count, entry.th32ThreadID
                )
            ) {
                have_entry = Thread32Next(snapshot, &entry);
                continue;
            }
            if (*count >= EXECUTABLE_PATCH_MAX_THREADS) {
                CloseHandle(snapshot);
                SetLastError(ERROR_TOO_MANY_TCBS);
                goto failed;
            }
            thread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                FALSE,
                entry.th32ThreadID
            );
            if (!thread) {
                DWORD error = GetLastError();
                if (error == ERROR_INVALID_PARAMETER) {
                    have_entry = Thread32Next(snapshot, &entry);
                    continue;
                }
                CloseHandle(snapshot);
                SetLastError(error);
                goto failed;
            }
            suspend_count = SuspendThread(thread);
            if (suspend_count == (DWORD)-1) {
                DWORD error = GetLastError();
                if (WaitForSingleObject(thread, 0) == WAIT_OBJECT_0) {
                    CloseHandle(thread);
                    have_entry = Thread32Next(snapshot, &entry);
                    continue;
                }
                CloseHandle(thread);
                CloseHandle(snapshot);
                SetLastError(error);
                goto failed;
            }
            threads[*count].handle = thread;
            threads[*count].id = entry.th32ThreadID;
            ++*count;
            added = TRUE;
            have_entry = Thread32Next(snapshot, &entry);
        }
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            DWORD error = GetLastError();
            CloseHandle(snapshot);
            SetLastError(error);
            goto failed;
        }
        CloseHandle(snapshot);
        if (!added) {
            return TRUE;
        }
    }
    SetLastError(ERROR_BUSY);

failed:
    resume_executable_patch_threads(threads, *count);
    *count = 0u;
    return FALSE;
}

static BOOL executable_patch_range_is_idle(
    const struct executable_patch_thread *threads,
    SIZE_T count,
    const void *function
) {
    const uintptr_t start = (uintptr_t)function;
    const uintptr_t end = start + EXECUTABLE_PATCH_SIZE;
    SIZE_T index;

    for (index = 0u; index < count; ++index) {
        CONTEXT context;

        ZeroMemory(&context, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(threads[index].handle, &context)) {
            if (WaitForSingleObject(threads[index].handle, 0) == WAIT_OBJECT_0) {
                continue;
            }
            return FALSE;
        }
        if ((uintptr_t)context.Rip >= start && (uintptr_t)context.Rip < end) {
            SetLastError(ERROR_BUSY);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL patch_executable_entry(void *function, void *replacement) {
    unsigned char jump[EXECUTABLE_PATCH_SIZE];
    DWORD old_protection = 0;
    DWORD ignored_protection;
    DWORD failure_error = ERROR_SUCCESS;
    struct executable_patch_thread threads[EXECUTABLE_PATCH_MAX_THREADS];
    SIZE_T thread_count;
    unsigned int attempt;
    char original_first_byte = 0;
    BOOL gate_installed = FALSE;
    BOOL protection_changed = FALSE;
    BOOL success = FALSE;

    if (!function || !replacement) {
        return FALSE;
    }
    AcquireSRWLockExclusive(&executable_patch_lock);
    jump[0] = 0x48;
    jump[1] = 0xb8;
    CopyMemory(jump + 2, &replacement, sizeof(replacement));
    jump[10] = 0xff;
    jump[11] = 0xe0;
    if (!register_executable_patch_route(function, replacement)) {
        failure_error = GetLastError();
        goto cleanup;
    }
    if (!VirtualProtect(
            function,
            sizeof(jump),
            PAGE_EXECUTE_READWRITE,
            &old_protection
        )) {
        failure_error = GetLastError();
        goto cleanup;
    }
    protection_changed = TRUE;
    original_first_byte = (char)__atomic_exchange_n(
        (volatile unsigned char *)function, 0xccu, __ATOMIC_SEQ_CST
    );
    gate_installed = TRUE;
    FlushInstructionCache(GetCurrentProcess(), function, 1u);
    if (InterlockedCompareExchange(
            &executable_patch_test_gate_delay_ms, 0, 0
        ) > 0) {
        Sleep((DWORD)InterlockedCompareExchange(
            &executable_patch_test_gate_delay_ms, 0, 0
        ));
    }
    for (attempt = 0u; attempt < EXECUTABLE_PATCH_MAX_ATTEMPTS; ++attempt) {
        if (!suspend_executable_patch_threads(threads, &thread_count)) {
            failure_error = GetLastError();
            goto cleanup;
        }
        if (!executable_patch_range_is_idle(
                threads, thread_count, function
            )) {
            resume_executable_patch_threads(threads, thread_count);
            Sleep(1);
            continue;
        }
        CopyMemory(
            (unsigned char *)function + 1u,
            jump + 1u,
            sizeof(jump) - 1u
        );
        FlushInstructionCache(
            GetCurrentProcess(), (unsigned char *)function + 1u,
            sizeof(jump) - 1u
        );
        __atomic_exchange_n(
            (volatile unsigned char *)function,
            jump[0],
            __ATOMIC_SEQ_CST
        );
        gate_installed = FALSE;
        FlushInstructionCache(GetCurrentProcess(), function, 1u);
        resume_executable_patch_threads(threads, thread_count);
        success = TRUE;
        break;
    }
    if (!success && failure_error == ERROR_SUCCESS) {
        failure_error = ERROR_BUSY;
    }

cleanup:
    if (gate_installed) {
        __atomic_exchange_n(
            (volatile unsigned char *)function,
            (unsigned char)original_first_byte,
            __ATOMIC_SEQ_CST
        );
        FlushInstructionCache(GetCurrentProcess(), function, 1u);
    }
    if (protection_changed) {
        VirtualProtect(
            function, sizeof(jump), old_protection, &ignored_protection
        );
    }
    ReleaseSRWLockExclusive(&executable_patch_lock);
    if (!success) {
        SetLastError(failure_error);
    }
    return success;
}

static BOOL patch_frame_adapter_accessor(void *function) {
    return patch_executable_entry(
        function, (void *)hooked_wrapper_frame_adapter_id
    );
}

/*
 * UU's "Original" quality request currently carries a landscape-oriented
 * max-scale rectangle (3840x2160).  The controlled host copies those values
 * into its capture configuration without accounting for monitor rotation, so
 * a 1440x2560 portrait display is reduced to 1216x2160 before encoding.  Find
 * the unique request-to-capture copy by semantics and make its height use the
 * same direction-neutral maximum as its width.  This changes only the upper
 * bound: the capture path still keeps the monitor's real aspect ratio.
 */
static BOOL patch_streamer_portrait_scale_limit(HMODULE module) {
    static const unsigned char scale_copy_pattern[] = {
        0x41, 0x8b, 0x56, 0x04, 0x85, 0xd2, 0x75, 0x10,
        0x41, 0x8b, 0x46, 0x30,
        0x89, 0x83, 0xc0, 0x00, 0x00, 0x00,
        0x41, 0x8b, 0x46, 0x34,
        0xeb, 0x08,
        0x89, 0xbb, 0xc0, 0x00, 0x00, 0x00,
        0x8b, 0xc7,
        0x4c, 0x8d, 0xab, 0xc4, 0x00, 0x00, 0x00,
        0x41, 0x89, 0x45, 0x00,
        0x89, 0x93, 0xc8, 0x00, 0x00, 0x00
    };
    const SIZE_T height_source_offset = 21u;
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    IMAGE_SECTION_HEADER *text = NULL;
    unsigned char *candidate = NULL;
    unsigned int candidate_count = 0;
    unsigned int section_index;
    SIZE_T index;
    DWORD old_protection;

    if (streamer_portrait_scale_limit_hooked) {
        return TRUE;
    }
    if (!base) {
        return FALSE;
    }
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    sections = IMAGE_FIRST_SECTION(nt);
    for (section_index = 0;
         section_index < nt->FileHeader.NumberOfSections;
         ++section_index) {
        if (memcmp(sections[section_index].Name, ".text", 5) == 0) {
            text = &sections[section_index];
            break;
        }
    }
    if (!text || text->Misc.VirtualSize < sizeof(scale_copy_pattern)) {
        return FALSE;
    }
    for (index = 0;
         index + sizeof(scale_copy_pattern) <= text->Misc.VirtualSize;
         ++index) {
        unsigned char *current = base + text->VirtualAddress + index;
        if (
            !bytes_equal(
                current, scale_copy_pattern, height_source_offset
            ) ||
            !bytes_equal(
                current + height_source_offset + 1u,
                scale_copy_pattern + height_source_offset + 1u,
                sizeof(scale_copy_pattern) - height_source_offset - 1u
            ) ||
            (current[height_source_offset] != 0x34u &&
             current[height_source_offset] != 0x30u)
        ) {
            continue;
        }
        candidate = current;
        ++candidate_count;
    }
    InterlockedExchange(
        &frame_portrait_scale_limit_candidates, (LONG)candidate_count
    );
    if (candidate_count != 1u || !candidate) {
        return FALSE;
    }
    if (candidate[height_source_offset] == 0x34u) {
        if (!VirtualProtect(
                candidate + height_source_offset,
                1u,
                PAGE_EXECUTE_READWRITE,
                &old_protection
            )) {
            return FALSE;
        }
        candidate[height_source_offset] = 0x30u;
        VirtualProtect(
            candidate + height_source_offset,
            1u,
            old_protection,
            &old_protection
        );
        FlushInstructionCache(
            GetCurrentProcess(), candidate + height_source_offset, 1u
        );
        InterlockedIncrement(&frame_portrait_scale_limit_patches);
    }
    streamer_portrait_scale_limit_hooked = TRUE;
    return TRUE;
}

/*
 * The capture object copies the already-built maximum rectangle once more
 * when a remote video track is created.  The input hook is loaded after the
 * first configuration object may have been constructed, so patch this later,
 * per-track copy as well.  This is the stage that determines the dimensions
 * eventually passed to StretchBlt and the encoder.
 */
static BOOL patch_streamer_portrait_capture_limit(HMODULE module) {
    static const unsigned char capture_copy_pattern[] = {
        0x41, 0x8b, 0x85, 0xc0, 0x00, 0x00, 0x00,
        0x89, 0x86, 0x24, 0x01, 0x00, 0x00,
        0x41, 0x8b, 0x85, 0xc4, 0x00, 0x00, 0x00,
        0x89, 0x86, 0x28, 0x01, 0x00, 0x00
    };
    const SIZE_T height_source_offset = 16u;
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    IMAGE_SECTION_HEADER *text = NULL;
    unsigned char *candidate = NULL;
    unsigned int candidate_count = 0;
    unsigned int section_index;
    SIZE_T index;
    DWORD old_protection;

    if (streamer_portrait_capture_limit_hooked) {
        return TRUE;
    }
    if (!base) {
        return FALSE;
    }
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    sections = IMAGE_FIRST_SECTION(nt);
    for (section_index = 0;
         section_index < nt->FileHeader.NumberOfSections;
         ++section_index) {
        if (memcmp(sections[section_index].Name, ".text", 5) == 0) {
            text = &sections[section_index];
            break;
        }
    }
    if (!text || text->Misc.VirtualSize < sizeof(capture_copy_pattern)) {
        return FALSE;
    }
    for (index = 0;
         index + sizeof(capture_copy_pattern) <= text->Misc.VirtualSize;
         ++index) {
        unsigned char *current = base + text->VirtualAddress + index;
        if (
            !bytes_equal(
                current, capture_copy_pattern, height_source_offset
            ) ||
            !bytes_equal(
                current + height_source_offset + 1u,
                capture_copy_pattern + height_source_offset + 1u,
                sizeof(capture_copy_pattern) - height_source_offset - 1u
            ) ||
            (current[height_source_offset] != 0xc4u &&
             current[height_source_offset] != 0xc0u)
        ) {
            continue;
        }
        candidate = current;
        ++candidate_count;
    }
    InterlockedExchange(
        &frame_portrait_capture_limit_candidates, (LONG)candidate_count
    );
    if (candidate_count != 1u || !candidate) {
        return FALSE;
    }
    if (candidate[height_source_offset] == 0xc4u) {
        if (!VirtualProtect(
                candidate + height_source_offset,
                1u,
                PAGE_EXECUTE_READWRITE,
                &old_protection
            )) {
            return FALSE;
        }
        candidate[height_source_offset] = 0xc0u;
        VirtualProtect(
            candidate + height_source_offset,
            1u,
            old_protection,
            &old_protection
        );
        FlushInstructionCache(
            GetCurrentProcess(), candidate + height_source_offset, 1u
        );
        InterlockedIncrement(&frame_portrait_capture_limit_patches);
    }
    streamer_portrait_capture_limit_hooked = TRUE;
    return TRUE;
}

/*
 * A live session update writes the negotiated maximum rectangle into the
 * capture object again and immediately calls its size setter.  This happens
 * after both construction-time copies above, so leaving it untouched restores
 * the 2160 landscape height ceiling and recreates the 1216x2160 portrait DIB.
 * Make both the change detector and the applied height use the width ceiling.
 */
static BOOL patch_streamer_portrait_update_limit(HMODULE module) {
    static const unsigned char update_pattern[] = {
        0x8b, 0x86, 0x28, 0x01, 0x00, 0x00,
        0x41, 0x3b, 0x86, 0xc4, 0x00, 0x00, 0x00,
        0x75, 0x12,
        0x45, 0x8b, 0x86, 0xc0, 0x00, 0x00, 0x00,
        0x44, 0x39, 0x86, 0x24, 0x01, 0x00, 0x00,
        0x75, 0x09, 0xeb, 0x32,
        0x45, 0x8b, 0x86, 0xc0, 0x00, 0x00, 0x00,
        0x44, 0x89, 0x86, 0x24, 0x01, 0x00, 0x00,
        0x41, 0x8b, 0x96, 0xc4, 0x00, 0x00, 0x00,
        0x89, 0x96, 0x28, 0x01, 0x00, 0x00
    };
    const SIZE_T compare_height_offset = 9u;
    const SIZE_T apply_height_offset = 50u;
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    IMAGE_SECTION_HEADER *text = NULL;
    unsigned char *candidate = NULL;
    unsigned int candidate_count = 0;
    unsigned int section_index;
    SIZE_T index;
    DWORD old_protection;

    if (streamer_portrait_update_limit_hooked) {
        return TRUE;
    }
    if (!base) {
        return FALSE;
    }
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    sections = IMAGE_FIRST_SECTION(nt);
    for (section_index = 0;
         section_index < nt->FileHeader.NumberOfSections;
         ++section_index) {
        if (memcmp(sections[section_index].Name, ".text", 5) == 0) {
            text = &sections[section_index];
            break;
        }
    }
    if (!text || text->Misc.VirtualSize < sizeof(update_pattern)) {
        return FALSE;
    }
    for (index = 0;
         index + sizeof(update_pattern) <= text->Misc.VirtualSize;
         ++index) {
        unsigned char *current = base + text->VirtualAddress + index;
        if (
            !bytes_equal(current, update_pattern, compare_height_offset) ||
            !bytes_equal(
                current + compare_height_offset + 1u,
                update_pattern + compare_height_offset + 1u,
                apply_height_offset - compare_height_offset - 1u
            ) ||
            !bytes_equal(
                current + apply_height_offset + 1u,
                update_pattern + apply_height_offset + 1u,
                sizeof(update_pattern) - apply_height_offset - 1u
            ) ||
            (current[compare_height_offset] != 0xc4u &&
             current[compare_height_offset] != 0xc0u) ||
            (current[apply_height_offset] != 0xc4u &&
             current[apply_height_offset] != 0xc0u)
        ) {
            continue;
        }
        candidate = current;
        ++candidate_count;
    }
    InterlockedExchange(
        &frame_portrait_update_limit_candidates, (LONG)candidate_count
    );
    if (candidate_count != 1u || !candidate) {
        return FALSE;
    }
    if (
        candidate[compare_height_offset] == 0xc4u ||
        candidate[apply_height_offset] == 0xc4u
    ) {
        if (!VirtualProtect(
                candidate + compare_height_offset,
                apply_height_offset - compare_height_offset + 1u,
                PAGE_EXECUTE_READWRITE,
                &old_protection
            )) {
            return FALSE;
        }
        if (candidate[compare_height_offset] == 0xc4u) {
            candidate[compare_height_offset] = 0xc0u;
            InterlockedIncrement(&frame_portrait_update_limit_patches);
        }
        if (candidate[apply_height_offset] == 0xc4u) {
            candidate[apply_height_offset] = 0xc0u;
            InterlockedIncrement(&frame_portrait_update_limit_patches);
        }
        VirtualProtect(
            candidate + compare_height_offset,
            apply_height_offset - compare_height_offset + 1u,
            old_protection,
            &old_protection
        );
        FlushInstructionCache(
            GetCurrentProcess(),
            candidate + compare_height_offset,
            apply_height_offset - compare_height_offset + 1u
        );
    }
    streamer_portrait_update_limit_hooked =
        candidate[compare_height_offset] == 0xc0u &&
        candidate[apply_height_offset] == 0xc0u;
    return streamer_portrait_update_limit_hooked;
}

/*
 * The final GDI capture scaler compares the monitor rectangle against a
 * landscape maximum rectangle.  Its height calculation reads [rect+0x94] -
 * [rect+0x8c], which is 2160 even when the source monitor is 1440x2560.  It
 * then preserves aspect ratio and creates a 1216x2160 DIB.  Use the maximum
 * rectangle's width for both bounds in the comparison and ratio paths.  The
 * negotiated width is the direction-neutral 3840 ceiling, so landscape
 * behavior is unchanged while a 2560-pixel portrait height stays native.
 */
static BOOL patch_streamer_portrait_scaler(HMODULE module) {
    static const unsigned char scaler_pattern[] = {
        0x8b, 0x97, 0x90, 0x00, 0x00, 0x00,
        0x2b, 0x97, 0x88, 0x00, 0x00, 0x00,
        0x44, 0x8b, 0x45, 0x88, 0x44, 0x2b, 0x45, 0x80,
        0x44, 0x8b, 0x4d, 0x8c, 0x44, 0x8b, 0x55, 0x84,
        0x44, 0x3b, 0xc2, 0x7f, 0x50,
        0x8b, 0x8f, 0x94, 0x00, 0x00, 0x00,
        0x2b, 0x8f, 0x8c, 0x00, 0x00, 0x00,
        0x41, 0x8b, 0xc1, 0x41, 0x2b, 0xc2, 0x3b, 0xc1, 0x7f, 0x3a,
        0x0f, 0x28, 0x75, 0x80, 0x66, 0x0f, 0x6f, 0xc6,
        0x66, 0x0f, 0x73, 0xd8, 0x0c, 0x66, 0x0f, 0x7e, 0xc3,
        0x66, 0x0f, 0x6f, 0xce, 0x66, 0x0f, 0x73, 0xd9, 0x08,
        0x66, 0x0f, 0x7e, 0xce, 0x89, 0x74, 0x24, 0x40,
        0x66, 0x0f, 0x6f, 0xc6, 0x66, 0x0f, 0x73, 0xd8, 0x04,
        0x66, 0x41, 0x0f, 0x7e, 0xc7, 0x66, 0x41, 0x0f, 0x7e, 0xf6,
        0xe9, 0x8c, 0x00, 0x00, 0x00,
        0x66, 0x41, 0x0f, 0x6e, 0xc0, 0x0f, 0x5b, 0xc0,
        0x66, 0x0f, 0x6e, 0xca, 0x0f, 0x5b, 0xc9,
        0xf3, 0x0f, 0x5e, 0xc8, 0x45, 0x2b, 0xca,
        0x66, 0x45, 0x0f, 0x6e, 0xc1, 0x45, 0x0f, 0x5b, 0xc0,
        0x8b, 0x87, 0x94, 0x00, 0x00, 0x00,
        0x2b, 0x87, 0x8c, 0x00, 0x00, 0x00,
        0x66, 0x0f, 0x6e, 0xf8, 0x0f, 0x5b, 0xff, 0xf3
    };
    static const SIZE_T field_offsets[] = {35u, 41u, 146u, 152u};
    static const unsigned char original_fields[] = {
        0x94u, 0x8cu, 0x94u, 0x8cu
    };
    static const unsigned char patched_fields[] = {
        0x90u, 0x88u, 0x90u, 0x88u
    };
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    IMAGE_SECTION_HEADER *text = NULL;
    unsigned char *candidate = NULL;
    unsigned int candidate_count = 0;
    unsigned int section_index;
    SIZE_T index;
    SIZE_T field_index;
    SIZE_T segment_start;
    BOOL matches;
    DWORD old_protection;

    if (streamer_portrait_scaler_hooked) {
        return TRUE;
    }
    if (!base) {
        return FALSE;
    }
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    sections = IMAGE_FIRST_SECTION(nt);
    for (section_index = 0;
         section_index < nt->FileHeader.NumberOfSections;
         ++section_index) {
        if (memcmp(sections[section_index].Name, ".text", 5) == 0) {
            text = &sections[section_index];
            break;
        }
    }
    if (!text || text->Misc.VirtualSize < sizeof(scaler_pattern)) {
        return FALSE;
    }
    for (index = 0;
         index + sizeof(scaler_pattern) <= text->Misc.VirtualSize;
         ++index) {
        unsigned char *current = base + text->VirtualAddress + index;
        matches = TRUE;
        segment_start = 0u;
        for (field_index = 0;
             field_index < sizeof(field_offsets) / sizeof(field_offsets[0]);
             ++field_index) {
            SIZE_T field_offset = field_offsets[field_index];
            if (
                !bytes_equal(
                    current + segment_start,
                    scaler_pattern + segment_start,
                    field_offset - segment_start
                ) ||
                (current[field_offset] != original_fields[field_index] &&
                 current[field_offset] != patched_fields[field_index])
            ) {
                matches = FALSE;
                break;
            }
            segment_start = field_offset + 1u;
        }
        if (
            !matches ||
            !bytes_equal(
                current + segment_start,
                scaler_pattern + segment_start,
                sizeof(scaler_pattern) - segment_start
            )
        ) {
            continue;
        }
        candidate = current;
        ++candidate_count;
    }
    InterlockedExchange(
        &frame_portrait_scaler_candidates, (LONG)candidate_count
    );
    if (candidate_count != 1u || !candidate) {
        return FALSE;
    }
    matches = TRUE;
    for (field_index = 0;
         field_index < sizeof(field_offsets) / sizeof(field_offsets[0]);
         ++field_index) {
        if (candidate[field_offsets[field_index]] != patched_fields[field_index]) {
            matches = FALSE;
            break;
        }
    }
    if (!matches) {
        SIZE_T first_offset = field_offsets[0];
        SIZE_T last_offset = field_offsets[
            sizeof(field_offsets) / sizeof(field_offsets[0]) - 1u
        ];
        if (!VirtualProtect(
                candidate + first_offset,
                last_offset - first_offset + 1u,
                PAGE_EXECUTE_READWRITE,
                &old_protection
            )) {
            return FALSE;
        }
        for (field_index = 0;
             field_index < sizeof(field_offsets) / sizeof(field_offsets[0]);
             ++field_index) {
            if (
                candidate[field_offsets[field_index]] ==
                original_fields[field_index]
            ) {
                candidate[field_offsets[field_index]] =
                    patched_fields[field_index];
                InterlockedIncrement(&frame_portrait_scaler_patches);
            }
        }
        VirtualProtect(
            candidate + first_offset,
            last_offset - first_offset + 1u,
            old_protection,
            &old_protection
        );
        FlushInstructionCache(
            GetCurrentProcess(),
            candidate + first_offset,
            last_offset - first_offset + 1u
        );
    }
    streamer_portrait_scaler_hooked = TRUE;
    for (field_index = 0;
         field_index < sizeof(field_offsets) / sizeof(field_offsets[0]);
         ++field_index) {
        if (candidate[field_offsets[field_index]] != patched_fields[field_index]) {
            streamer_portrait_scaler_hooked = FALSE;
            break;
        }
    }
    return streamer_portrait_scaler_hooked;
}

struct msvc_complete_object_locator_x64 {
    uint32_t signature;
    uint32_t object_offset;
    uint32_t constructor_displacement;
    uint32_t type_descriptor_rva;
    uint32_t class_descriptor_rva;
    uint32_t self_rva;
};

static BOOL address_in_image(
    const unsigned char *base,
    SIZE_T image_size,
    const void *address,
    SIZE_T size
) {
    const unsigned char *value = (const unsigned char *)address;

    if (!base || !address || size > image_size || value < base) {
        return FALSE;
    }
    return (SIZE_T)(value - base) <= image_size - size;
}

static void **streamer_frame_vtable_start(
    const unsigned char *base,
    const IMAGE_SECTION_HEADER *text,
    const IMAGE_SECTION_HEADER *rdata,
    void **adapter_slot
) {
    unsigned char *rdata_start;
    void **start = adapter_slot;
    unsigned int methods = 0;

    if (!base || !text || !rdata || !adapter_slot) {
        return NULL;
    }
    rdata_start = (unsigned char *)base + rdata->VirtualAddress;
    while (
        methods < 128u &&
        (unsigned char *)start >= rdata_start + sizeof(*start) &&
        address_in_image_section(base, text, start[-1], 1u)
    ) {
        --start;
        ++methods;
    }
    return start;
}

static const char *streamer_vtable_rtti_class_name(
    const unsigned char *base,
    const IMAGE_NT_HEADERS64 *nt,
    const IMAGE_SECTION_HEADER *rdata,
    void **vtable
) {
    struct msvc_complete_object_locator_x64 locator;
    const struct msvc_complete_object_locator_x64 *locator_address;
    const unsigned char *type_descriptor;
    const char *name;
    SIZE_T image_size;
    SIZE_T length;

    if (!base || !nt || !rdata || !vtable) {
        return NULL;
    }
    if (!address_in_image_section(
            base, rdata, vtable - 1, sizeof(void *)
        )) {
        return NULL;
    }
    locator_address = (const struct msvc_complete_object_locator_x64 *)
        vtable[-1];
    if (!address_in_image_section(
            base, rdata, locator_address, sizeof(locator)
        )) {
        return NULL;
    }
    CopyMemory(&locator, locator_address, sizeof(locator));
    if (
        locator.signature != 1u ||
        base + locator.self_rva != (const unsigned char *)locator_address
    ) {
        return NULL;
    }
    image_size = nt->OptionalHeader.SizeOfImage;
    type_descriptor = base + locator.type_descriptor_rva;
    if (!address_in_image(
            base,
            image_size,
            type_descriptor,
            2u * sizeof(void *) + 4u
        )) {
        return NULL;
    }
    name = (const char *)(type_descriptor + 2u * sizeof(void *));
    if (name[0] != '.' || name[1] != '?' || name[2] != 'A') {
        return NULL;
    }
    for (length = 3u; length < 256u; ++length) {
        if (!address_in_image(base, image_size, name, length + 1u)) {
            return NULL;
        }
        if (!name[length]) {
            return name;
        }
    }
    return NULL;
}

static unsigned int streamer_adapter_constructor_references(
    const unsigned char *base,
    const IMAGE_SECTION_HEADER *text,
    void **vtable
) {
    SIZE_T text_span;
    SIZE_T index;
    unsigned int references = 0;

    if (!base || !text || !vtable) {
        return 0u;
    }
    text_span = text->Misc.VirtualSize;
    if (text_span < text->SizeOfRawData) {
        text_span = text->SizeOfRawData;
    }
    for (index = 0; index + 104u <= text_span; ++index) {
        const unsigned char *instruction =
            base + text->VirtualAddress + index;
        int32_t displacement;
        const unsigned char *target;
        SIZE_T lookahead;

        if (
            instruction[0] != 0x48 || instruction[1] != 0x8d ||
            instruction[2] != 0x05 || instruction[7] != 0x48 ||
            instruction[8] != 0x89 || instruction[9] != 0x06
        ) {
            continue;
        }
        CopyMemory(&displacement, instruction + 3, sizeof(displacement));
        target = instruction + 7 + displacement;
        if (target != (const unsigned char *)vtable) {
            continue;
        }
        for (lookahead = 10u; lookahead + 3u <= 104u; ++lookahead) {
            const unsigned char *store = instruction + lookahead;
            if (
                store[0] == 0x89 && (store[1] & 0xc7u) == 0x46u &&
                store[2] == 0x44
            ) {
                ++references;
                break;
            }
        }
    }
    return references;
}

static BOOL streamer_rtti_name_is_video_frame(const char *name) {
    if (!name) {
        return TRUE;
    }
    return
        strstr(name, "Frame") != NULL &&
        strstr(name, "@streamer@@") != NULL &&
        strstr(name, "@webrtc@@") == NULL;
}

/*
 * UU's GDI RawVideoFrame reports adapter LUID 0 even when DXVK exposes a real
 * NVIDIA adapter.  VideoEncoderFactory therefore refuses its matching NVENC
 * entry and deliberately falls across adapters to OpenH264.  Locate the GDI
 * frame vtable from its constructor rather than a version-specific RVA, then
 * override only the adapter-id virtual method.  The constructor/vtable
 * fingerprints are also checked offline by uu-remote-encoder-policy before an
 * updated official client is allowed to enable the bridge.
 */
static BOOL patch_streamer_gdi_frame_adapter(HMODULE module) {
    static const unsigned char constructor_prefix[] = {
        0xe8, 0, 0, 0, 0, 0x90, 0x48, 0x8d, 0x05
    };
    static const unsigned char constructor_suffix[] = {
        0x49, 0x89, 0x07, 0x49, 0x89, 0xb7, 0xa8, 0, 0, 0
    };
    static const unsigned char returns_true_dword[] = {
        0xb8, 0x01, 0, 0, 0, 0xc3
    };
    static const unsigned char returns_true_byte[] = {0xb0, 0x01, 0xc3};
    static const unsigned char has_cursor[] = {
        0x48, 0x83, 0xb9, 0xa8, 0, 0, 0, 0,
        0x0f, 0x95, 0xc0, 0xc3
    };
    static const unsigned char returns_zero[] = {0x33, 0xc0, 0xc3};
    static const unsigned char base_adapter_getter_code[] = {
        0x8b, 0x41, 0x44, 0xc3
    };
    static const unsigned char encoder_path_accessor_prefix[] = {
        0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec,
        0x20, 0x48, 0x8b, 0xf9, 0x48, 0x8b, 0x59, 0x28
    };
    static const unsigned char encoder_path_adapter_call[] = {
        0x48, 0x8b, 0x00, 0xff, 0x50, 0x30
    };
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    IMAGE_SECTION_HEADER *text = NULL;
    IMAGE_SECTION_HEADER *rdata = NULL;
    void **candidate_slot = NULL;
    void **base_slots[16];
    void **wrapper_slot = NULL;
    void *base_adapter_getter = NULL;
    void *encoder_path_accessor = NULL;
    unsigned int candidate_count = 0;
    unsigned int base_getter_count = 0;
    unsigned int base_slot_count = 0;
    unsigned int getter_slot_count = 0;
    unsigned int long_slot_count = 0;
    unsigned int wrapper_slot_count = 0;
    unsigned int rtti_named_count = 0;
    unsigned int encoder_path_accessor_count = 0;
    unsigned int section_index;
    SIZE_T index;

    if (!configured_frame_adapter_luid) {
        return TRUE;
    }
    if (streamer_frame_adapter_hooked) {
        return TRUE;
    }
    if (!base) {
        return FALSE;
    }
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return FALSE;
    }
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
    ) {
        return FALSE;
    }
    sections = IMAGE_FIRST_SECTION(nt);
    for (section_index = 0;
         section_index < nt->FileHeader.NumberOfSections;
         ++section_index) {
        if (memcmp(sections[section_index].Name, ".text", 5) == 0) {
            text = &sections[section_index];
        } else if (
            memcmp(sections[section_index].Name, ".rdata", 6) == 0
        ) {
            rdata = &sections[section_index];
        }
    }
    if (!text || !rdata || text->Misc.VirtualSize < 23u) {
        return FALSE;
    }
    for (index = 0;
         index + sizeof(base_adapter_getter_code) <= text->Misc.VirtualSize;
         ++index) {
        unsigned char *function = base + text->VirtualAddress + index;
        if (bytes_equal(
                function,
                base_adapter_getter_code,
                sizeof(base_adapter_getter_code)
            )) {
            base_adapter_getter = function;
            ++base_getter_count;
        }
    }
    if (base_getter_count != 1u || !base_adapter_getter) {
        return FALSE;
    }
    for (index = 0;
         index + 96u <= text->Misc.VirtualSize;
         ++index) {
        unsigned char *function = base + text->VirtualAddress + index;
        SIZE_T method_offset;

        if (!bytes_equal(
                function,
                encoder_path_accessor_prefix,
                sizeof(encoder_path_accessor_prefix)
            )) {
            continue;
        }
        for (method_offset = sizeof(encoder_path_accessor_prefix);
             method_offset + sizeof(encoder_path_adapter_call) <= 96u;
             ++method_offset) {
            if (bytes_equal(
                    function + method_offset,
                    encoder_path_adapter_call,
                    sizeof(encoder_path_adapter_call)
                )) {
                encoder_path_accessor = function;
                ++encoder_path_accessor_count;
                break;
            }
        }
    }
    for (index = 0; index + 23u <= text->Misc.VirtualSize; ++index) {
        unsigned char *instruction = base + text->VirtualAddress + index;
        int32_t displacement;
        void **vtable;

        if (
            instruction[0] != constructor_prefix[0] ||
            !bytes_equal(
                instruction + 5, constructor_prefix + 5,
                sizeof(constructor_prefix) - 5u
            ) ||
            !bytes_equal(
                instruction + 13,
                constructor_suffix,
                sizeof(constructor_suffix)
            )
        ) {
            continue;
        }
        CopyMemory(&displacement, instruction + 9, sizeof(displacement));
        vtable = (void **)(instruction + 13 + displacement);
        if (!address_in_image_section(
                base, rdata, vtable, 7u * sizeof(*vtable)
            ) ||
            !address_in_image_section(base, text, vtable[1], 6u) ||
            !address_in_image_section(base, text, vtable[2], 3u) ||
            !address_in_image_section(base, text, vtable[3], 12u) ||
            !address_in_image_section(base, text, vtable[5], 3u) ||
            vtable[4] != vtable[5] ||
            !bytes_equal(vtable[1], returns_true_dword, 6u) ||
            !bytes_equal(vtable[2], returns_true_byte, 3u) ||
            !bytes_equal(vtable[3], has_cursor, 12u) ||
            !bytes_equal(vtable[5], returns_zero, 3u)
        ) {
            continue;
        }
        candidate_slot = &vtable[5];
        ++candidate_count;
    }
    if (candidate_count != 1u || !candidate_slot) {
        return FALSE;
    }
    {
        SIZE_T rdata_span = rdata->Misc.VirtualSize;
        if (rdata_span < rdata->SizeOfRawData) {
            rdata_span = rdata->SizeOfRawData;
        }
        for (index = 0u;
             index + 2u * sizeof(void *) <= rdata_span;
             index += sizeof(void *)) {
            void **slot = (void **)(base + rdata->VirtualAddress + index);
            BOOL preceding_are_text = TRUE;
            unsigned int method_index;

            if (*slot != base_adapter_getter) {
                continue;
            }
            ++getter_slot_count;
            /*
             * Three RawVideoFrame base tables end at this getter.  Four much
             * longer tables also reuse it in UU 4.37, but live tracing proved
             * that none is the adapter path consumed by VideoEncoderFactory.
             * Keep classifying these tables as a version guard, but never
             * patch them: doing so corrupts WebRTC media negotiation.
             */
            if (address_in_image_section(base, text, slot[1], 1u)) {
                void **vtable = streamer_frame_vtable_start(
                    base, text, rdata, slot
                );
                const char *rtti_name = streamer_vtable_rtti_class_name(
                    base, nt, rdata, vtable
                );
                unsigned int constructor_references =
                    streamer_adapter_constructor_references(
                        base, text, vtable
                    );

                ++long_slot_count;
                if (rtti_name) {
                    ++rtti_named_count;
                }
                if (
                    constructor_references &&
                    streamer_rtti_name_is_video_frame(rtti_name)
                ) {
                    wrapper_slot = slot;
                    ++wrapper_slot_count;
                }
                continue;
            }
            if (index < 5u * sizeof(void *)) {
                continue;
            }
            for (method_index = 1u; method_index <= 5u; ++method_index) {
                if (!address_in_image_section(
                        base, text, *(slot - method_index), 1u
                    )) {
                    preceding_are_text = FALSE;
                    break;
                }
            }
            if (!preceding_are_text) {
                continue;
            }
            if (base_slot_count >= sizeof(base_slots) / sizeof(base_slots[0])) {
                return FALSE;
            }
            base_slots[base_slot_count] = slot;
            ++base_slot_count;
        }
    }
    InterlockedExchange(
        &frame_adapter_long_candidates, (LONG)long_slot_count
    );
    InterlockedExchange(
        &frame_adapter_wrapper_candidates, (LONG)wrapper_slot_count
    );
    InterlockedExchange(
        &frame_adapter_rtti_named_candidates, (LONG)rtti_named_count
    );
    InterlockedExchange(
        &frame_adapter_encoder_path_candidates,
        (LONG)encoder_path_accessor_count
    );
    if (
        !base_slot_count || !long_slot_count || wrapper_slot_count != 1u ||
        !wrapper_slot || getter_slot_count != base_slot_count + long_slot_count ||
        encoder_path_accessor_count != 1u || !encoder_path_accessor
    ) {
        return FALSE;
    }
    if (!patch_frame_adapter_slot(
            candidate_slot,
            (void *)hooked_frame_adapter_id,
            &original_frame_adapter_id
        )) {
        return FALSE;
    }
    for (section_index = 0u; section_index < base_slot_count; ++section_index) {
        if (!patch_frame_adapter_slot(
                base_slots[section_index],
                (void *)hooked_frame_adapter_id,
                NULL
            )) {
            return FALSE;
        }
    }
    /*
     * The encoder does not call the RawVideoFrame getter directly.  Its
     * queued-frame wrapper locks the frame store and invokes virtual slot
     * +0x30 on the concrete frame.  This uniquely-shaped accessor is the
     * value that appears in "frame from adapter {}" and is therefore the
     * narrowest safe place to supply DXGI's adapter LUID.
     */
    if (!patch_frame_adapter_accessor(encoder_path_accessor)) {
        return FALSE;
    }
    streamer_wrapper_frame_adapter_hooked = TRUE;
    streamer_frame_adapter_hooked = TRUE;
    return TRUE;
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

static void event_loop_message_fingerprint(
    struct event_loop_message_fingerprint *fingerprint,
    const MSG *message
) {
    ZeroMemory(fingerprint, sizeof(*fingerprint));
    if (!message) {
        return;
    }
    fingerprint->window = message->hwnd;
    fingerprint->message = message->message;
    fingerprint->wparam = message->wParam;
    fingerprint->lparam = message->lParam;
    fingerprint->time = message->time;
    fingerprint->point = message->pt;
}

static BOOL event_loop_message_fingerprint_equal(
    const struct event_loop_message_fingerprint *left,
    const struct event_loop_message_fingerprint *right
) {
    return
        left->window == right->window &&
        left->message == right->message &&
        left->wparam == right->wparam &&
        left->lparam == right->lparam &&
        left->time == right->time &&
        left->point.x == right->point.x &&
        left->point.y == right->point.y;
}

static BOOL event_loop_message_belongs_to_process(const MSG *message) {
    DWORD pid = 0;

    if (!message || !message->hwnd) {
        return FALSE;
    }
    GetWindowThreadProcessId(message->hwnd, &pid);
    return pid == GetCurrentProcessId();
}

static void event_loop_reset_repeated_message_probe(
    struct event_loop_thread_state *state
) {
    if (!state) {
        return;
    }
    ZeroMemory(
        &state->last_removed_message,
        sizeof(state->last_removed_message)
    );
    state->repeated_removed_messages = 0;
    state->repeated_message_burst_started = 0;
}

static BOOL event_loop_sticky_null_fingerprint_should_filter(
    struct event_loop_thread_state *state,
    const struct event_loop_message_fingerprint *current,
    BOOL belongs_to_process,
    ULONGLONG now
) {
    if (!state || !current) {
        return FALSE;
    }
    if (state->sticky_null_active) {
        if (event_loop_message_fingerprint_equal(
                &state->sticky_null_message, current
            )) {
            return TRUE;
        }
        state->sticky_null_active = FALSE;
        ZeroMemory(
            &state->sticky_null_message,
            sizeof(state->sticky_null_message)
        );
        event_loop_reset_repeated_message_probe(state);
    }
    if (
        current->message != WM_NULL ||
        !belongs_to_process
    ) {
        event_loop_reset_repeated_message_probe(state);
        return FALSE;
    }
    if (
        !state->repeated_message_burst_started ||
        now < state->repeated_message_burst_started ||
        now - state->repeated_message_burst_started >
            EVENT_LOOP_STICKY_NULL_WINDOW_MS ||
        !event_loop_message_fingerprint_equal(
            &state->last_removed_message, current
        )
    ) {
        state->last_removed_message = *current;
        state->repeated_removed_messages = 1;
        state->repeated_message_burst_started = now;
        return FALSE;
    }
    ++state->repeated_removed_messages;
    if (
        state->repeated_removed_messages <
        EVENT_LOOP_STICKY_NULL_THRESHOLD
    ) {
        return FALSE;
    }
    state->sticky_null_active = TRUE;
    state->sticky_null_message = *current;
    event_loop_reset_repeated_message_probe(state);
    return TRUE;
}

static BOOL event_loop_sticky_null_should_filter(
    struct event_loop_thread_state *state,
    const MSG *message,
    ULONGLONG now
) {
    struct event_loop_message_fingerprint current;
    BOOL was_active;
    BOOL should_filter;

    if (!state || !message) {
        return FALSE;
    }
    event_loop_message_fingerprint(&current, message);
    was_active = state->sticky_null_active;
    should_filter = event_loop_sticky_null_fingerprint_should_filter(
        state,
        &current,
        event_loop_message_belongs_to_process(message),
        now
    );
    if (should_filter && !was_active && state->sticky_null_active) {
        InterlockedIncrement(&event_loop_sticky_nulls_detected);
        InterlockedExchange64(
            &event_loop_last_sticky_null, (LONG64)now
        );
    }
    return should_filter;
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
            state->sticky_null_active = FALSE;
            ZeroMemory(
                &state->sticky_null_message,
                sizeof(state->sticky_null_message)
            );
            event_loop_reset_repeated_message_probe(state);
        }
        return FALSE;
    }
    if (
        state &&
        message &&
        event_loop_sticky_null_should_filter(
            state, message, GetTickCount64()
        )
    ) {
        InterlockedIncrement(&event_loop_sticky_nulls_filtered);
        result = original_peek_message_w(
            message,
            NULL,
            WM_NULL + 1u,
            0xffffffffu,
            remove_message
        );
        if (!result) {
            state->last_peek_empty = TRUE;
            return FALSE;
        }
    }
    event_loop_reset_false_wakes(state);
    InterlockedIncrement64(&event_loop_messages_dequeued);
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
        state->sticky_null_active &&
        result == WAIT_OBJECT_0 + handle_count
    ) {
        InterlockedIncrement(&event_loop_sticky_null_wake_breaks);
        event_loop_reset_false_wakes(state);
        Sleep(EVENT_LOOP_STICKY_NULL_BACKOFF_MS);
        return WAIT_TIMEOUT;
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
    ULONGLONG now = GetTickCount64();
    ULONGLONG last_ack = (ULONGLONG)InterlockedCompareExchange64(
        &ui_health_last_ack_tick, 0, 0
    );
    LONG64 last_ack_age = -1;

    if (last_ack && now >= last_ack) {
        last_ack_age = (LONG64)(now - last_ack);
    }

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
        "arbitration_posts=%ld\r\n"
        "arbitration_coalesced=%ld\r\n"
        "state_changes=%ld\r\n"
        "[home_window]\r\n"
        "hidden_by_user=%ld\r\n"
        "reopen_blocked=%ld\r\n"
        "show_authorized=%ld\r\n"
        "repaint_pulses=%ld\r\n"
        "[event_loop]\r\n"
        "mode=qt-wine-sticky-message-guard\r\n"
        "empty_queue_wakes=%ld\r\n"
        "guard_breaks=%ld\r\n"
        "messages_dequeued=%lld\r\n"
        "posted_forwarded=%ld\r\n"
        "posted_coalesced=%ld\r\n"
        "sticky_nulls_detected=%ld\r\n"
        "sticky_nulls_filtered=%ld\r\n"
        "sticky_null_wake_breaks=%ld\r\n"
        "last_guard_break_tick=%lld\r\n"
        "last_sticky_null_tick=%lld\r\n"
        "[ui_health]\r\n"
        "pings_sent=%ld\r\n"
        "pings_acked=%ld\r\n"
        "timeouts=%ld\r\n"
        "recovery_requests=%ld\r\n"
        "target_generation=%ld\r\n"
        "window_invalidations=%ld\r\n"
        "no_livelock_suppressions=%ld\r\n"
        "consecutive_timeouts=%ld\r\n"
        "hard_stalls_detected=%ld\r\n"
        "last_ack_tick=%lld\r\n"
        "last_ack_age_ms=%lld\r\n"
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
        InterlockedCompareExchange(&focus_arbitration_posts, 0, 0),
        InterlockedCompareExchange(&focus_arbitration_coalesced, 0, 0),
        InterlockedCompareExchange(&focus_window_state_changes, 0, 0),
        InterlockedCompareExchange(&focus_home_hidden_by_user, 0, 0),
        InterlockedCompareExchange(&focus_home_reopen_blocked, 0, 0),
        InterlockedCompareExchange(&focus_home_show_authorized, 0, 0),
        InterlockedCompareExchange(&focus_home_repaint_pulses, 0, 0),
        InterlockedCompareExchange(&event_loop_empty_queue_wakes, 0, 0),
        InterlockedCompareExchange(&event_loop_guard_breaks, 0, 0),
        (long long)InterlockedCompareExchange64(
            &event_loop_messages_dequeued, 0, 0
        ),
        InterlockedCompareExchange(&event_loop_posted_forwarded, 0, 0),
        InterlockedCompareExchange(&event_loop_posted_coalesced, 0, 0),
        InterlockedCompareExchange(&event_loop_sticky_nulls_detected, 0, 0),
        InterlockedCompareExchange(&event_loop_sticky_nulls_filtered, 0, 0),
        InterlockedCompareExchange(
            &event_loop_sticky_null_wake_breaks, 0, 0
        ),
        (long long)InterlockedCompareExchange64(
            &event_loop_last_guard_break, 0, 0
        ),
        (long long)InterlockedCompareExchange64(
            &event_loop_last_sticky_null, 0, 0
        ),
        InterlockedCompareExchange(&ui_health_pings_sent, 0, 0),
        InterlockedCompareExchange(&ui_health_pings_acked, 0, 0),
        InterlockedCompareExchange(&ui_health_timeouts, 0, 0),
        InterlockedCompareExchange(&ui_health_recovery_requests, 0, 0),
        InterlockedCompareExchange(&ui_health_stall_generation, 0, 0),
        InterlockedCompareExchange(
            &ui_health_window_invalidations, 0, 0
        ),
        InterlockedCompareExchange(
            &ui_health_no_livelock_suppressions, 0, 0
        ),
        InterlockedCompareExchange(
            &ui_health_consecutive_timeouts, 0, 0
        ),
        InterlockedCompareExchange(
            &ui_health_hard_stalls_detected, 0, 0
        ),
        (long long)last_ack,
        (long long)last_ack_age,
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
        InterlockedCompareExchange(&focus_home_repaint_pulses, 0, 0)
    );
    WritePrivateProfileStringA(
        "home_window", "repaint_pulses", value, focus_status_path
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
static BOOL focus_request_arbitration(HWND preferred_window);
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

static void ui_health_cancel_pending(void) {
    InterlockedExchange64(&ui_health_ping_sent_at, 0);
    InterlockedExchangePointer(&ui_health_ping_window, NULL);
    InterlockedExchange(&ui_health_ping_generation, 0);
}

static void ui_health_reset_stall_tracking(void) {
    InterlockedExchange(&ui_health_consecutive_timeouts, 0);
    InterlockedExchangePointer(&ui_health_stall_window, NULL);
    InterlockedExchange(&ui_health_stall_generation, 0);
}

static void ui_health_cancel_recovery_request(void) {
    InterlockedExchange(&ui_health_recovery_requested, 0);
    InterlockedExchange64(&ui_health_recovery_requested_at, 0);
    if (controller_restart_request_path[0]) {
        DeleteFileA(controller_restart_request_path);
    }
}

static LONG ui_health_note_timeout(HWND window, LONG generation) {
    HWND previous_window = (HWND)InterlockedCompareExchangePointer(
        &ui_health_stall_window, NULL, NULL
    );
    LONG previous_generation = InterlockedCompareExchange(
        &ui_health_stall_generation, 0, 0
    );

    if (previous_window != window || previous_generation != generation) {
        InterlockedExchangePointer(&ui_health_stall_window, window);
        InterlockedExchange(&ui_health_stall_generation, generation);
        InterlockedExchange(&ui_health_consecutive_timeouts, 1);
        return 1;
    }
    return InterlockedIncrement(&ui_health_consecutive_timeouts);
}

static BOOL ui_health_has_hard_stall_evidence(LONG consecutive_timeouts) {
    return consecutive_timeouts >= UI_HEALTH_HARD_STALL_TIMEOUTS;
}

static void focus_remove_window(HWND window) {
    unsigned int index;
    BOOL removed = FALSE;

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
        InterlockedIncrement(&ui_health_window_invalidations);
        ui_health_cancel_pending();
    }
    if (
        removed &&
        window == (HWND)InterlockedCompareExchangePointer(
            &ui_health_stall_window, NULL, NULL
        )
    ) {
        /* Never restart a replacement UI for a window that already died. */
        ui_health_cancel_recovery_request();
        ui_health_reset_stall_tracking();
    }
    if (
        removed &&
        window == (HWND)InterlockedCompareExchangePointer(
            &focus_preferred_window, NULL, NULL
        )
    ) {
        InterlockedExchange(&focus_latch_active, 0);
        InterlockedExchange64(&focus_latch_until, 0);
        InterlockedExchangePointer(&focus_preferred_window, NULL);
        InterlockedExchange(&focus_preferred_role, FOCUS_ROLE_UNKNOWN);
        InterlockedExchange64(&focus_last_apply_posted, 0);
    }
    if (removed) {
        /* A posted arbitration message dies with its target HWND. */
        InterlockedExchange(&focus_arbitration_pending, 0);
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
    LONG sticky_nulls_before,
    LONG sticky_nulls_now
) {
    return guard_breaks_now != guard_breaks_before ||
        sticky_nulls_now != sticky_nulls_before;
}

static void request_controller_restart(
    const CHAR *reason,
    LONG generation,
    LONG guard_breaks_before,
    LONG guard_breaks_now,
    LONG sticky_nulls_before,
    LONG sticky_nulls_now,
    LONG consecutive_timeouts
) {
    CHAR payload[512];
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
        "reason=%s\r\n"
        "hook_version=%lu\r\n"
        "guard_evidence=%u\r\n"
        "sticky_null_evidence=%u\r\n"
        "ui_timeout_evidence=%u\r\n"
        "consecutive_timeouts=%ld\r\n"
        "pings_sent=%ld\r\n"
        "pings_acked=%ld\r\n"
        "window_generation=%ld\r\n"
        "guard_breaks=%ld\r\n"
        "sticky_nulls=%ld\r\n",
        (unsigned long)GetCurrentProcessId(),
        reason ? reason : "unknown",
        (unsigned long)HOOK_VERSION,
        guard_breaks_now != guard_breaks_before ? 1u : 0u,
        sticky_nulls_now != sticky_nulls_before ? 1u : 0u,
        ui_health_has_hard_stall_evidence(consecutive_timeouts) ? 1u : 0u,
        consecutive_timeouts,
        InterlockedCompareExchange(&ui_health_pings_sent, 0, 0),
        InterlockedCompareExchange(&ui_health_pings_acked, 0, 0),
        generation,
        guard_breaks_now,
        sticky_nulls_now
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
        InterlockedExchange64(&ui_health_recovery_requested_at, 0);
        return;
    }
    if (
        length > 0 &&
        WriteFile(file, payload, (DWORD)length, &written, NULL) &&
        written == (DWORD)length
    ) {
        FlushFileBuffers(file);
        InterlockedIncrement(&ui_health_recovery_requests);
        InterlockedExchange64(
            &ui_health_recovery_requested_at,
            (LONG64)GetTickCount64()
        );
    } else {
        InterlockedExchange(&ui_health_recovery_requested, 0);
        InterlockedExchange64(&ui_health_recovery_requested_at, 0);
    }
    CloseHandle(file);
}

static void focus_ui_health_tick(ULONGLONG now) {
    ULONGLONG previous_tick;
    ULONGLONG sent_at;
    LONG sent;
    LONG next;
    LONG generation;
    LONG guard_breaks_before;
    LONG guard_breaks_now;
    LONG sticky_nulls_before;
    LONG sticky_nulls_now;
    LONG consecutive_timeouts;
    LONG recovery_requested;
    ULONGLONG recovery_requested_at;
    HWND window;

    previous_tick = (ULONGLONG)InterlockedExchange64(
        &ui_health_last_worker_tick, (LONG64)now
    );
    sent = InterlockedCompareExchange(&ui_health_pings_sent, 0, 0);
    sent_at = (ULONGLONG)InterlockedCompareExchange64(
        &ui_health_ping_sent_at, 0, 0
    );
    if (
        previous_tick &&
        now >= previous_tick &&
        now - previous_tick > UI_HEALTH_RESUME_GAP_MS
    ) {
        /* A suspended laptop is not a hung Qt event loop. */
        ui_health_cancel_pending();
        ui_health_cancel_recovery_request();
        ui_health_reset_stall_tracking();
        sent_at = 0;
    }
    recovery_requested = InterlockedCompareExchange(
        &ui_health_recovery_requested, 0, 0
    );
    if (recovery_requested) {
        recovery_requested_at = (ULONGLONG)InterlockedCompareExchange64(
            &ui_health_recovery_requested_at, 0, 0
        );
        if (
            recovery_requested_at &&
            now >= recovery_requested_at &&
            now - recovery_requested_at >= UI_HEALTH_RECOVERY_RETRY_MS &&
            GetFileAttributesA(controller_restart_request_path) ==
                INVALID_FILE_ATTRIBUTES
        ) {
            InterlockedExchange(&ui_health_recovery_requested, 0);
            InterlockedExchange64(&ui_health_recovery_requested_at, 0);
        } else {
            return;
        }
    }
    if (sent_at) {
        window = (HWND)InterlockedCompareExchangePointer(
            &ui_health_ping_window, NULL, NULL
        );
        generation = InterlockedCompareExchange(
            &ui_health_ping_generation, 0, 0
        );
        if (!ui_health_target_is_current(window, generation)) {
            InterlockedIncrement(&ui_health_window_invalidations);
            ui_health_cancel_pending();
            ui_health_reset_stall_tracking();
            return;
        }
        if (
            now >= sent_at &&
            now - sent_at >= UI_HEALTH_TIMEOUT_MS
        ) {
            InterlockedIncrement(&ui_health_timeouts);
            consecutive_timeouts = ui_health_note_timeout(
                window, generation
            );
            guard_breaks_before = InterlockedCompareExchange(
                &ui_health_ping_guard_breaks, 0, 0
            );
            guard_breaks_now = InterlockedCompareExchange(
                &event_loop_guard_breaks, 0, 0
            );
            sticky_nulls_before = InterlockedCompareExchange(
                &ui_health_ping_sticky_nulls, 0, 0
            );
            sticky_nulls_now = InterlockedCompareExchange(
                &event_loop_sticky_nulls_detected, 0, 0
            );
            if (ui_health_has_livelock_evidence(
                    guard_breaks_before,
                    guard_breaks_now,
                    sticky_nulls_before,
                    sticky_nulls_now
                )) {
                request_controller_restart(
                    "event-loop-livelock",
                    generation,
                    guard_breaks_before,
                    guard_breaks_now,
                    sticky_nulls_before,
                    sticky_nulls_now,
                    consecutive_timeouts
                );
            } else if (ui_health_has_hard_stall_evidence(
                    consecutive_timeouts
                )) {
                InterlockedIncrement(&ui_health_hard_stalls_detected);
                request_controller_restart(
                    "ui-hard-stall",
                    generation,
                    guard_breaks_before,
                    guard_breaks_now,
                    sticky_nulls_before,
                    sticky_nulls_now,
                    consecutive_timeouts
                );
            } else {
                InterlockedIncrement(
                    &ui_health_no_livelock_suppressions
                );
                ui_health_cancel_pending();
            }
        }
        return;
    }
    generation = 0;
    window = focus_ui_health_window(&generation);
    if (!window || !IsWindow(window)) {
        ui_health_reset_stall_tracking();
        return;
    }
    if (
        window != (HWND)InterlockedCompareExchangePointer(
            &ui_health_stall_window, NULL, NULL
        ) ||
        generation != InterlockedCompareExchange(
            &ui_health_stall_generation, 0, 0
        )
    ) {
        ui_health_reset_stall_tracking();
        InterlockedExchangePointer(&ui_health_stall_window, window);
        InterlockedExchange(&ui_health_stall_generation, generation);
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
        &ui_health_ping_sticky_nulls,
        InterlockedCompareExchange(&event_loop_sticky_nulls_detected, 0, 0)
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
        ui_health_cancel_pending();
        ui_health_reset_stall_tracking();
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
        focus_request_arbitration(window);
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
    InterlockedCompareExchange(&focus_home_repaint_pending, 1, 0);
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
    if (focus_consume_home_show_request()) {
        return FALSE;
    }
    remote = (HWND)InterlockedCompareExchangePointer(
        &focus_visible_remote_window, NULL, NULL
    );
    if (!remote || !IsWindow(remote) || !IsWindowVisible(remote)) {
        InterlockedExchange(&focus_home_hidden_by_user, 0);
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
    LONG preferred_role;

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
    preferred_role = InterlockedCompareExchange(
        &focus_preferred_role, 0, 0
    );
    if (
        (!until || now >= until) &&
        !(preferred_role == FOCUS_ROLE_DIALOG && focus_modal_was_visible)
    ) {
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
    LONG preferred_role;

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
    preferred_role = InterlockedCompareExchange(
        &focus_preferred_role, 0, 0
    );
    if (
        (!until || now >= until) &&
        !(preferred_role == FOCUS_ROLE_DIALOG && focus_modal_was_visible)
    ) {
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

static void focus_queue_home_repaint(HWND window) {
    if (
        !window ||
        InterlockedCompareExchange(&focus_home_repaint_pending, 2, 1) != 1
    ) {
        return;
    }
    if (!PostMessageW(window, WM_UU_HOME_REPAINT, 0, 0)) {
        InterlockedExchange(&focus_home_repaint_pending, 0);
    }
}

static void focus_finish_home_repaint(HWND window) {
    HWND stored_window;
    LONG width;
    LONG height;

    if (InterlockedCompareExchange(&focus_home_repaint_pending, 0, 4) != 4) {
        return;
    }
    stored_window = (HWND)InterlockedExchangePointer(
        &focus_home_repaint_window, NULL
    );
    width = InterlockedExchange(&focus_home_repaint_width, 0);
    height = InterlockedExchange(&focus_home_repaint_height, 0);
    if (
        stored_window == window && window && IsWindow(window) &&
        width > 1 && height > 1 && original_set_window_pos
    ) {
        original_set_window_pos(
            window,
            NULL,
            0,
            0,
            width,
            height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                SWP_NOOWNERZORDER
        );
    }
    if (window && IsWindow(window)) {
        RedrawWindow(
            window,
            NULL,
            NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW
        );
    }
    InterlockedIncrement(&focus_home_repaint_pulses);
}

static void focus_begin_home_repaint(HWND window) {
    RECT rectangle;
    int width;
    int height;

    if (
        InterlockedCompareExchange(&focus_home_repaint_pending, 4, 3) != 3 ||
        !window || !IsWindow(window) || !IsWindowVisible(window)
    ) {
        InterlockedCompareExchange(&focus_home_repaint_pending, 0, 4);
        return;
    }
    if (
        original_set_window_pos &&
        !IsIconic(window) && !IsZoomed(window) &&
        GetWindowRect(window, &rectangle)
    ) {
        width = rectangle.right - rectangle.left;
        height = rectangle.bottom - rectangle.top;
        if (width > 1 && height > 1 && width < INT_MAX && height < INT_MAX) {
            InterlockedExchangePointer(&focus_home_repaint_window, window);
            InterlockedExchange(&focus_home_repaint_width, width);
            InterlockedExchange(&focus_home_repaint_height, height);
            original_set_window_pos(
                window,
                NULL,
                0,
                0,
                width + 1,
                height + 1,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                    SWP_NOOWNERZORDER
            );
            if (SetTimer(
                    window,
                    HOME_REPAINT_TIMER_ID,
                    HOME_REPAINT_RESTORE_DELAY_MS,
                    NULL
                )) {
                return;
            }
        }
    }
    focus_finish_home_repaint(window);
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
    BOOL state_changed;

    if (
        !focus_window_entry(window, &original_proc, &role) ||
        !original_proc ||
        original_proc == focus_subclass_window_proc
    ) {
        return DefWindowProcW(window, message, wparam, lparam);
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
            InterlockedExchange64(
                &ui_health_last_ack_tick, (LONG64)GetTickCount64()
            );
            InterlockedExchange64(&ui_health_ping_sent_at, 0);
            InterlockedExchangePointer(&ui_health_ping_window, NULL);
            ui_health_reset_stall_tracking();
            ui_health_cancel_recovery_request();
        }
        return 0;
    }
    if (message == WM_UU_FOCUS_ARBITRATE) {
        if (InterlockedExchange(&focus_arbitration_pending, 0)) {
            focus_arbitrate_windows();
        }
        return 0;
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
    if (message == WM_UU_HOME_REPAINT) {
        if (
            InterlockedCompareExchange(
                &focus_home_repaint_pending, 3, 2
            ) == 2
        ) {
            if (!SetTimer(
                    window,
                    HOME_REPAINT_TIMER_ID,
                    HOME_REPAINT_DELAY_MS,
                    NULL
                )) {
                InterlockedCompareExchange(
                    &focus_home_repaint_pending, 4, 3
                );
                focus_finish_home_repaint(window);
            }
        }
        return 0;
    }
    if (message == WM_TIMER && wparam == HOME_REPAINT_TIMER_ID) {
        KillTimer(window, HOME_REPAINT_TIMER_ID);
        if (InterlockedCompareExchange(
                &focus_home_repaint_pending, 0, 0
            ) == 3) {
            focus_begin_home_repaint(window);
        } else {
            focus_finish_home_repaint(window);
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

    state_changed =
        message == WM_SHOWWINDOW ||
        message == WM_WINDOWPOSCHANGED ||
        message == WM_STYLECHANGED ||
        message == WM_SETTEXT ||
        message == WM_NCDESTROY;
    result = CallWindowProcW(
        original_proc, window, message, wparam, lparam
    );
    if (
        role == FOCUS_ROLE_HOME &&
        (
            (message == WM_SHOWWINDOW && wparam) ||
            (
                message == WM_WINDOWPOSCHANGED &&
                lparam &&
                (((WINDOWPOS *)lparam)->flags & SWP_SHOWWINDOW)
            )
        )
    ) {
        focus_queue_home_repaint(window);
    }
    if (message == WM_NCDESTROY) {
        focus_remove_window(window);
    }
    if (state_changed) {
        focus_request_arbitration(
            message == WM_NCDESTROY ? NULL : window
        );
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
    if (role == FOCUS_ROLE_HOME && IsWindowVisible(window)) {
        InterlockedCompareExchange(&focus_home_repaint_pending, 1, 0);
        focus_queue_home_repaint(window);
    }
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
    modal_visible = scan.dialog != NULL;
    if (modal_visible) {
        if (!focus_modal_was_visible) {
            InterlockedIncrement(&focus_modal_latches);
        }
        focus_apply_latch(
            scan.dialog, FOCUS_ROLE_DIALOG, 0
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

struct focus_window_state_signature {
    uint64_t hash;
    uint32_t count;
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

static uint64_t focus_signature_mix(uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static BOOL CALLBACK focus_collect_window_state(
    HWND window,
    LPARAM parameter
) {
    struct focus_window_state_signature *signature =
        (struct focus_window_state_signature *)parameter;
    DWORD pid = 0;
    uint64_t value;

    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) {
        return TRUE;
    }
    value = (uint64_t)(uintptr_t)window;
    value ^= (uint64_t)(uintptr_t)GetWindow(window, GW_OWNER) << 1;
    value ^= (uint64_t)(ULONG_PTR)GetWindowLongPtrW(window, GWL_STYLE) << 7;
    value ^=
        (uint64_t)(ULONG_PTR)GetWindowLongPtrW(window, GWL_EXSTYLE) << 13;
    if (IsWindowVisible(window)) {
        value ^= UINT64_C(0x9e3779b97f4a7c15);
    }
    if (IsIconic(window)) {
        value ^= UINT64_C(0xd6e8feb86659fd93);
    }
    signature->hash ^= focus_signature_mix(value);
    ++signature->count;
    return TRUE;
}

static uint64_t focus_window_state_signature_value(void) {
    struct focus_window_state_signature signature;
    HWND foreground;
    DWORD foreground_pid = 0;

    signature.hash = UINT64_C(0x6eed0e9da4d94a4f);
    signature.count = 0;
    EnumWindows(focus_collect_window_state, (LPARAM)&signature);
    foreground = GetForegroundWindow();
    if (foreground) {
        GetWindowThreadProcessId(foreground, &foreground_pid);
        if (foreground_pid == GetCurrentProcessId()) {
            signature.hash ^= focus_signature_mix(
                (uint64_t)(uintptr_t)foreground ^
                UINT64_C(0xa0761d6478bd642f)
            );
        }
    }
    return focus_signature_mix(
        signature.hash ^ ((uint64_t)signature.count << 32)
    );
}

static BOOL focus_request_arbitration(HWND preferred_window) {
    struct focus_wakeup_scan scan;
    HWND wake_window = NULL;
    DWORD wake_pid = 0;
    unsigned int index;

    if (preferred_window && IsWindow(preferred_window)) {
        GetWindowThreadProcessId(preferred_window, &wake_pid);
        if (wake_pid == GetCurrentProcessId()) {
            wake_window = preferred_window;
        }
    }
    AcquireSRWLockShared(&focus_window_lock);
    for (index = 0; !wake_window && index < FOCUS_MAX_WINDOWS; ++index) {
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
    if (!wake_window) {
        return FALSE;
    }
    if (InterlockedCompareExchange(&focus_arbitration_pending, 1, 0)) {
        InterlockedIncrement(&focus_arbitration_coalesced);
        return TRUE;
    }
    if (PostMessageW(wake_window, WM_UU_FOCUS_ARBITRATE, 0, 0)) {
        InterlockedIncrement(&focus_arbitration_posts);
        return TRUE;
    }
    InterlockedCompareExchange(&focus_arbitration_pending, 0, 1);
    return FALSE;
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
    ULONGLONG last_health_ping = 0;
    (void)parameter;

    for (;;) {
        HHOOK release_hook = NULL;
        ULONGLONG now;
        LONG64 previous_signature;
        LONG64 current_signature;

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
            last_activation_scan = now;
        }
        /* Expire abandoned tray authorizations without touching any window. */
        (void)focus_home_show_request_pending();
        current_signature = (LONG64)focus_window_state_signature_value();
        previous_signature = InterlockedExchange64(
            &focus_last_window_signature, current_signature
        );
        if (previous_signature && previous_signature != current_signature) {
            InterlockedIncrement(&focus_window_state_changes);
            focus_request_arbitration(NULL);
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
    focus_arbitrate_windows();
    InterlockedExchange64(
        &focus_last_window_signature,
        (LONG64)focus_window_state_signature_value()
    );
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
    BOOL adapter_ready;
    BOOL ready;

    AcquireSRWLockExclusive(&streamer_patch_lock);
    if (!module) {
        ReleaseSRWLockExclusive(&streamer_patch_lock);
        return FALSE;
    }
    if (module != patched_streamer_module) {
        patched_streamer_module = module;
        streamer_send_input_hooked = FALSE;
        streamer_cursor_info_hooked = FALSE;
        streamer_bit_blt_hooked = FALSE;
        streamer_stretch_blt_hooked = FALSE;
        streamer_dxgi_factory_hooked = FALSE;
        dxgi_factory_vtable_hooked = FALSE;
        dxgi_adapter_vtable_hooked = FALSE;
        dxgi_output_vtable_hooked = FALSE;
        dxgi_duplication_vtable_hooked = FALSE;
        streamer_frame_adapter_hooked = FALSE;
        streamer_wrapper_frame_adapter_hooked = FALSE;
        streamer_portrait_scale_limit_hooked = FALSE;
        streamer_portrait_capture_limit_hooked = FALSE;
        streamer_portrait_update_limit_hooked = FALSE;
        streamer_portrait_scaler_hooked = FALSE;
        original_frame_adapter_id = NULL;
        InterlockedExchange(&frame_adapter_hook_calls, 0);
        InterlockedExchange(&frame_adapter_wrapper_hook_calls, 0);
        InterlockedExchange(&frame_adapter_slots_patched, 0);
        InterlockedExchange(&frame_adapter_long_candidates, 0);
        InterlockedExchange(&frame_adapter_wrapper_candidates, 0);
        InterlockedExchange(&frame_adapter_rtti_named_candidates, 0);
        InterlockedExchange(&frame_adapter_encoder_path_candidates, 0);
        InterlockedExchange(&frame_portrait_scale_limit_candidates, 0);
        InterlockedExchange(&frame_portrait_scale_limit_patches, 0);
        InterlockedExchange(&frame_portrait_capture_limit_candidates, 0);
        InterlockedExchange(&frame_portrait_capture_limit_patches, 0);
        InterlockedExchange(&frame_portrait_update_limit_candidates, 0);
        InterlockedExchange(&frame_portrait_update_limit_patches, 0);
        InterlockedExchange(&frame_portrait_scaler_candidates, 0);
        InterlockedExchange(&frame_portrait_scaler_patches, 0);
        InterlockedExchange(&dxgi_factories_created, 0);
        InterlockedExchange(&dxgi_adapters_enumerated, 0);
        InterlockedExchange(&dxgi_outputs_enumerated, 0);
        InterlockedExchange(&dxgi_duplications_created, 0);
        InterlockedExchange(&dxgi_acquire_calls, 0);
        InterlockedExchange(&dxgi_cursor_announcements, 0);
        InterlockedExchange(&dxgi_cursor_shape_requests, 0);
        InterlockedExchange(&dxgi_cursor_shape_updates, 0);
        InterlockedExchange(&dxgi_cursor_shape_fallbacks, 0);
        InterlockedExchange(&dxgi_cursor_delivered_sequence, 0);
    }
    adapter_ready = patch_streamer_gdi_frame_adapter(module);
    patch_streamer_portrait_scale_limit(module);
    patch_streamer_portrait_capture_limit(module);
    patch_streamer_portrait_update_limit(module);
    patch_streamer_portrait_scaler(module);
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
    if (!streamer_dxgi_factory_hooked) {
        streamer_dxgi_factory_hooked = patch_import(
            module,
            "dxgi.dll",
            "CreateDXGIFactory1",
            (void *)hooked_create_dxgi_factory1,
            (void **)&original_create_dxgi_factory1
        );
    }
    ready = streamer_send_input_hooked &&
        streamer_cursor_info_hooked &&
        streamer_bit_blt_hooked &&
        streamer_stretch_blt_hooked && adapter_ready;
    ReleaseSRWLockExclusive(&streamer_patch_lock);
    return ready;
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
    lstrcpynW(
        native_cursor_path,
        L"C:\\uu-remote-native-cursor.bin",
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

static void initialize_frame_adapter_luid(void) {
    CHAR value[32];
    CHAR *end = NULL;
    unsigned long parsed;
    DWORD length;

    length = GetEnvironmentVariableA(
        "UU_REMOTE_NVENC_ADAPTER_LUID", value, sizeof(value)
    );
    if (!length || length >= sizeof(value)) {
        return;
    }
    parsed = strtoul(value, &end, 10);
    if (
        !end || *end != '\0' || parsed == 0ul ||
        parsed > (unsigned long)UINT32_MAX
    ) {
        return;
    }
    configured_frame_adapter_luid = (DWORD)parsed;
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
    if (streamer_frame_adapter_hooked) {
        status |= 32u;
    }
    return status;
}

__declspec(dllexport) DWORD WINAPI UURemoteFocusHookStatus(void) {
    return controller_focus_hook_status();
}

__declspec(dllexport) DWORD WINAPI UURemoteFrameBoundsSelfTest(void) {
    int width = INT_MAX;
    int height = INT_MAX;
    int translated_x;
    int translated_y;

    if (
        !clamp_frame_source_rectangle(
            128u, 72u, 127, 71, &width, &height
        ) ||
        width != 1 || height != 1
    ) {
        return 0u;
    }
    width = INT_MAX;
    height = INT_MAX;
    if (
        !clamp_frame_source_rectangle(
            128u, 72u, 0, 0, &width, &height
        ) ||
        width != 128 || height != 72
    ) {
        return 0u;
    }
    width = 1;
    height = 1;
    if (
        clamp_frame_source_rectangle(
            128u, 72u, -1, 0, &width, &height
        ) ||
        clamp_frame_source_rectangle(
            128u, 72u, 128, 0, &width, &height
        )
    ) {
        return 0u;
    }
    if (
        !offset_frame_source_coordinates(
            64, 32, -128, 16, &translated_x, &translated_y
        ) ||
        translated_x != -64 || translated_y != 48 ||
        offset_frame_source_coordinates(
            INT_MAX, 0, 1, 0, &translated_x, &translated_y
        ) ||
        offset_frame_source_coordinates(
            0, INT_MIN, 0, -1, &translated_x, &translated_y
        )
    ) {
        return 0u;
    }
    return 1u;
}

__declspec(dllexport) DWORD WINAPI UURemoteFrameSnapshotSelfTest(void) {
    unsigned char storage[UUWF_HEADER_SIZE + UUWF_BUFFER_COUNT * 16u];
    struct wayland_frame_header *header =
        (struct wayland_frame_header *)storage;
    const unsigned char *pixels = NULL;
    SIZE_T index;

    ZeroMemory(storage, sizeof(storage));
    header->magic = UUWF_MAGIC;
    header->version = UUWF_VERSION;
    header->header_size = UUWF_HEADER_SIZE;
    header->width = 2u;
    header->height = 2u;
    header->stride = 8u;
    header->frame_size = 16u;
    header->buffer_count = UUWF_BUFFER_COUNT;
    header->active_buffer = 1u;
    header->sequence = 7u;
    memset(storage + UUWF_HEADER_SIZE, 0x11, 16u);
    memset(storage + UUWF_HEADER_SIZE + 16u, 0x22, 16u);
    if (!snapshot_wayland_frame(header, storage, &pixels) || !pixels) {
        return 0u;
    }
    for (index = 0; index < 16u; ++index) {
        if (pixels[index] != 0x22) {
            return 0u;
        }
    }
    return 1u;
}

__declspec(dllexport) DWORD WINAPI UURemoteFrameMappingIdentitySelfTest(void) {
    BOOL ready;

    AcquireSRWLockExclusive(&frame_lock);
    ready = refresh_frame_mapping();
    if (ready) {
        Sleep(UUWF_PATH_CHECK_INTERVAL_MS + 100u);
        ready = refresh_frame_mapping();
    }
    ReleaseSRWLockExclusive(&frame_lock);
    return ready ? 1u : 0u;
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

__declspec(dllexport) DWORD WINAPI UURemoteStickyNullGuardSelfTest(void) {
    struct event_loop_thread_state state;
    struct event_loop_message_fingerprint fingerprint;
    unsigned int index;

    ZeroMemory(&state, sizeof(state));
    ZeroMemory(&fingerprint, sizeof(fingerprint));
    fingerprint.window = (HWND)(uintptr_t)1u;
    fingerprint.message = WM_NULL;
    fingerprint.time = 1234u;
    fingerprint.point.x = 1313;
    fingerprint.point.y = 395;
    for (
        index = 0;
        index + 1u < EVENT_LOOP_STICKY_NULL_THRESHOLD;
        ++index
    ) {
        if (event_loop_sticky_null_fingerprint_should_filter(
                &state, &fingerprint, TRUE, 10u + index
            )) {
            return 0u;
        }
    }
    if (!event_loop_sticky_null_fingerprint_should_filter(
            &state,
            &fingerprint,
            TRUE,
            10u + EVENT_LOOP_STICKY_NULL_THRESHOLD
        )) {
        return 0u;
    }
    if (!state.sticky_null_active) {
        return 0u;
    }
    if (!event_loop_sticky_null_fingerprint_should_filter(
            &state,
            &fingerprint,
            TRUE,
            11u + EVENT_LOOP_STICKY_NULL_THRESHOLD
        )) {
        return 0u;
    }
    ++fingerprint.time;
    if (event_loop_sticky_null_fingerprint_should_filter(
            &state,
            &fingerprint,
            TRUE,
            12u + EVENT_LOOP_STICKY_NULL_THRESHOLD
        )) {
        return 0u;
    }
    if (state.sticky_null_active) {
        return 0u;
    }
    ZeroMemory(&state, sizeof(state));
    for (index = 0; index < EVENT_LOOP_STICKY_NULL_THRESHOLD; ++index) {
        if (event_loop_sticky_null_fingerprint_should_filter(
                &state, &fingerprint, FALSE, 100u + index
            )) {
            return 0u;
        }
    }
    return 1u;
}

__declspec(dllexport) DWORD WINAPI UURemoteUIHealthEvidenceSelfTest(void) {
    if (ui_health_has_livelock_evidence(3, 3, 10, 10)) {
        return 0u;
    }
    if (!ui_health_has_livelock_evidence(3, 4, 10, 10)) {
        return 0u;
    }
    if (!ui_health_has_livelock_evidence(3, 3, 10, 11)) {
        return 0u;
    }
    if (!ui_health_has_livelock_evidence(3, 4, 10, 11)) {
        return 0u;
    }
    if (ui_health_has_hard_stall_evidence(
            UI_HEALTH_HARD_STALL_TIMEOUTS - 1
        )) {
        return 0u;
    }
    return ui_health_has_hard_stall_evidence(
        UI_HEALTH_HARD_STALL_TIMEOUTS
    ) ? 1u : 0u;
}

struct executable_patch_test_state {
    void *function;
    volatile LONG stop;
    volatile LONG invalid_results;
};

static DWORD WINAPI executable_patch_test_replacement(void *unused) {
    (void)unused;
    return 42u;
}

static DWORD WINAPI executable_patch_test_worker(void *parameter) {
    typedef DWORD(WINAPI *test_function_fn)(void);
    struct executable_patch_test_state *state =
        (struct executable_patch_test_state *)parameter;
    test_function_fn function;

    CopyMemory(&function, &state->function, sizeof(function));
    while (!InterlockedCompareExchange(&state->stop, 0, 0)) {
        DWORD value = function();
        if (value != 7u && value != 42u) {
            InterlockedIncrement(&state->invalid_results);
        }
    }
    return 0u;
}

__declspec(dllexport) DWORD WINAPI UURemoteExecutablePatchSelfTest(void) {
    static const unsigned char original_code[EXECUTABLE_PATCH_SIZE] = {
        0xb8, 0x07, 0x00, 0x00, 0x00, 0xc3,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    typedef DWORD(WINAPI *test_function_fn)(void);
    struct executable_patch_test_state state;
    HANDLE workers[4] = {NULL, NULL, NULL, NULL};
    DWORD old_protection;
    test_function_fn function;
    SIZE_T index;
    BOOL patched = FALSE;
    BOOL workers_ready = TRUE;
    DWORD result = 0u;
    LONG redirects_before;

    ZeroMemory(&state, sizeof(state));
    state.function = VirtualAlloc(
        NULL,
        EXECUTABLE_PATCH_SIZE,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!state.function) {
        return 0u;
    }
    CopyMemory(state.function, original_code, sizeof(original_code));
    if (!VirtualProtect(
            state.function,
            EXECUTABLE_PATCH_SIZE,
            PAGE_EXECUTE_READ,
            &old_protection
        )) {
        goto cleanup;
    }
    FlushInstructionCache(
        GetCurrentProcess(), state.function, EXECUTABLE_PATCH_SIZE
    );
    CopyMemory(&function, &state.function, sizeof(function));
    if (function() != 7u) {
        goto cleanup;
    }
    for (index = 0u; index < sizeof(workers) / sizeof(workers[0]); ++index) {
        workers[index] = CreateThread(
            NULL, 0, executable_patch_test_worker, &state, 0, NULL
        );
        if (!workers[index]) {
            workers_ready = FALSE;
            break;
        }
    }
    if (!workers_ready) {
        goto cleanup;
    }
    Sleep(10);
    redirects_before = InterlockedCompareExchange(
        &executable_patch_redirects, 0, 0
    );
    InterlockedExchange(&executable_patch_test_gate_delay_ms, 10);
    patched = patch_executable_entry(
        state.function, (void *)executable_patch_test_replacement
    );
    InterlockedExchange(&executable_patch_test_gate_delay_ms, 0);
    Sleep(10);
    if (
        patched && function() == 42u &&
        InterlockedCompareExchange(
            &executable_patch_redirects, 0, 0
        ) > redirects_before &&
        InterlockedCompareExchange(&state.invalid_results, 0, 0) == 0
    ) {
        result = 1u;
    }

cleanup:
    InterlockedExchange(&state.stop, 1);
    for (index = 0u; index < sizeof(workers) / sizeof(workers[0]); ++index) {
        if (workers[index]) {
            WaitForSingleObject(workers[index], 5000);
            CloseHandle(workers[index]);
        }
    }
    VirtualFree(state.function, 0, MEM_RELEASE);
    state.function = NULL;
    collect_stale_executable_patch_routes();
    if (
        InterlockedCompareExchange(
            &executable_patch_route_count, 0, 0
        ) != 0 ||
        executable_patch_exception_handler_handle
    ) {
        result = 0u;
    }
    for (
        index = 0u;
        result && index < EXECUTABLE_PATCH_MAX_ROUTES + 8u;
        ++index
    ) {
        state.function = VirtualAlloc(
            NULL,
            EXECUTABLE_PATCH_SIZE,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );
        if (!state.function) {
            result = 0u;
            break;
        }
        CopyMemory(state.function, original_code, sizeof(original_code));
        if (!VirtualProtect(
                state.function,
                EXECUTABLE_PATCH_SIZE,
                PAGE_EXECUTE_READ,
                &old_protection
            )) {
            result = 0u;
        } else {
            FlushInstructionCache(
                GetCurrentProcess(),
                state.function,
                EXECUTABLE_PATCH_SIZE
            );
            CopyMemory(&function, &state.function, sizeof(function));
            if (
                !patch_executable_entry(
                    state.function,
                    (void *)executable_patch_test_replacement
                ) ||
                function() != 42u
            ) {
                result = 0u;
            }
        }
        VirtualFree(state.function, 0, MEM_RELEASE);
        state.function = NULL;
        collect_stale_executable_patch_routes();
        if (
            InterlockedCompareExchange(
                &executable_patch_route_count, 0, 0
            ) != 0 ||
            executable_patch_exception_handler_handle
        ) {
            result = 0u;
        }
    }
    return result;
}

__declspec(dllexport) DWORD WINAPI UURemoteDXGICursorSelfTest(
    DWORD expected_width,
    DWORD expected_height,
    DWORD expected_hotspot_x,
    DWORD expected_hotspot_y
) {
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape_info;
    unsigned char *pixels = NULL;
    UINT required = 0;
    uint32_t sequence = 0;
    HRESULT result;
    DWORD passed = 0;

    ZeroMemory(&shape_info, sizeof(shape_info));
    result = fill_native_dxgi_cursor_shape(
        0,
        NULL,
        &required,
        &shape_info,
        &sequence
    );
    if (
        result != DXGI_ERROR_MORE_DATA ||
        required != expected_width * expected_height * 4u ||
        shape_info.Type != DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR ||
        shape_info.Width != expected_width ||
        shape_info.Height != expected_height ||
        shape_info.Pitch != expected_width * 4u ||
        shape_info.HotSpot.x != (LONG)expected_hotspot_x ||
        shape_info.HotSpot.y != (LONG)expected_hotspot_y
    ) {
        return 0u;
    }
    pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required);
    if (!pixels) {
        return 0u;
    }
    result = fill_native_dxgi_cursor_shape(
        required,
        pixels,
        &required,
        &shape_info,
        &sequence
    );
    if (
        result == S_OK && sequence > 0 &&
        required == expected_width * expected_height * 4u &&
        *(const DWORD *)pixels == 0xff20c060u
    ) {
        passed = 1u;
    }
    HeapFree(GetProcessHeap(), 0, pixels);
    return passed;
}

__declspec(dllexport) DWORD WINAPI UURemoteInputHookMarkPreloaded(
    LPVOID unused
) {
    (void)unused;
    hook_preloaded = TRUE;
    return 1u;
}

__declspec(dllexport) DWORD WINAPI UURemoteInputHookInitialize(LPVOID unused) {
    BOOL ready;
    BOOL streamer_ready = TRUE;

    if (process_kind == HOOK_PROCESS_CONTROLLER) {
        return initialize_controller_focus_hook() ? 1u : 0u;
    }
    if (process_kind != HOOK_PROCESS_SERVER) {
        return 0u;
    }
    if (unused) {
        hook_preloaded = TRUE;
    }
    if (GetModuleHandleW(L"streamer.dll")) {
        streamer_ready = patch_streamer_imports();
    }
    ready = (
        streamer_ready &&
        send_input_hooked &&
        get_key_state_hooked &&
        cursor_pos_hooked &&
        refresh_endpoint()
    );
    write_wol_hook_status();
    write_frame_hook_status();
    return ready ? 1u : 0u;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    HMODULE main_module;
    if (reason == DLL_PROCESS_DETACH) {
        PVOID handler = executable_patch_exception_handler_handle;

        executable_patch_exception_handler_handle = NULL;
        if (handler) {
            RemoveVectoredExceptionHandler(handler);
        }
        if (frame_snapshot) {
            VirtualFree(frame_snapshot, 0, MEM_RELEASE);
            frame_snapshot = NULL;
            frame_snapshot_capacity = 0;
        }
        return TRUE;
    }
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
    initialize_frame_adapter_luid();
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
    get_key_state_hooked = patch_import(
        main_module,
        "USER32.dll",
        "GetKeyState",
        (void *)hooked_get_key_state,
        (void **)&original_get_key_state
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
