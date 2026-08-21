#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

typedef int(WINAPI *probe_streamer_cursor_fn)(
    LONG, LONG, LONG, LONG, DWORD, DWORD, DWORD, DWORD, DWORD
);
typedef int(WINAPI *probe_streamer_frame_fn)(void);
typedef int(WINAPI *probe_streamer_cursor_cache_lifetime_fn)(void);
typedef int(WINAPI *probe_streamer_embedded_cursor_fn)(void);
typedef DWORD(WINAPI *wol_hook_status_fn)(void);
typedef DWORD(WINAPI *frame_hook_status_fn)(void);
typedef DWORD(WINAPI *dxgi_cursor_self_test_fn)(
    DWORD, DWORD, DWORD, DWORD
);
typedef HANDLE EVT_HANDLE;
__declspec(dllimport) BOOL WINAPI EvtClose(EVT_HANDLE object);

#pragma pack(push, 1)
struct test_cursor_header {
    DWORD magic;
    DWORD version;
    DWORD header_size;
    DWORD sequence;
    DWORD width;
    DWORD height;
    DWORD hotspot_x;
    DWORD hotspot_y;
    DWORD pixel_size;
    BYTE reserved[28];
};
#pragma pack(pop)

static const BYTE expected_mac[6] = {0x58, 0x11, 0x22, 0x76, 0x19, 0x64};

static int matching_mac(const BYTE *actual, ULONG length) {
    return length == sizeof(expected_mac) &&
        memcmp(actual, expected_mac, sizeof(expected_mac)) == 0;
}

