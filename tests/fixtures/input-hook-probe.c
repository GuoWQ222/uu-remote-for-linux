#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

typedef int(WINAPI *probe_streamer_cursor_fn)(LONG, LONG, LONG, LONG);
typedef DWORD(WINAPI *wol_hook_status_fn)(void);
typedef HANDLE EVT_HANDLE;
__declspec(dllimport) BOOL WINAPI EvtClose(EVT_HANDLE object);

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

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous,
    LPSTR command_line,
    int show
) {
    INPUT input;
    CURSORINFO cursor;
    POINT position;
    LONG expected_x;
    LONG expected_y;
    HMODULE streamer;
    probe_streamer_cursor_fn probe_streamer_cursor;
    wol_hook_status_fn wol_hook_status;
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
    if (!wol_hook_status || wol_hook_status() != 15u) {
        return 8;
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
        return 1;
    }
    probe_streamer_cursor = (probe_streamer_cursor_fn)GetProcAddress(
        streamer, "ProbeStreamerCursor"
    );
    if (!probe_streamer_cursor) {
        return 1;
    }

    /* Give the explicit watchdog time to initialize and patch streamer.dll. */
    Sleep(5000);

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dx = 32768;
    input.mi.dy = 16384;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    if (SendInput(1, &input, sizeof(input)) != 1) {
        return 2;
    }
    expected_x = (LONG)(
        (32768LL * (GetSystemMetrics(SM_CXSCREEN) - 1) + 32767) /
        65535
    );
    expected_y = (LONG)(
        (16384LL * (GetSystemMetrics(SM_CYSCREEN) - 1) + 32767) /
        65535
    );
    streamer_result = probe_streamer_cursor(
        32768, 16384, expected_x, expected_y
    );
    if (streamer_result != 0) {
        return streamer_result;
    }

    ZeroMemory(&cursor, sizeof(cursor));
    cursor.cbSize = sizeof(cursor);
    if (
        !GetCursorInfo(&cursor) ||
        cursor.flags != CURSOR_SHOWING ||
        !close_enough(cursor.ptScreenPos.x, expected_x) ||
        !close_enough(cursor.ptScreenPos.y, expected_y)
    ) {
        return 3;
    }
    if (
        !GetCursorPos(&position) ||
        !close_enough(position.x, expected_x) ||
        !close_enough(position.y, expected_y)
    ) {
        return 4;
    }
    Sleep(200);
    return 0;
}
