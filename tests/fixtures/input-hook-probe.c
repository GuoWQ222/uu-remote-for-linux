#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef int(WINAPI *probe_streamer_cursor_fn)(LONG, LONG, LONG, LONG);

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
    int streamer_result;
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;

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

    /* Give the explicit injector watchdog time to patch both PE modules. */
    Sleep(1500);
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
