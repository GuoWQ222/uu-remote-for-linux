/*
 * UU Remote WOL PowerShell compatibility bridge.
 *
 * The Windows client checks NIC wake properties with the Windows-only
 * NetAdapter PowerShell module. Wine ships a PowerShell launcher, but not the
 * NetAdapter cmdlets, so those commands exit without useful output and UU
 * reports that no wired adapter exists even after IP Helper enumeration has
 * found the Linux Ethernet interface.
 *
 * This process-local application-directory bridge answers only the known WOL
 * property queries, and only after the Linux launcher has verified and enabled
 * native Magic Packet wake-up. All unrelated commands are delegated to Wine's
 * system PowerShell executable.
 */

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <process.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define BRIDGE_VERSION 1

static const WCHAR config_path[] = L"C:\\uu-remote-wol-bridge.ini";
static const WCHAR status_path[] =
    L"C:\\uu-remote-wol-powershell-status.ini";

static BOOL contains_case_insensitive(
    const WCHAR *haystack,
    const WCHAR *needle
) {
    size_t needle_length;
    const WCHAR *candidate;

    if (!haystack || !needle) {
        return FALSE;
    }
    needle_length = wcslen(needle);
    if (needle_length == 0) {
        return TRUE;
    }
    for (candidate = haystack; *candidate; ++candidate) {
        size_t index;
        for (index = 0; index < needle_length; ++index) {
            if (!candidate[index]) {
                return FALSE;
            }
            if (
                towlower((wint_t)candidate[index]) !=
                towlower((wint_t)needle[index])
            ) {
                break;
            }
        }
        if (index == needle_length) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL native_wol_ready(void) {
    return (
        GetPrivateProfileIntW(L"wol", L"enabled", 0, config_path) == 1 &&
        GetPrivateProfileIntW(
            L"wol", L"native_magic", 0, config_path
        ) == 1 &&
        GetPrivateProfileIntW(
            L"wol", L"native_power_wakeup", 0, config_path
        ) == 1
    );
}

static void record_status(const WCHAR *query, const WCHAR *result) {
    WCHAR value[32];
    int calls;

    wsprintfW(value, L"%u", (unsigned int)BRIDGE_VERSION);
    WritePrivateProfileStringW(L"bridge", L"version", value, status_path);
    wsprintfW(value, L"%lu", (unsigned long)GetCurrentProcessId());
    WritePrivateProfileStringW(L"bridge", L"pid", value, status_path);
    WritePrivateProfileStringW(L"bridge", L"last_query", query, status_path);
    WritePrivateProfileStringW(L"bridge", L"last_result", result, status_path);
    calls = GetPrivateProfileIntW(L"calls", query, 0, status_path);
    wsprintfW(value, L"%d", calls + 1);
    WritePrivateProfileStringW(L"calls", query, value, status_path);
}

static int emit_ascii_line(
    const WCHAR *query,
    const CHAR *text,
    const WCHAR *status_result
) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    size_t length = strlen(text);

    record_status(query, status_result);
    if (
        output == NULL ||
        output == INVALID_HANDLE_VALUE ||
        length > MAXDWORD
    ) {
        return 1;
    }
    return WriteFile(output, text, (DWORD)length, &written, NULL) &&
        written == (DWORD)length ? 0 : 1;
}

static int delegate_to_system_powershell(WCHAR **arguments) {
    WCHAR executable[MAX_PATH];
    UINT length;
    intptr_t result;

    length = GetSystemDirectoryW(executable, MAX_PATH);
    if (
        length == 0 ||
        length >= MAX_PATH ||
        lstrlenW(executable) +
            lstrlenW(L"\\WindowsPowerShell\\v1.0\\powershell.exe") >=
            MAX_PATH
    ) {
        record_status(L"delegate", L"system-directory-error");
        return 127;
    }
    lstrcatW(executable, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    record_status(L"delegate", L"system-powershell");
    result = _wspawnv(
        _P_WAIT,
        executable,
        (const WCHAR *const *)arguments
    );
    if (result == -1) {
        record_status(L"delegate", L"spawn-error");
        return 127;
    }
    return (int)result;
}

int wmain(int argument_count, WCHAR **arguments) {
    const WCHAR *command_line = GetCommandLineW();

    if (
        argument_count == 2 &&
        lstrcmpiW(arguments[1], L"--uu-remote-bridge-version") == 0
    ) {
        return emit_ascii_line(L"version", "1\r\n", L"1");
    }
    if (!native_wol_ready()) {
        return delegate_to_system_powershell(arguments);
    }

    if (
        contains_case_insensitive(command_line, L"Set-NetAdapter") ||
        contains_case_insensitive(command_line, L"Set-CimInstance")
    ) {
        record_status(L"setter", L"success");
        return 0;
    }
    if (
        contains_case_insensitive(
            command_line,
            L"AllowComputerToTurnOffDevice"
        )
    ) {
        return emit_ascii_line(
            L"allow_turn_off",
            "Enabled\r\n",
            L"Enabled"
        );
    }
    if (
        contains_case_insensitive(command_line, L"S5WakeOnLan") &&
        contains_case_insensitive(command_line, L"RegistryValue")
    ) {
        return emit_ascii_line(L"s5_wake", "1\r\n", L"1");
    }
    if (
        contains_case_insensitive(command_line, L"EnablePME") &&
        contains_case_insensitive(command_line, L"RegistryValue")
    ) {
        return emit_ascii_line(L"enable_pme", "1\r\n", L"1");
    }
    if (
        contains_case_insensitive(
            command_line,
            L"MSNdis_DeviceWakeOnMagicPacketOnly"
        )
    ) {
        return emit_ascii_line(L"magic_only", "True\r\n", L"True");
    }
    if (
        contains_case_insensitive(
            command_line,
            L"MSPower_DeviceWakeEnable"
        )
    ) {
        return emit_ascii_line(L"device_wake", "True\r\n", L"True");
    }
    if (
        contains_case_insensitive(command_line, L"WakeOnMagicPacket")
    ) {
        return emit_ascii_line(
            L"magic_packet",
            "Enabled\r\n",
            L"Enabled"
        );
    }
    if (contains_case_insensitive(command_line, L"WakeOnPattern")) {
        return emit_ascii_line(
            L"wake_pattern",
            "Enabled\r\n",
            L"Enabled"
        );
    }

    return delegate_to_system_powershell(arguments);
}
