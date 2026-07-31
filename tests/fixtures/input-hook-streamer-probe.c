#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int close_enough(LONG actual, LONG expected) {
    LONG difference = actual - expected;
    return difference >= -1 && difference <= 1;
}

__declspec(dllexport) int WINAPI ProbeStreamerCursor(
    LONG normalized_x,
    LONG normalized_y,
    LONG expected_x,
    LONG expected_y
) {
    INPUT input;
    CURSORINFO cursor;
    ICONINFO icon;

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dx = normalized_x;
    input.mi.dy = normalized_y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    if (SendInput(1, &input, sizeof(input)) != 1) {
        return 10;
    }

    ZeroMemory(&cursor, sizeof(cursor));
    cursor.cbSize = sizeof(cursor);
    if (
        !GetCursorInfo(&cursor) ||
        cursor.flags != CURSOR_SHOWING ||
        !cursor.hCursor ||
        !close_enough(cursor.ptScreenPos.x, expected_x) ||
        !close_enough(cursor.ptScreenPos.y, expected_y)
    ) {
        return 11;
    }

    ZeroMemory(&icon, sizeof(icon));
    if (!GetIconInfo(cursor.hCursor, &icon)) {
        return 12;
    }
    if (!icon.hbmMask && !icon.hbmColor) {
        return 13;
    }
    if (icon.hbmMask) {
        DeleteObject(icon.hbmMask);
    }
    if (icon.hbmColor) {
        DeleteObject(icon.hbmColor);
    }
    return 0;
}