static int probe_adapter_addresses(void) {
    ULONG size = 0;
    ULONG result;
    PIP_ADAPTER_ADDRESSES adapters;
    PIP_ADAPTER_ADDRESSES adapter;
    ULONG expected_address = inet_addr("198.18.0.1");
    ULONG expected_gateway = inet_addr("10.6.15.254");
    int matched = 0;

    result = GetAdaptersAddresses(
        AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, NULL, &size
    );
    if (result != ERROR_BUFFER_OVERFLOW || !size) {
        return 5;
    }
    adapters = (PIP_ADAPTER_ADDRESSES)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, size
    );
    if (!adapters) {
        return 5;
    }
    result = GetAdaptersAddresses(
        AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, adapters, &size
    );
    if (result == NO_ERROR) {
        for (adapter = adapters; adapter; adapter = adapter->Next) {
            PIP_ADAPTER_UNICAST_ADDRESS address;
            if (
                !matching_mac(
                    adapter->PhysicalAddress,
                    adapter->PhysicalAddressLength
                ) ||
                adapter->IfType != IF_TYPE_ETHERNET_CSMACD ||
                adapter->OperStatus != IfOperStatusUp
            ) {
                continue;
            }
            for (address = adapter->FirstUnicastAddress;
                address;
                address = address->Next) {
                if (
                    address->Address.lpSockaddr &&
                    address->Address.lpSockaddr->sa_family == AF_INET &&
                    address->OnLinkPrefixLength == 15 &&
                    ((SOCKADDR_IN *)address->Address.lpSockaddr)
                        ->sin_addr.s_addr == expected_address &&
                    adapter->FirstGatewayAddress &&
                    adapter->FirstGatewayAddress->Address.lpSockaddr &&
                    adapter->FirstGatewayAddress->Address.lpSockaddr
                        ->sa_family == AF_INET &&
                    ((SOCKADDR_IN *)adapter->FirstGatewayAddress
                        ->Address.lpSockaddr)->sin_addr.s_addr ==
                        expected_gateway &&
                    adapter->AdapterName &&
                    adapter->AdapterName[0] &&
                    adapter->FriendlyName &&
                    adapter->FriendlyName[0] &&
                    adapter->Description &&
                    adapter->Description[0]
                ) {
                    matched = 1;
                    break;
                }
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, adapters);
    return result == NO_ERROR && matched ? 0 : 5;
}

static int probe_adapter_info(void) {
    ULONG size = 0;
    ULONG result;
    PIP_ADAPTER_INFO adapters;
    PIP_ADAPTER_INFO adapter;
    int matched = 0;

    result = GetAdaptersInfo(NULL, &size);
    if (result != ERROR_BUFFER_OVERFLOW || !size) {
        return 6;
    }
    adapters = (PIP_ADAPTER_INFO)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, size
    );
    if (!adapters) {
        return 6;
    }
    result = GetAdaptersInfo(adapters, &size);
    if (result == NO_ERROR) {
        for (adapter = adapters; adapter; adapter = adapter->Next) {
            if (
                matching_mac(adapter->Address, adapter->AddressLength) &&
                adapter->Type == IF_TYPE_ETHERNET_CSMACD &&
                lstrcmpA(
                    adapter->IpAddressList.IpAddress.String,
                    "198.18.0.1"
                ) == 0 &&
                lstrcmpA(
                    adapter->IpAddressList.IpMask.String,
                    "255.254.0.0"
                ) == 0 &&
                lstrcmpA(
                    adapter->GatewayList.IpAddress.String,
                    "10.6.15.254"
                ) == 0
            ) {
                matched = 1;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, adapters);
    return result == NO_ERROR && matched ? 0 : 6;
}

static int probe_if_table2(void) {
    PMIB_IF_TABLE2 table = NULL;
    NETIO_STATUS result = GetIfTable2(&table);
    ULONG index;
    int matched = 0;

    if (result != NO_ERROR || !table) {
        return 7;
    }
    for (index = 0; index < table->NumEntries; ++index) {
        PMIB_IF_ROW2 row = &table->Table[index];
        if (
            matching_mac(row->PhysicalAddress, row->PhysicalAddressLength) &&
            row->Type == IF_TYPE_ETHERNET_CSMACD &&
            row->MediaType == NdisMedium802_3 &&
            row->PhysicalMediumType == NdisPhysicalMedium802_3 &&
            row->AccessType == NET_IF_ACCESS_BROADCAST &&
            row->DirectionType == NET_IF_DIRECTION_SENDRECEIVE &&
            row->OperStatus == IfOperStatusUp &&
            row->AdminStatus == NET_IF_ADMIN_STATUS_UP &&
            row->MediaConnectState == MediaConnectStateConnected &&
            row->InterfaceAndOperStatusFlags.HardwareInterface &&
            row->InterfaceAndOperStatusFlags.ConnectorPresent &&
            !row->InterfaceAndOperStatusFlags.NotMediaConnected
        ) {
            matched = 1;
            break;
        }
    }
    FreeMibTable(table);
    return matched ? 0 : 7;
}

static int close_enough(LONG actual, LONG expected) {
    LONG difference = actual - expected;
    return difference >= -1 && difference <= 1;
}

static int write_test_cursor(
    DWORD sequence,
    DWORD width,
    DWORD height,
    DWORD hotspot_x,
    DWORD hotspot_y
) {
    struct test_cursor_header header;
    HANDLE file;
    DWORD *pixels;
    DWORD pixel_size = width * height * sizeof(*pixels);
    DWORD written;
    DWORD index;
    int result = 1;

    ZeroMemory(&header, sizeof(header));
    header.magic = 0x49435555u;
    header.version = 1;
    header.header_size = sizeof(header);
    header.sequence = sequence;
    header.width = width;
    header.height = height;
    header.hotspot_x = hotspot_x;
    header.hotspot_y = hotspot_y;
    header.pixel_size = pixel_size;
    pixels = (DWORD *)HeapAlloc(GetProcessHeap(), 0, pixel_size);
    if (!pixels) {
        return 1;
    }
    for (index = 0; index < width * height; ++index) {
        pixels[index] = (index % (width + 1u) == 0u)
            ? 0xff20c060u
            : 0x00000000u;
    }
    file = CreateFileW(
        L"C:\\uu-remote-native-cursor.bin",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, pixels);
        return 1;
    }
    if (
        WriteFile(file, &header, sizeof(header), &written, NULL) &&
        written == sizeof(header) &&
        WriteFile(file, pixels, pixel_size, &written, NULL) &&
        written == pixel_size
    ) {
        result = 0;
    }
    CloseHandle(file);
    HeapFree(GetProcessHeap(), 0, pixels);
    return result;
}

static int set_lock_state(int virtual_key, int enabled) {
    INPUT inputs[2];
    int current = (GetKeyState(virtual_key) & 1) != 0;

    if (current == enabled) {
        return 0;
    }
    ZeroMemory(inputs, sizeof(inputs));
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = (WORD)virtual_key;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2 ? 0 : 10;
}

static int probe_caps_lock_state(void) {
    SHORT initial;
    SHORT toggled;
    SHORT restored;

    initial = GetKeyState(VK_CAPITAL);
    if (set_lock_state(VK_CAPITAL, (initial & 1) == 0)) {
        return 10;
    }
    Sleep(100);
    toggled = GetKeyState(VK_CAPITAL);
    if (((initial ^ toggled) & 1) == 0) {
        return 10;
    }
    if (set_lock_state(VK_CAPITAL, (initial & 1) != 0)) {
        return 10;
    }
    Sleep(100);
    restored = GetKeyState(VK_CAPITAL);
    return ((initial ^ restored) & 1) == 0 ? 0 : 10;
}

static int probe_frame_capture(BOOL stretch, int source_x) {
    const int width = 128;
    const int height = 72;
    BITMAPINFO info;
    HDC screen = NULL;
    HDC memory = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previous;
    DWORD *pixels = NULL;
    BOOL copied;
    int result = 9;

    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(
        screen, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0
    );
    if (!screen || !memory || !bitmap || !pixels) {
        goto cleanup;
    }
    previous = SelectObject(memory, bitmap);
    copied = stretch
        ? StretchBlt(
            memory, 0, 0, width, height,
            screen, source_x, 0, width, height, SRCCOPY
        )
        : BitBlt(
            memory, 0, 0, width, height,
            screen, source_x, 0, SRCCOPY | CAPTUREBLT
        );
    if (
        copied &&
        (pixels[0] & 0x00ffffffu) != 0u &&
        (pixels[width * height - 1] & 0x00ffffffu) != 0u &&
        (pixels[0] & 0x00ffffffu) !=
            (pixels[width * height - 1] & 0x00ffffffu)
    ) {
        result = 0;
    }
    SelectObject(memory, previous);

cleanup:
    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (memory) {
        DeleteDC(memory);
    }
    if (screen) {
        ReleaseDC(NULL, screen);
    }
    return result;
}

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous,
    LPSTR command_line,
    int show
) {
    INPUT input;
    POINT position;
    LONG expected_x;
    LONG expected_y;
    HMODULE streamer;
    probe_streamer_cursor_fn probe_streamer_cursor;
    probe_streamer_frame_fn probe_streamer_frame;
    probe_streamer_cursor_cache_lifetime_fn
        probe_streamer_cursor_cache_lifetime;
    probe_streamer_embedded_cursor_fn probe_streamer_embedded_cursor;
    wol_hook_status_fn wol_hook_status;
    frame_hook_status_fn frame_hook_status;
    dxgi_cursor_self_test_fn dxgi_cursor_self_test;
    HMODULE hook;
    int streamer_result;
    int wol_result;
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;

    /*
     * Keep wevtapi.dll as an actual import, just like GameViewerServer. Its
     * project-provided replacement has the input hook as a static dependency,
     * so all WOL IAT entries must already be patched at the first instruction
     * of WinMain, before any watchdog could observe this process.
     */
    EvtClose(NULL);
    hook = GetModuleHandleW(L"uu-remote-input-hook.dll");
    if (!hook) {
        return 8;
    }
    wol_hook_status = (wol_hook_status_fn)GetProcAddress(
        hook, "UURemoteWolHookStatus"
    );
    frame_hook_status = (frame_hook_status_fn)GetProcAddress(
        hook, "UURemoteFrameHookStatus"
    );
    dxgi_cursor_self_test = (dxgi_cursor_self_test_fn)GetProcAddress(
        hook, "UURemoteDXGICursorSelfTest"
    );
    if (
        !wol_hook_status || wol_hook_status() != 15u ||
        !dxgi_cursor_self_test
    ) {
        return 8;
    }
    if (!frame_hook_status || (frame_hook_status() & 3u) != 3u) {
        return 9;
    }
    wol_result = probe_adapter_addresses();
    if (wol_result) {
        return wol_result;
    }
    wol_result = probe_adapter_info();
    if (wol_result) {
        return wol_result;
    }
    wol_result = probe_if_table2();
    if (wol_result) {
        return wol_result;
    }

    streamer = LoadLibraryW(L"streamer.dll");
    if (!streamer) {
        return 21;
    }
    probe_streamer_cursor = (probe_streamer_cursor_fn)GetProcAddress(
        streamer, "ProbeStreamerCursor"
    );
    probe_streamer_frame = (probe_streamer_frame_fn)GetProcAddress(
        streamer, "ProbeStreamerFrame"
    );
    probe_streamer_cursor_cache_lifetime =
        (probe_streamer_cursor_cache_lifetime_fn)GetProcAddress(
            streamer, "ProbeStreamerCursorCacheLifetime"
        );
    probe_streamer_embedded_cursor =
        (probe_streamer_embedded_cursor_fn)GetProcAddress(
            streamer, "ProbeStreamerEmbeddedCursor"
        );
    if (
        !probe_streamer_cursor || !probe_streamer_frame ||
        !probe_streamer_cursor_cache_lifetime ||
        !probe_streamer_embedded_cursor
    ) {
        return 22;
    }

    /* Give the explicit watchdog time to initialize and patch streamer.dll. */
    Sleep(5000);

    if (
        probe_frame_capture(FALSE, 0) ||
        probe_frame_capture(TRUE, 0) ||
        probe_frame_capture(FALSE, -64) ||
        probe_streamer_frame() ||
        (frame_hook_status() & 31u) != 31u
    ) {
        return 9;
    }
    if (probe_caps_lock_state()) {
        return 10;
    }

    /* The fixture's final USER32/Wine cursor position is (500, 300). */
    if (!SetCursorPos(500, 300)) {
        return 3;
    }

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dx = 32768;
    input.mi.dy = 16384;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE |
        MOUSEEVENTF_VIRTUALDESK;
    if (SendInput(1, &input, sizeof(input)) != 1) {
        return 2;
    }
    expected_x = 500;
    expected_y = 300;
    streamer_result = probe_streamer_cursor(
        32768,
        16384,
        expected_x,
        expected_y,
        MOUSEEVENTF_VIRTUALDESK,
        4,
        4,
        1,
        2
    );
    if (streamer_result != 0) {
        return streamer_result;
    }

    /*
     * UU 4.37 no longer imports GetCursorInfo in GameViewerServer.exe.  The
     * cursor query lives in streamer.dll, where ProbeStreamerCursor verifies
     * the required hook.  Keep this executable free of that redundant import
     * so the integration test matches the official module split.
     */
    if (
        !GetCursorPos(&position) ||
        !close_enough(position.x, expected_x) ||
        !close_enough(position.y, expected_y)
    ) {
        return 4;
    }

    /*
     * Without VIRTUALDESK, the same real cursor is exposed in the configured
     * monitor-local space.  That monitor starts at (100, 50).
     */
    if (write_test_cursor(2, 6, 8, 3, 4)) {
        return 14;
    }
    if (!dxgi_cursor_self_test(6, 8, 3, 4)) {
        return 15;
    }
    Sleep(30);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    if (SendInput(1, &input, sizeof(input)) != 1) {
        return 2;
    }
    expected_x = 400;
    expected_y = 250;
    streamer_result = probe_streamer_cursor(
        32768, 16384, expected_x, expected_y, 0, 6, 8, 3, 4
    );
    if (streamer_result != 0) {
        return streamer_result;
    }
    if (
        !GetCursorPos(&position) ||
        !close_enough(position.x, expected_x) ||
        !close_enough(position.y, expected_y)
    ) {
        return 4;
    }
    streamer_result = probe_streamer_cursor_cache_lifetime();
    if (streamer_result != 0) {
        return streamer_result;
    }
    streamer_result = probe_streamer_embedded_cursor();
    if (streamer_result != 0) {
        return streamer_result;
    }
    Sleep(200);
    return 0;
}
