/* Exercise the production MemFd reader, including padding, offsets and bounds. */
#define _GNU_SOURCE
#define main capture_helper_main
#include "../src/uu-remote-pipewire-capture.c"
#undef main
#include <assert.h>

int main(void)
{
    struct application app = {0};
    unsigned char output[32];
    unsigned char input[64] = {0};
    struct spa_chunk chunk = {.offset = 4, .size = 28, .stride = 16};
    int fd = memfd_create("capture-cpu-test", MFD_CLOEXEC);
    struct spa_data data = {.type = SPA_DATA_MemFd, .fd = fd,
                            .mapoffset = 4, .maxsize = 60, .chunk = &chunk};
    assert(fd >= 0);
    app.source_width = app.width = 3;
    app.source_height = app.height = 2;
    app.drm_format = DRM_FORMAT_ARGB8888;
    for (unsigned int y = 0; y < 2; ++y)
        for (unsigned int x = 0; x < 3; ++x)
            for (unsigned int c = 0; c < 4; ++c)
                input[8 + y * 16 + x * 4 + c] = y * 40 + x * 8 + c;
    assert(write(fd, input, sizeof(input)) == sizeof(input));
    memset(output, 0xaa, sizeof(output));
    assert(read_shared_frame(&app, &data, output));
    assert(!memcmp(output, input + 8, 12));
    assert(!memcmp(output + 12, input + 24, 12));
    assert(output[24] == 0xaa);
    app.drm_format = DRM_FORMAT_XRGB8888;
    assert(read_shared_frame(&app, &data, output));
    for (unsigned int i = 0; i < 6; ++i) assert(output[i * 4 + 3] == 255);
    app.width = 1;
    app.height = 1;
    app.drm_format = DRM_FORMAT_ARGB8888;
    assert(read_shared_frame(&app, &data, output));
    for (unsigned int c = 0; c < 4; ++c) assert(output[c] == 28 + c);
    /* Upscale a one-pixel source; no reads beyond the source rectangle. */
    app.source_width = app.source_height = 1;
    app.width = app.height = 2;
    assert(read_shared_frame(&app, &data, output));
    for (unsigned int i = 0; i < 4; ++i) assert(!memcmp(output + i * 4, input + 8, 4));
    data.type = SPA_DATA_DmaBuf;
    assert(!read_shared_frame(&app, &data, output));
    data.type = SPA_DATA_MemFd;
    chunk.stride = -16;
    assert(!read_shared_frame(&app, &data, output));
    chunk.stride = 2;
    assert(!read_shared_frame(&app, &data, output));
    chunk.stride = 16;
    chunk.size = 3;
    assert(!read_shared_frame(&app, &data, output));
    chunk.size = 28;
    data.maxsize = 7;
    assert(!read_shared_frame(&app, &data, output));
    data.maxsize = 60;
    assert(ftruncate(fd, 11) == 0);
    assert(!read_shared_frame(&app, &data, output));
    close(fd);
    /* Software formats must omit the mandatory DMA-BUF modifier entirely. */
    setenv("UU_REMOTE_CAPTURE_MODE", "cpu", 1);
    uint8_t storage[2048];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
    struct modifier_set modifiers = {0};
    struct spa_rectangle size = SPA_RECTANGLE(1920, 1080);
    const struct spa_pod *format = build_capture_format(&builder, SPA_VIDEO_FORMAT_BGRx,
                                                       &modifiers, &size);
    assert(!spa_pod_find_prop(format, NULL, SPA_FORMAT_VIDEO_modifier));
    puts("CPU capture pixel, bounds and format tests passed");
    return 0;
}
