/*
 * UU Remote Win64 input hook.
 *
 * GameViewerServer falls back to USER32.SendInput when its proprietary Windows
 * HID driver is unavailable.  Wine consumes that call inside the Windows
 * server and does not create a real Linux input event.  This DLL patches only
 * GameViewerServer.exe's main-module import table and reroutes SendInput
 * packets to the authenticated native bridge. Keeping one authoritative
 * native path avoids duplicate relative mouse movement through Wine and XTest.
 *
 * The hook is loaded by the project's explicit same-architecture injector.
 * It deliberately performs no networking while the loader lock is held.
 * Endpoint discovery and Winsock initialization are lazy.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <windows.h>

#include <stdint.h>
#include <string.h>

#define UUIP_MAGIC 0x50495555u
#define UUIP_VERSION 1u
#define UUIP_HELLO 1u
#define UUIP_MOUSE 2u
#define UUIP_KEYBOARD 3u
#define HOOK_VERSION 3u

typedef UINT(WINAPI *send_input_fn)(UINT, LPINPUT, int);
typedef BOOL(WINAPI *get_cursor_info_fn)(PCURSORINFO);
typedef BOOL(WINAPI *get_cursor_pos_fn)(LPPOINT);

static send_input_fn original_send_input;
static get_cursor_info_fn original_get_cursor_info;
static get_cursor_pos_fn original_get_cursor_pos;
static HMODULE hook_module;
static WCHAR endpoint_path[MAX_PATH];
static INIT_ONCE winsock_once = INIT_ONCE_STATIC_INIT;
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
static HMODULE patched_streamer_module;
static BOOL streamer_send_input_hooked;
static BOOL streamer_cursor_info_hooked;
static SRWLOCK cursor_lock = SRWLOCK_INIT;
static POINT tracked_cursor_position;
static BOOL tracked_cursor_valid;

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
#pragma pack(pop)

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

static BOOL patch_streamer_imports(void) {
    HMODULE module = GetModuleHandleW(L"streamer.dll");

    if (!module) {
        return FALSE;
    }
    if (module != patched_streamer_module) {
        patched_streamer_module = module;
        streamer_send_input_hooked = FALSE;
        streamer_cursor_info_hooked = FALSE;
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
    return streamer_send_input_hooked && streamer_cursor_info_hooked;
}

static BOOL is_target_process(void) {
    WCHAR process_path[MAX_PATH];
    WCHAR *name;
    if (!GetModuleFileNameW(NULL, process_path, MAX_PATH)) {
        return FALSE;
    }
    name = process_path + lstrlenW(process_path);
    while (name > process_path && name[-1] != L'\\' && name[-1] != L'/') {
        --name;
    }
    return lstrcmpiW(name, L"GameViewerServer.exe") == 0;
}

static void initialize_endpoint_path(void) {
    WCHAR *name;
    if (!GetModuleFileNameW(hook_module, endpoint_path, MAX_PATH)) {
        endpoint_path[0] = L'\0';
        return;
    }
    name = endpoint_path + lstrlenW(endpoint_path);
    while (name > endpoint_path && name[-1] != L'\\' && name[-1] != L'/') {
        --name;
    }
    lstrcpynW(
        name,
        L"uuyc-input-bridge.endpoint",
        (int)(MAX_PATH - (name - endpoint_path))
    );
}

__declspec(dllexport) DWORD WINAPI UUYCInputHookVersion(void) {
    return HOOK_VERSION;
}

__declspec(dllexport) DWORD WINAPI UUYCInputHookInitialize(LPVOID unused) {
    (void)unused;
    return (
        send_input_hooked &&
        cursor_info_hooked &&
        cursor_pos_hooked &&
        patch_streamer_imports() &&
        refresh_endpoint()
    ) ? 1u : 0u;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    HMODULE main_module;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    hook_module = instance;
    DisableThreadLibraryCalls(instance);
    if (!is_target_process()) {
        return TRUE;
    }
    initialize_endpoint_path();
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
    return TRUE;
}
