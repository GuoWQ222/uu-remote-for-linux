#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>

typedef DWORD(WINAPI *frame_hook_status_fn)(void);

int main(int argc, char **argv) {
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    BITMAPINFO info = {0};
    HDC screen = NULL;
    HDC memory = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previous = NULL;
    uint32_t *pixels = NULL;
    uint64_t checksum = UINT64_C(1469598103934665603);
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0;
    size_t index;
    size_t count;
    int result = 1;
    HMODULE hook = NULL;

    if (argc > 1) {
        hook = LoadLibraryA(argv[1]);
        if (!hook) {
            fprintf(stderr, "hook-load-failed error=%lu\n", GetLastError());
            return 4;
        }
    }

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "invalid-screen %dx%d\n", width, height);
        return 2;
    }
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
        fprintf(stderr, "gdi-create-failed error=%lu\n", GetLastError());
        goto cleanup;
    }
    previous = SelectObject(memory, bitmap);
    if (!BitBlt(
            memory, 0, 0, width, height, screen, 0, 0,
            SRCCOPY | CAPTUREBLT
        )) {
        fprintf(stderr, "bitblt-failed error=%lu\n", GetLastError());
        goto cleanup;
    }

    count = (size_t)width * (size_t)height;
    for (index = 0; index < count; index += 97) {
        const uint32_t pixel = pixels[index] & UINT32_C(0x00ffffff);
        if (pixel < minimum) {
            minimum = pixel;
        }
        if (pixel > maximum) {
            maximum = pixel;
        }
        checksum ^= pixel;
        checksum *= UINT64_C(1099511628211);
    }
    printf(
        "capture-ok width=%d height=%d min=%06lx max=%06lx "
        "first=%06lx last=%06lx checksum=%016llx\n",
        width, height, (unsigned long)minimum, (unsigned long)maximum,
        (unsigned long)(pixels[0] & UINT32_C(0x00ffffff)),
        (unsigned long)(pixels[(size_t)width * height - 1] &
            UINT32_C(0x00ffffff)),
        (unsigned long long)checksum
    );
    if (hook) {
        union {
            FARPROC generic;
            frame_hook_status_fn typed;
        } status_symbol;
        status_symbol.generic = GetProcAddress(
            hook, "UURemoteFrameHookStatus"
        );
        printf(
            "frame-hook-status=%lu\n",
            status_symbol.typed
                ? (unsigned long)status_symbol.typed()
                : 0ul
        );
    }
    result = minimum == maximum ? 3 : 0;

cleanup:
    if (previous && memory) {
        SelectObject(memory, previous);
    }
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
