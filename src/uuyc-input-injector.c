/*
 * Explicit Win64 injector/watchdog for the UU Remote input hook.
 *
 * Wine does not reliably honor the Windows AppInit_DLLs mechanism.  This
 * same-architecture helper watches for GameViewerServer.exe and loads the
 * project hook with a remote LoadLibraryW thread.  It detects an existing
 * module before injecting and repeats the operation after a server restart.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <tlhelp32.h>

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#define INJECTOR_VERSION 1u
#define WATCH_INTERVAL_MS 500u

static DWORD find_process(const WCHAR *image_name) {
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    DWORD pid = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (lstrcmpiW(entry.szExeFile, image_name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

static BOOL basename_matches(const WCHAR *path, const WCHAR *name) {
    const WCHAR *basename = path + lstrlenW(path);
    while (basename > path && basename[-1] != L'\\' && basename[-1] != L'/') {
        --basename;
    }
    return lstrcmpiW(basename, name) == 0;
}

static BOOL remote_module_info(
    DWORD pid,
    const WCHAR *module_name,
    uintptr_t *base_address
) {
    HANDLE snapshot;
    MODULEENTRY32W entry;
    BOOL found = FALSE;

    snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid
    );
    if (snapshot == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (
                lstrcmpiW(entry.szModule, module_name) == 0 ||
                basename_matches(entry.szExePath, module_name)
            ) {
                if (base_address) {
                    *base_address = (uintptr_t)entry.modBaseAddr;
                }
                found = TRUE;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static BOOL initialize_remote_hook(DWORD pid, const WCHAR *dll_path) {
    uintptr_t remote_hook;
    HMODULE local_hook = NULL;
    FARPROC local_initialize;
    LPTHREAD_START_ROUTINE remote_initialize;
    HANDLE process = NULL;
    HANDLE thread = NULL;
    DWORD thread_exit = 0;
    BOOL success = FALSE;

    if (!remote_module_info(
            pid, L"uuyc-input-hook.dll", &remote_hook
        )) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    local_hook = LoadLibraryExW(
        dll_path, NULL, DONT_RESOLVE_DLL_REFERENCES
    );
    if (!local_hook) {
        return FALSE;
    }
    local_initialize = GetProcAddress(
        local_hook, "UUYCInputHookInitialize"
    );
    if (!local_initialize) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        goto cleanup;
    }
    remote_initialize = (LPTHREAD_START_ROUTINE)(
        remote_hook +
        ((uintptr_t)local_initialize - (uintptr_t)local_hook)
    );
    process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
        FALSE,
        pid
    );
    if (!process) {
        goto cleanup;
    }
    thread = CreateRemoteThread(
        process, NULL, 0, remote_initialize, NULL, 0, NULL
    );
    if (!thread) {
        goto cleanup;
    }
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) {
        SetLastError(ERROR_TIMEOUT);
        goto cleanup;
    }
    if (!GetExitCodeThread(thread, &thread_exit) || thread_exit == 0) {
        SetLastError(ERROR_DLL_INIT_FAILED);
        goto cleanup;
    }
    success = TRUE;

cleanup:
    if (thread) {
        CloseHandle(thread);
    }
    if (process) {
        CloseHandle(process);
    }
    if (local_hook) {
        FreeLibrary(local_hook);
    }
    return success;
}

static BOOL inject_library(DWORD pid, const WCHAR *dll_path) {
    HANDLE process = NULL;
    HANDLE thread = NULL;
    void *remote_path = NULL;
    HMODULE local_kernel32;
    FARPROC local_load_library;
    uintptr_t remote_kernel32;
    LPTHREAD_START_ROUTINE remote_load_library;
    SIZE_T path_size;
    SIZE_T written = 0;
    DWORD thread_exit = 0;
    BOOL success = FALSE;

    if (remote_module_info(pid, L"uuyc-input-hook.dll", NULL)) {
        return initialize_remote_hook(pid, dll_path);
    }
    if (!remote_module_info(pid, L"kernel32.dll", &remote_kernel32)) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return FALSE;
    }
    local_kernel32 = GetModuleHandleW(L"kernel32.dll");
    local_load_library = GetProcAddress(local_kernel32, "LoadLibraryW");
    if (!local_kernel32 || !local_load_library) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    remote_load_library = (LPTHREAD_START_ROUTINE)(
        remote_kernel32 +
        ((uintptr_t)local_load_library - (uintptr_t)local_kernel32)
    );

    process = OpenProcess(
        PROCESS_CREATE_THREAD |
            PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE |
            PROCESS_VM_READ,
        FALSE,
        pid
    );
    if (!process) {
        goto cleanup;
    }
    path_size = ((SIZE_T)lstrlenW(dll_path) + 1) * sizeof(WCHAR);
    remote_path = VirtualAllocEx(
        process,
        NULL,
        path_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!remote_path) {
        goto cleanup;
    }
    if (
        !WriteProcessMemory(
            process, remote_path, dll_path, path_size, &written
        ) ||
        written != path_size
    ) {
        goto cleanup;
    }
    thread = CreateRemoteThread(
        process,
        NULL,
        0,
        remote_load_library,
        remote_path,
        0,
        NULL
    );
    if (!thread) {
        goto cleanup;
    }
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) {
        SetLastError(ERROR_TIMEOUT);
        goto cleanup;
    }
    if (!GetExitCodeThread(thread, &thread_exit) || thread_exit == 0) {
        SetLastError(ERROR_DLL_INIT_FAILED);
        goto cleanup;
    }
    success = remote_module_info(pid, L"uuyc-input-hook.dll", NULL) &&
        initialize_remote_hook(pid, dll_path);
    if (!success) {
        SetLastError(ERROR_MOD_NOT_FOUND);
    }

cleanup:
    if (thread) {
        CloseHandle(thread);
    }
    if (remote_path) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    }
    if (process) {
        CloseHandle(process);
    }
    return success;
}

static int inject_once(const WCHAR *image_name, const WCHAR *dll_path) {
    DWORD pid = find_process(image_name);
    if (!pid) {
        fwprintf(stderr, L"target-not-running image=%ls\n", image_name);
        return 2;
    }
    if (!inject_library(pid, dll_path)) {
        fwprintf(
            stderr,
            L"inject-failed pid=%lu error=%lu\n",
            (unsigned long)pid,
            (unsigned long)GetLastError()
        );
        return 1;
    }
    wprintf(L"injected pid=%lu\n", (unsigned long)pid);
    return 0;
}

static int watch_process(const WCHAR *image_name, const WCHAR *dll_path) {
    DWORD last_pid = 0;
    BOOL last_injected = FALSE;

    for (;;) {
        DWORD pid = find_process(image_name);
        if (!pid) {
            last_pid = 0;
            last_injected = FALSE;
            Sleep(WATCH_INTERVAL_MS);
            continue;
        }
        if (pid != last_pid) {
            last_pid = pid;
            last_injected = FALSE;
        }
        if (
            !last_injected ||
            !remote_module_info(pid, L"uuyc-input-hook.dll", NULL)
        ) {
            last_injected = inject_library(pid, dll_path);
            if (last_injected) {
                wprintf(L"injected pid=%lu\n", (unsigned long)pid);
                fflush(stdout);
            } else {
                fwprintf(
                    stderr,
                    L"inject-retry pid=%lu error=%lu\n",
                    (unsigned long)pid,
                    (unsigned long)GetLastError()
                );
                fflush(stderr);
            }
        }
        Sleep(WATCH_INTERVAL_MS);
    }
    return 0;
}

__declspec(dllexport) DWORD WINAPI UUYCInputInjectorVersion(void) {
    return INJECTOR_VERSION;
}

int wmain(int argument_count, WCHAR **arguments) {
    if (
        argument_count == 4 &&
        lstrcmpiW(arguments[1], L"--watch") == 0
    ) {
        return watch_process(arguments[2], arguments[3]);
    }
    if (
        argument_count == 4 &&
        lstrcmpiW(arguments[1], L"--once") == 0
    ) {
        return inject_once(arguments[2], arguments[3]);
    }
    fwprintf(
        stderr,
        L"usage: uuyc-input-injector.exe "
        L"(--watch|--once) IMAGE_NAME DLL_PATH\n"
    );
    return 64;
}
