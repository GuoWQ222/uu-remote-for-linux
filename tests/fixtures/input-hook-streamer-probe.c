#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
    DWORD coordinate_flags,
    DWORD expected_width,
    DWORD expected_height,
    DWORD expected_hotspot_x,
    DWORD expected_hotspot_y
) {
    INPUT input;
    CURSORINFO cursor;
    ICONINFO icon;
    BITMAP bitmap;

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
    ZeroMemory(&bitmap, sizeof(bitmap));
    if (
        icon.fIcon ||
        icon.xHotspot != expected_hotspot_x ||
        icon.yHotspot != expected_hotspot_y ||
        !icon.hbmColor ||
        GetObjectW(icon.hbmColor, sizeof(bitmap), &bitmap) != sizeof(bitmap) ||
        (DWORD)bitmap.bmWidth != expected_width ||
        (DWORD)bitmap.bmHeight != expected_height
    ) {
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

static int write_lifetime_cursor(DWORD sequence) {
    struct test_cursor_header header;
    DWORD pixels[4];
    HANDLE file;
    DWORD written;
    DWORD index;
    int result = 1;

    ZeroMemory(&header, sizeof(header));
    header.magic = 0x49435555u;
    header.version = 1u;
    header.header_size = sizeof(header);
    header.sequence = sequence;
    header.width = 2u;
    header.height = 2u;
    header.pixel_size = sizeof(pixels);
    for (index = 0u; index < 4u; ++index) {
        pixels[index] = 0xff000000u |
            ((sequence * 17u + index * 31u) & 0x00ffffffu);
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
        return 1;
    }
    if (
        WriteFile(file, &header, sizeof(header), &written, NULL) &&
        written == sizeof(header) &&
        WriteFile(file, pixels, sizeof(pixels), &written, NULL) &&
        written == sizeof(pixels)
    ) {
        result = 0;
    }
    CloseHandle(file);
    return result;
}

__declspec(dllexport) int WINAPI ProbeStreamerCursorCacheLifetime(void) {
    HCURSOR retained = NULL;
    HCURSOR previous = NULL;
    CURSORINFO cursor;
    ICONINFO icon;
    DWORD index;

    for (index = 0u; index < 66u; ++index) {
        if (write_lifetime_cursor(100u + index)) {
            return 30;
        }
        Sleep(12);
        ZeroMemory(&cursor, sizeof(cursor));
        cursor.cbSize = sizeof(cursor);
        if (!GetCursorInfo(&cursor) || !cursor.hCursor) {
            return 31;
        }
        if (previous && cursor.hCursor == previous) {
            /* Every fixture image is unique, including entries 65 and 66. */
            return 33;
        }
        if (index == 0u) {
            retained = cursor.hCursor;
        }
        previous = cursor.hCursor;
    }
    ZeroMemory(&icon, sizeof(icon));
    if (!retained || !GetIconInfo(retained, &icon)) {
        return 32;
    }
    if (icon.hbmMask) {
        DeleteObject(icon.hbmMask);
    }
    if (icon.hbmColor) {
        DeleteObject(icon.hbmColor);
    }
    return 0;
}

__declspec(dllexport) int WINAPI ProbeStreamerEmbeddedCursor(void) {
    CURSORINFO cursor;
    BITMAPINFO bitmap_info;
    HDC screen = NULL;
    HDC memory = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previous = NULL;
    DWORD *pixels = NULL;
    DWORD expected = 0x0055aa33u;
    DWORD index;
    int result = 0;

    if (!WritePrivateProfileStringW(
            L"bridge",
            L"force_cursor",
            L"0",
            L"C:\\uu-remote-input-bridge.endpoint"
        )) {
        return 40;
    }
    /* The hook intentionally rate-limits endpoint refreshes to one second. */
    Sleep(1100);
    ZeroMemory(&cursor, sizeof(cursor));
    cursor.cbSize = sizeof(cursor);
    if (
        !GetCursorInfo(&cursor) ||
        cursor.flags != CURSOR_SHOWING ||
        !cursor.hCursor
    ) {
        return 41;
    }

    screen = GetDC(NULL);
    if (!screen) {
        return 42;
    }
    memory = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!memory) {
        return 43;
    }
    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = 32;
    bitmap_info.bmiHeader.biHeight = -32;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(
        memory,
        &bitmap_info,
        DIB_RGB_COLORS,
        (void **)&pixels,
        NULL,
        0
    );
    if (!bitmap || !pixels) {
        result = 44;
        goto cleanup;
    }
    previous = SelectObject(memory, bitmap);
    if (!previous || previous == HGDI_ERROR) {
        result = 45;
        goto cleanup;
    }
    for (index = 0; index < 32u * 32u; ++index) {
        pixels[index] = expected;
    }
    if (!DrawIconEx(
            memory,
            0,
            0,
            cursor.hCursor,
            32,
            32,
            0,
            NULL,
            DI_NORMAL
        )) {
        result = 46;
        goto cleanup;
    }
    for (index = 0; index < 32u * 32u; ++index) {
        if (pixels[index] != expected) {
            result = 47;
            break;
        }
    }

cleanup:
    if (previous && previous != HGDI_ERROR) {
        SelectObject(memory, previous);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    return result;
}
