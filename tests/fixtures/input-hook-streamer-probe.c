#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int close_enough(LONG actual, LONG expected) {
    LONG difference = actual - expected;
    return difference >= -1 && difference <= 1;
}

static int probe_frame_capture(BOOL stretch) {
    const int width = 128;
    const int height = 72;
    BITMAPINFO info;
    HDC screen;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    DWORD *pixels;
    BOOL copied;
    int result = 20;

    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    pixels = NULL;
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
            screen, 0, 0, width, height, SRCCOPY
        )
        : BitBlt(
            memory, 0, 0, width, height,
            screen, 0, 0, SRCCOPY | CAPTUREBLT
        );
    if (
        copied &&
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

__declspec(dllexport) int WINAPI ProbeStreamerFrame(void) {
    int result = probe_frame_capture(FALSE);
    return result ? result : probe_frame_capture(TRUE);
}

__declspec(dllexport) int WINAPI ProbeStreamerCursor(
    LONG normalized_x,
    LONG normalized_y,
    LONG expected_x,
    LONG expected_y,
    DWORD coordinate_flags
) {
    INPUT input;
    CURSORINFO cursor;
    ICONINFO icon;

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dx = normalized_x;
    input.mi.dy = normalized_y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE |
        coordinate_flags;
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
