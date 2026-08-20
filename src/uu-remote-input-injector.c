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

#define INJECTOR_VERSION 3u
#define WATCH_INTERVAL_MS 500u
#define PRELOAD_MODULE_WAIT_MS 10000u

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

static BOOL initialize_remote_hook(
    DWORD pid,
    const WCHAR *dll_path,
    BOOL preloaded
) {
    uintptr_t remote_hook;
    HMODULE local_hook = NULL;
    FARPROC local_initialize;
    LPTHREAD_START_ROUTINE remote_initialize;
    HANDLE process = NULL;
    HANDLE thread = NULL;
    DWORD thread_exit = 0;
    BOOL success = FALSE;

    if (!remote_module_info(
            pid, L"uu-remote-input-hook.dll", &remote_hook
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
        local_hook, "UURemoteInputHookInitialize"
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
        process,
        NULL,
        0,
        remote_initialize,
        preloaded ? (LPVOID)(uintptr_t)1u : NULL,
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

static BOOL inject_library(
    DWORD pid,
    const WCHAR *dll_path,
    BOOL preloaded
) {
    HANDLE process = NULL;
    HANDLE thread = NULL;
    void *remote_path = NULL;
    HMODULE local_kernel32;
    FARPROC local_load_library;
    uintptr_t remote_kernel32;
    LPTHREAD_START_ROUTINE remote_load_library;
    SIZE_T path_size;
    SIZE_T written = 0;
    DWORD wait_result;
    DWORD thread_exit = 0;
    DWORD failure_error = ERROR_SUCCESS;
    BOOL remote_thread_finished = FALSE;
    BOOL success = FALSE;

    if (remote_module_info(pid, L"uu-remote-input-hook.dll", NULL)) {
        return initialize_remote_hook(pid, dll_path, preloaded);
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
    wait_result = WaitForSingleObject(thread, 10000);
    if (wait_result != WAIT_OBJECT_0) {
        failure_error = wait_result == WAIT_TIMEOUT
            ? ERROR_TIMEOUT
            : (wait_result == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE);
        SetLastError(failure_error);
        goto cleanup;
    }
    remote_thread_finished = TRUE;
    if (!GetExitCodeThread(thread, &thread_exit) || thread_exit == 0) {
        SetLastError(ERROR_DLL_INIT_FAILED);
        goto cleanup;
    }
    success = remote_module_info(pid, L"uu-remote-input-hook.dll", NULL) &&
        initialize_remote_hook(pid, dll_path, preloaded);
    if (!success) {
        SetLastError(ERROR_MOD_NOT_FOUND);
    }

cleanup:
    if (!success && failure_error == ERROR_SUCCESS) {
        failure_error = GetLastError();
    }
    if (thread) {
        CloseHandle(thread);
    }
    if (remote_path && (!thread || remote_thread_finished)) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    }
    if (process) {
        CloseHandle(process);
    }
    if (!success) {
        SetLastError(failure_error);
    }
    return success;
}

static int inject_once(const WCHAR *image_name, const WCHAR *dll_path) {
    DWORD pid = find_process(image_name);
    if (!pid) {
        fwprintf(stderr, L"target-not-running image=%ls\n", image_name);
        return 2;
    }
    if (!inject_library(pid, dll_path, FALSE)) {
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
    uintptr_t initialized_streamer = 0;

    for (;;) {
        DWORD pid = find_process(image_name);
        if (!pid) {
            last_pid = 0;
            last_injected = FALSE;
            initialized_streamer = 0;
            Sleep(WATCH_INTERVAL_MS);
            continue;
        }
        if (pid != last_pid) {
            last_pid = pid;
            last_injected = FALSE;
            initialized_streamer = 0;
        }
        if (
            !last_injected ||
            !remote_module_info(pid, L"uu-remote-input-hook.dll", NULL)
        ) {
            last_injected = inject_library(pid, dll_path, FALSE);
            if (last_injected) {
                initialized_streamer = 0;
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
        if (last_injected) {
            uintptr_t streamer_module = 0;

            if (!remote_module_info(
                    pid, L"streamer.dll", &streamer_module
                )) {
                initialized_streamer = 0;
            } else if (
                streamer_module != initialized_streamer &&
                initialize_remote_hook(pid, dll_path, FALSE)
            ) {
                initialized_streamer = streamer_module;
                wprintf(
                    L"streamer-initialized pid=%lu\n",
                    (unsigned long)pid
                );
                fflush(stdout);
            }
        }
        Sleep(WATCH_INTERVAL_MS);
    }
    return 0;
}

static int watch_process_with_module(
    const WCHAR *image_name,
    const WCHAR *required_module,
    const WCHAR *dll_path
) {
    struct watched_process {
        DWORD pid;
        BOOL initialized;
        BOOL seen;
    } watched[32];
    unsigned int watched_count = 0;

    ZeroMemory(watched, sizeof(watched));
    for (;;) {
        HANDLE snapshot;
        PROCESSENTRY32W entry;
        unsigned int index;

        for (index = 0; index < watched_count; ++index) {
            watched[index].seen = FALSE;
        }
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            ZeroMemory(&entry, sizeof(entry));
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    struct watched_process *state = NULL;

                    if (lstrcmpiW(entry.szExeFile, image_name) != 0) {
                        continue;
                    }
                    if (!remote_module_info(
                            entry.th32ProcessID, required_module, NULL
                        )) {
                        continue;
                    }
                    for (index = 0; index < watched_count; ++index) {
                        if (watched[index].pid == entry.th32ProcessID) {
                            state = &watched[index];
                            break;
                        }
                    }
                    if (!state && watched_count < 32u) {
                        state = &watched[watched_count++];
                        state->pid = entry.th32ProcessID;
                        state->initialized = FALSE;
                    }
                    if (!state) {
                        continue;
                    }
                    state->seen = TRUE;
                    if (
                        state->initialized &&
                        !remote_module_info(
                            state->pid,
                            L"uu-remote-input-hook.dll",
                            NULL
                        )
                    ) {
                        state->initialized = FALSE;
                    }
                    if (!state->initialized) {
                        state->initialized = inject_library(
                            state->pid, dll_path, FALSE
                        );
                        if (state->initialized) {
                            wprintf(
                                L"injected pid=%lu module=%ls\n",
                                (unsigned long)state->pid,
                                required_module
                            );
                            fflush(stdout);
                        } else {
                            fwprintf(
                                stderr,
                                L"inject-retry pid=%lu module=%ls "
                                L"error=%lu\n",
                                (unsigned long)state->pid,
                                required_module,
                                (unsigned long)GetLastError()
                            );
                            fflush(stderr);
                        }
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
        for (index = 0; index < watched_count;) {
            if (!watched[index].seen) {
                watched[index] = watched[watched_count - 1u];
                --watched_count;
            } else {
                ++index;
            }
        }
        Sleep(WATCH_INTERVAL_MS);
    }
    return 0;
}

static int launch_suspended(
    const WCHAR *dll_path,
    const WCHAR *application_path,
    BOOL wait_for_exit
) {
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD exit_code = 1;
    BOOL main_thread_resumed = FALSE;

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (
        !CreateProcessW(
            application_path,
            NULL,
            NULL,
            NULL,
            FALSE,
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            NULL,
            NULL,
            &startup,
            &process
        )
    ) {
        fwprintf(
            stderr,
            L"launch-failed application=%ls error=%lu\n",
            application_path,
            (unsigned long)GetLastError()
        );
        return 1;
    }
    if (!remote_module_info(process.dwProcessId, L"kernel32.dll", NULL)) {
        /*
         * Wine returns a suspended process before its PE loader has exposed
         * imported modules through Toolhelp.  Let the new main thread advance
         * through the loader and inject as soon as both kernel32 and iphlpapi
         * become visible.  Do not suspend it again: a fixed-delay suspension
         * can catch Wine while it owns the loader lock, which deadlocks the
         * remote LoadLibrary thread and makes the hook appear only at process
         * teardown.
         */
        DWORD waited = 0;

        if (ResumeThread(process.hThread) == (DWORD)-1) {
            TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
            WaitForSingleObject(process.hProcess, 10000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 1;
        }
        main_thread_resumed = TRUE;
        while (
            waited < PRELOAD_MODULE_WAIT_MS &&
            WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT &&
            (
                !remote_module_info(
                    process.dwProcessId, L"kernel32.dll", NULL
                ) ||
                !remote_module_info(
                    process.dwProcessId, L"iphlpapi.dll", NULL
                )
            )
        ) {
            Sleep(1);
            ++waited;
        }
        if (
            WaitForSingleObject(process.hProcess, 0) != WAIT_TIMEOUT ||
            !remote_module_info(
                process.dwProcessId, L"kernel32.dll", NULL
            ) ||
            !remote_module_info(
                process.dwProcessId, L"iphlpapi.dll", NULL
            )
        ) {
            fwprintf(
                stderr,
                L"preload-modules-timeout pid=%lu error=%lu\n",
                (unsigned long)process.dwProcessId,
                (unsigned long)GetLastError()
            );
            TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
            WaitForSingleObject(process.hProcess, 10000);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 1;
        }
    }
    if (!inject_library(process.dwProcessId, dll_path, TRUE)) {
        fwprintf(
            stderr,
            L"preload-failed pid=%lu error=%lu\n",
            (unsigned long)process.dwProcessId,
            (unsigned long)GetLastError()
        );
        TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
        WaitForSingleObject(process.hProcess, 10000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
    if (
        !main_thread_resumed &&
        ResumeThread(process.hThread) == (DWORD)-1
    ) {
        fwprintf(
            stderr,
            L"resume-failed pid=%lu error=%lu\n",
            (unsigned long)process.dwProcessId,
            (unsigned long)GetLastError()
        );
        TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
        WaitForSingleObject(process.hProcess, 10000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
    wprintf(L"preloaded pid=%lu\n", (unsigned long)process.dwProcessId);
    fflush(stdout);
    CloseHandle(process.hThread);
    if (wait_for_exit) {
        WaitForSingleObject(process.hProcess, INFINITE);
        if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
            exit_code = 1;
        }
    } else {
        exit_code = 0;
    }
    CloseHandle(process.hProcess);
    return (int)exit_code;
}

__declspec(dllexport) DWORD WINAPI UURemoteInputInjectorVersion(void) {
    return INJECTOR_VERSION;
}

int wmain(int argument_count, WCHAR **arguments) {
    if (
        argument_count == 5 &&
        lstrcmpiW(arguments[1], L"--watch-module") == 0
    ) {
        return watch_process_with_module(
            arguments[2], arguments[3], arguments[4]
        );
    }
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
    if (
        argument_count == 4 &&
        lstrcmpiW(arguments[1], L"--launch-and-wait") == 0
    ) {
        return launch_suspended(arguments[2], arguments[3], TRUE);
    }
    if (
        argument_count == 4 &&
        lstrcmpiW(arguments[1], L"--launch") == 0
    ) {
        return launch_suspended(arguments[2], arguments[3], FALSE);
    }
    fwprintf(
        stderr,
        L"usage: uu-remote-input-injector.exe "
        L"(--watch|--once) IMAGE_NAME DLL_PATH\n"
        L"       uu-remote-input-injector.exe "
        L"--watch-module IMAGE_NAME REQUIRED_MODULE DLL_PATH\n"
        L"       uu-remote-input-injector.exe "
        L"(--launch|--launch-and-wait) DLL_PATH APPLICATION_PATH\n"
    );
    return 64;
}
