/* Exercise the real official image, with all changes confined to this process. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef DWORD (WINAPI *initialize_fn)(void *);

/* Execute the real scaler's comparison/ratio instructions in a private thunk.
 * Only its final fast-path jump is redirected to a local return sequence. */
static float scaler_ratio(const unsigned char *fragment, int width, int height) {
    static const unsigned char prefix[] = {
        0x55,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x57,
        0x48,0x81,0xec,0xd8,0x03,0,0,
        0xf3,0x0f,0x7f,0xbc,0x24,0xb0,0x03,0,0,
        0xf3,0x44,0x0f,0x7f,0x84,0x24,0xc0,0x03,0,0,
        0x48,0x8d,0xac,0x24,0,0x01,0,0,
        0x44,0x89,0x4d,0xcc,0xc7,0x45,0xc4,0,0,0,0,
        0x48,0x8b,0xf1,0x4c,0x8b,0xea,0x41,0x8b,0xd0,
        0x45,0x33,0xc0,0x45,0x33,0xff
    };
    static const unsigned char suffix[] = {
        0xeb,0x09,0xb8,0,0,0x80,0x3f,0x66,0x0f,0x6e,0xf8,
        0xf3,0x0f,0x10,0xc7,
        0xf3,0x0f,0x6f,0xbc,0x24,0xb0,0x03,0,0,
        0xf3,0x44,0x0f,0x6f,0x84,0x24,0xc0,0x03,0,0,
        0x48,0x81,0xc4,0xd8,0x03,0,0,
        0x41,0x5f,0x41,0x5d,0x41,0x5c,0x5e,0x5b,0x5d,0xc3
    };
    unsigned char limits[0xa0] = {0}, frame[0x40] = {0};
    unsigned char *code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    float (WINAPI *evaluate)(void *, void *, int, int);
    float result;
    int32_t jump = 193 + 2 - (112 + 5);
    if (!code) return -1.0f;
    *(int *)(limits + 0x90) = 3840;
    *(int *)(limits + 0x94) = 2160;
    *(int *)(frame + 0x2c) = width;
    *(int *)(frame + 0x30) = height;
    memcpy(code, prefix, sizeof(prefix));
    memcpy(code + sizeof(prefix), fragment, 193);
    memcpy(code + sizeof(prefix) + 113, &jump, sizeof(jump));
    memcpy(code + sizeof(prefix) + 193, suffix, sizeof(suffix));
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    evaluate = (float (WINAPI *)(void *, void *, int, int))(uintptr_t)code;
    result = evaluate(limits, frame, width, height);
    VirtualFree(code, 0, MEM_RELEASE);
    return result;
}

static int check_scaler(const unsigned char *fragment, int patched) {
    static const struct { int width, height; float original, updated; } cases[] = {
        {1440, 2560, 0.84375f, 1.0f},
        {2160, 3840, 0.5625f, 1.0f},
        {2560, 1440, 1.0f, 1.0f},
        {5120, 1440, 0.75f, 0.75f}
    };
    SIZE_T i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        float expected = patched ? cases[i].updated : cases[i].original;
        float actual = scaler_ratio(fragment, cases[i].width, cases[i].height);
        if (actual < expected - 0.0001f || actual > expected + 0.0001f) {
            fprintf(stderr, "scaler %dx%d: got %f, expected %f (patched=%d)\n",
                    cases[i].width, cases[i].height, actual, expected, patched);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    HMODULE streamer, hook;
    unsigned char *base, *text = NULL, *before;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sections;
    SIZE_T text_size = 0, i, changed = 0;
    DWORD expected[8];
    unsigned char values[8] = {0x30, 0xc0, 0xc0, 0xc0, 0x90, 0x88, 0x90, 0x88};
    uintptr_t original_slot;
    DWORD slot;
    initialize_fn initialize;
    char mode[64];

    if (argc != 7) {
        fprintf(stderr, "expected hook path and scale/capture/update/scaler/adapter-slot RVAs\n");
        return 2;
    }
    streamer = LoadLibraryA("streamer.dll");
    if (!streamer) { fprintf(stderr, "streamer load failed: %lu\n", GetLastError()); return 1; }
    base = (unsigned char *)streamer;
    nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    sections = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!memcmp(sections[i].Name, ".text", 5)) {
            text = base + sections[i].VirtualAddress;
            text_size = sections[i].Misc.VirtualSize;
        }
    }
    if (!text || !text_size) return 1;
    before = malloc(text_size);
    if (!before) return 1;
    memcpy(before, text, text_size);
    expected[0] = strtoul(argv[2], NULL, 0) + 36;
    expected[1] = strtoul(argv[3], NULL, 0) + 16;
    expected[2] = strtoul(argv[4], NULL, 0) + 9;
    expected[3] = strtoul(argv[4], NULL, 0) + 50;
    expected[4] = strtoul(argv[5], NULL, 0) + 40;
    expected[5] = strtoul(argv[5], NULL, 0) + 49;
    expected[6] = strtoul(argv[5], NULL, 0) + 119;
    expected[7] = strtoul(argv[5], NULL, 0) + 128;
    slot = strtoul(argv[6], NULL, 0);
    original_slot = *(uintptr_t *)(base + slot);
    if (!check_scaler(base + strtoul(argv[5], NULL, 0), 0)) return 1;
    hook = LoadLibraryA(argv[1]);
    if (!hook) { fprintf(stderr, "hook load failed: %lu\n", GetLastError()); return 1; }
    initialize = (initialize_fn)(uintptr_t)GetProcAddress(hook, "UURemoteInputHookInitialize");
    if (!initialize) return 1;
    /* No input endpoint is created in this probe. Initialization still verifies
     * and installs the streamer adaptations before returning endpoint status. */
    initialize(NULL);
    GetPrivateProfileStringA("encoder", "adapter_mode", "", mode, sizeof(mode),
                            "C:\\uu-remote-wayland-frame-status.ini");
    if (strcmp(mode, "native-device-pointer")) {
        fprintf(stderr, "native GPUDevice call chain not recognized\n");
        return 1;
    }
    for (i = 0; i < text_size; ++i) {
        if (text[i] != before[i]) {
            DWORD rva = (DWORD)(text + i - base);
            SIZE_T j;
            for (j = 0; j < 8 && expected[j] != rva; ++j) {}
            if (j == 8 || text[i] != values[j]) {
                fprintf(stderr, "unexpected executable change at RVA 0x%lx\n", rva);
                return 1;
            }
            ++changed;
        }
    }
    if (changed != 8) { fprintf(stderr, "portrait fields changed: %llu, expected 8\n", (unsigned long long)changed); return 1; }
    if (*(uintptr_t *)(base + slot) != original_slot) {
        fprintf(stderr, "native GPUDevice pointer method was overwritten\n");
        return 1;
    }
    if (!check_scaler(base + strtoul(argv[5], NULL, 0), 1)) return 1;
    memcpy(before, text, text_size);
    initialize(NULL);
    if (memcmp(before, text, text_size)) { fprintf(stderr, "second initialization changed code\n"); return 1; }
    free(before);
    printf("CURRENT_STREAMER_OK\tportrait_fields=8\tnative_gpu_pointer_preserved=1\tidempotent=1\tportrait_math_cases=4\n");
    return 0;
}
