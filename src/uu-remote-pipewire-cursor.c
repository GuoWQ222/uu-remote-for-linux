/*
 * Extract Wayland cursor bitmaps from an XDG ScreenCast PipeWire stream.
 *
 * GStreamer's pipewiresrc negotiates SPA_META_Cursor but currently converts
 * it to a position-only ROI, discarding spa_meta_bitmap.  This sidecar opens
 * the same restricted Portal remote, consumes only metadata, and publishes
 * the compositor-selected cursor in the UUCI file format understood by the
 * Wine capture hook.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>
#include <spa/param/format-utils.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/utils/defs.h>
#include <spa/utils/result.h>

#define UUCI_MAGIC 0x49435555u
#define UUCI_VERSION 1u
#define UUCI_HEADER_SIZE 64u
#define UUCI_MAX_DIMENSION 512u
#define UUCI_MAX_STREAMS 16u
#define UUCI_META_SIZE(width, height) \
    (sizeof(struct spa_meta_cursor) + \
     sizeof(struct spa_meta_bitmap) + (width) * (height) * 4u)

struct cursor_file_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t sequence;
    uint32_t width;
    uint32_t height;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t pixel_size;
    unsigned char reserved[28];
};

_Static_assert(
    sizeof(struct cursor_file_header) == UUCI_HEADER_SIZE,
    "UUCI header must remain 64 bytes"
);

struct cursor_snapshot {
    uint32_t width;
    uint32_t height;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t pixel_size;
    unsigned char *pixels;
};

struct cursor_event {
    uint32_t id;
    int32_t position_x;
    int32_t position_y;
    bool has_bitmap;
    struct cursor_snapshot snapshot;
};

struct application;

struct stream_data {
    struct application *application;
    struct pw_stream *stream;
    struct spa_hook listener;
    uint32_t node;
    uint32_t width;
    uint32_t height;
    struct cursor_snapshot cursor;
    bool has_cursor;
};

struct application {
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct stream_data streams[UUCI_MAX_STREAMS];
    uint32_t n_streams;
    const char *output_path;
    uint32_t sequence;
    uint64_t last_hash;
    uint32_t updates;
    bool failed;
};

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_snapshot(const struct cursor_snapshot *snapshot)
{
    uint32_t metadata[4];
    uint64_t hash = UINT64_C(1469598103934665603);

    metadata[0] = snapshot->width;
    metadata[1] = snapshot->height;
    metadata[2] = snapshot->hotspot_x;
    metadata[3] = snapshot->hotspot_y;
    hash = hash_bytes(hash, metadata, sizeof(metadata));
    return hash_bytes(hash, snapshot->pixels, snapshot->pixel_size);
}

static bool convert_pixel(
    uint32_t format,
    const unsigned char *source,
    unsigned char *destination
)
{
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRA:
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = source[3];
        return true;
    case SPA_VIDEO_FORMAT_RGBA:
        destination[0] = source[2];
        destination[1] = source[1];
        destination[2] = source[0];
        destination[3] = source[3];
        return true;
    case SPA_VIDEO_FORMAT_ARGB:
        destination[0] = source[3];
        destination[1] = source[2];
        destination[2] = source[1];
        destination[3] = source[0];
        return true;
    case SPA_VIDEO_FORMAT_ABGR:
        destination[0] = source[1];
        destination[1] = source[2];
        destination[2] = source[3];
        destination[3] = source[0];
        return true;
    case SPA_VIDEO_FORMAT_BGRx:
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = 0xff;
        return true;
    case SPA_VIDEO_FORMAT_RGBx:
        destination[0] = source[2];
        destination[1] = source[1];
        destination[2] = source[0];
        destination[3] = 0xff;
        return true;
    case SPA_VIDEO_FORMAT_xBGR:
        destination[0] = source[1];
        destination[1] = source[2];
        destination[2] = source[3];
        destination[3] = 0xff;
        return true;
    case SPA_VIDEO_FORMAT_xRGB:
        destination[0] = source[3];
        destination[1] = source[2];
        destination[2] = source[1];
        destination[3] = 0xff;
        return true;
    default:
        return false;
    }
}

static bool cursor_inside_stream(
    const struct stream_data *stream_data,
    const struct cursor_event *event
)
{
    return event->position_x >= 0 && event->position_y >= 0 &&
        (uint32_t)event->position_x < stream_data->width &&
        (uint32_t)event->position_y < stream_data->height;
}

static int extract_cursor(
    const struct spa_meta *meta,
    struct cursor_event *event
)
{
    const unsigned char *meta_bytes;
    const struct spa_meta_cursor *cursor;
    const struct spa_meta_bitmap *bitmap;
    const unsigned char *bitmap_bytes;
    uint64_t bitmap_position;
    uint64_t last_byte;
    uint32_t row;
    uint32_t column;
    size_t source_stride;

    memset(event, 0, sizeof(*event));
    if (!meta || !meta->data || meta->size < sizeof(*cursor)) {
        return 0;
    }
    meta_bytes = meta->data;
    cursor = (const struct spa_meta_cursor *)meta_bytes;
    if (!spa_meta_cursor_is_valid(cursor)) {
        return 0;
    }
    event->id = cursor->id;
    event->position_x = cursor->position.x;
    event->position_y = cursor->position.y;
    if (cursor->bitmap_offset == 0) {
        return 1;
    }
    if (
        cursor->bitmap_offset < sizeof(*cursor) ||
        cursor->bitmap_offset > meta->size - sizeof(*bitmap)
    ) {
        return -EINVAL;
    }
    bitmap = (const struct spa_meta_bitmap *)(
        meta_bytes + cursor->bitmap_offset
    );
    if (
        !spa_meta_bitmap_is_valid(bitmap) ||
        bitmap->offset < sizeof(*bitmap) ||
        bitmap->size.width == 0 ||
        bitmap->size.width > UUCI_MAX_DIMENSION ||
        bitmap->size.height == 0 ||
        bitmap->size.height > UUCI_MAX_DIMENSION ||
        bitmap->stride <= 0 ||
        (uint32_t)bitmap->stride < bitmap->size.width * 4u
    ) {
        return -EINVAL;
    }
    bitmap_position = (uint64_t)cursor->bitmap_offset + bitmap->offset;
    source_stride = (size_t)bitmap->stride;
    last_byte = bitmap_position +
        (uint64_t)(bitmap->size.height - 1u) * source_stride +
        (uint64_t)bitmap->size.width * 4u;
    if (bitmap_position > meta->size || last_byte > meta->size) {
        return -EINVAL;
    }
    if (
        cursor->hotspot.x < 0 || cursor->hotspot.y < 0 ||
        (uint32_t)cursor->hotspot.x >= bitmap->size.width ||
        (uint32_t)cursor->hotspot.y >= bitmap->size.height
    ) {
        return -EINVAL;
    }

    event->snapshot.width = bitmap->size.width;
    event->snapshot.height = bitmap->size.height;
    event->snapshot.hotspot_x = (uint32_t)cursor->hotspot.x;
    event->snapshot.hotspot_y = (uint32_t)cursor->hotspot.y;
    event->snapshot.pixel_size =
        event->snapshot.width * event->snapshot.height * 4u;
    event->snapshot.pixels = malloc(event->snapshot.pixel_size);
    if (!event->snapshot.pixels) {
        return -ENOMEM;
    }
    bitmap_bytes = meta_bytes + bitmap_position;
    for (row = 0; row < event->snapshot.height; ++row) {
        const unsigned char *source = bitmap_bytes + row * source_stride;
        unsigned char *destination = event->snapshot.pixels +
            row * event->snapshot.width * 4u;
        for (column = 0; column < event->snapshot.width; ++column) {
            unsigned char *pixel = destination + column * 4u;
            if (!convert_pixel(
                    bitmap->format,
                    source + column * 4u,
                    pixel
                )) {
                free(event->snapshot.pixels);
                event->snapshot.pixels = NULL;
                return -ENOTSUP;
            }
            /*
             * Mutter may leave RGB bytes undefined when alpha is zero.
             * Canonicalize them so identical cursor shapes do not consume a
             * new Win32 HCURSOR cache entry for invisible pixel noise.
             */
            if (pixel[3] == 0) {
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 0;
            }
        }
    }
    event->has_bitmap = true;
    return 2;
}

static int write_all(int descriptor, const void *data, size_t size)
{
    const unsigned char *bytes = data;

    while (size > 0) {
        ssize_t written = write(descriptor, bytes, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        bytes += written;
        size -= (size_t)written;
    }
    return 0;
}

static int publish_snapshot(
    struct application *application,
    const struct stream_data *stream_data,
    const struct cursor_event *event
)
{
    struct cursor_file_header header;
    const struct cursor_snapshot *snapshot = &stream_data->cursor;
    char temporary[PATH_MAX];
    uint64_t hash;
    int descriptor;
    int result;

    hash = hash_snapshot(snapshot);
    if (application->last_hash != 0 && application->last_hash == hash) {
        return 0;
    }
    application->sequence += 1u;
    if (application->sequence == 0) {
        application->sequence = 1u;
    }
    memset(&header, 0, sizeof(header));
    header.magic = UUCI_MAGIC;
    header.version = UUCI_VERSION;
    header.header_size = sizeof(header);
    header.sequence = application->sequence;
    header.width = snapshot->width;
    header.height = snapshot->height;
    header.hotspot_x = snapshot->hotspot_x;
    header.hotspot_y = snapshot->hotspot_y;
    header.pixel_size = snapshot->pixel_size;
    if (snprintf(
            temporary,
            sizeof(temporary),
            "%s.%ld.tmp",
            application->output_path,
            (long)getpid()
        ) >= (int)sizeof(temporary)) {
        return -ENAMETOOLONG;
    }
    descriptor = open(
        temporary,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        S_IRUSR | S_IWUSR
    );
    if (descriptor < 0) {
        return -errno;
    }
    result = write_all(descriptor, &header, sizeof(header));
    if (result == 0) {
        result = write_all(
            descriptor,
            snapshot->pixels,
            snapshot->pixel_size
        );
    }
    if (close(descriptor) < 0 && result == 0) {
        result = -errno;
    }
    if (result == 0 && rename(temporary, application->output_path) < 0) {
        result = -errno;
    }
    if (result != 0) {
        unlink(temporary);
        return result;
    }
    application->last_hash = hash;
    application->updates += 1u;
    fprintf(
        stderr,
        "cursor-update sequence=%" PRIu32 " size=%" PRIu32 "x%" PRIu32
        " hotspot=%" PRIu32 ",%" PRIu32 " node=%" PRIu32
        " cursor-id=%" PRIu32 " position=%" PRId32 ",%" PRId32 "\n",
        header.sequence,
        header.width,
        header.height,
        header.hotspot_x,
        header.hotspot_y,
        stream_data->node,
        event->id,
        event->position_x,
        event->position_y
    );
    fflush(stderr);
    return 1;
}

static void process_cursor_meta(
    struct stream_data *stream_data,
    const struct spa_buffer *buffer
)
{
    struct cursor_event event;
    struct spa_meta *meta;
    int result;

    meta = spa_buffer_find_meta(buffer, SPA_META_Cursor);
    result = extract_cursor(meta, &event);
    if (result <= 0) {
        if (result < 0 && result != -ENOTSUP) {
            fprintf(stderr, "invalid cursor metadata: %s\n", strerror(-result));
        }
        return;
    }
    if (event.has_bitmap) {
        free(stream_data->cursor.pixels);
        stream_data->cursor = event.snapshot;
        stream_data->has_cursor = true;
        event.snapshot.pixels = NULL;
    }
    if (!cursor_inside_stream(stream_data, &event)) {
        free(event.snapshot.pixels);
        return;
    }
    if (!stream_data->has_cursor) {
        free(event.snapshot.pixels);
        return;
    }
    result = publish_snapshot(
        stream_data->application,
        stream_data,
        &event
    );
    free(event.snapshot.pixels);
    if (result < 0) {
        fprintf(stderr, "cursor publication failed: %s\n", strerror(-result));
    }
}

static void on_process(void *userdata)
{
    struct stream_data *stream_data = userdata;
    struct pw_buffer *latest = NULL;
    struct pw_buffer *current;

    while ((current = pw_stream_dequeue_buffer(stream_data->stream))) {
        if (latest) {
            pw_stream_queue_buffer(stream_data->stream, latest);
        }
        latest = current;
    }
    if (!latest) {
        return;
    }
    process_cursor_meta(stream_data, latest->buffer);
    pw_stream_queue_buffer(stream_data->stream, latest);
}

static void on_state_changed(
    void *userdata,
    enum pw_stream_state old,
    enum pw_stream_state state,
    const char *error
)
{
    struct stream_data *stream_data = userdata;

    (void)old;
    if (state == PW_STREAM_STATE_ERROR) {
        fprintf(
            stderr,
            "PipeWire node %" PRIu32 " failed: %s\n",
            stream_data->node,
            error ? error : "unknown error"
        );
        stream_data->application->failed = true;
        pw_main_loop_quit(stream_data->application->loop);
    }
}

static void on_param_changed(
    void *userdata,
    uint32_t id,
    const struct spa_pod *param
)
{
    struct stream_data *stream_data = userdata;
    struct spa_video_info_raw raw;
    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(
        buffer,
        sizeof(buffer)
    );
    const struct spa_pod *params[2];
    uint32_t data_types;
    uint32_t width;
    uint32_t height;
    uint64_t image_size;

    if (!param || id != SPA_PARAM_Format) {
        return;
    }
    memset(&raw, 0, sizeof(raw));
    if (spa_format_video_raw_parse(param, &raw) < 0) {
        return;
    }
    width = raw.size.width ? raw.size.width : stream_data->width;
    height = raw.size.height ? raw.size.height : stream_data->height;
    image_size = (uint64_t)width * height * 4u;
    if (image_size == 0 || image_size > INT32_MAX) {
        pw_stream_set_error(
            stream_data->stream,
            -EINVAL,
            "invalid video dimensions"
        );
        return;
    }
    data_types = (1u << SPA_DATA_MemPtr) |
        (1u << SPA_DATA_MemFd) |
        (1u << SPA_DATA_DmaBuf);
    params[0] = spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_ParamBuffers,
        SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers,
        SPA_POD_CHOICE_RANGE_Int(2, 2, 4),
        SPA_PARAM_BUFFERS_blocks,
        SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size,
        SPA_POD_Int((int32_t)image_size),
        SPA_PARAM_BUFFERS_stride,
        SPA_POD_Int((int32_t)(width * 4u)),
        SPA_PARAM_BUFFERS_dataType,
        SPA_POD_CHOICE_FLAGS_Int(data_types)
    );
    params[1] = spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_ParamMeta,
        SPA_PARAM_Meta,
        SPA_PARAM_META_type,
        SPA_POD_Id(SPA_META_Cursor),
        SPA_PARAM_META_size,
        SPA_POD_CHOICE_RANGE_Int(
            UUCI_META_SIZE(384u, 384u),
            sizeof(struct spa_meta_cursor),
            UUCI_META_SIZE(UUCI_MAX_DIMENSION, UUCI_MAX_DIMENSION)
        )
    );
    pw_stream_update_params(stream_data->stream, params, 2);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .process = on_process,
};

static int connect_stream(struct stream_data *stream_data)
{
    struct application *application = stream_data->application;
    struct pw_properties *properties;
    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(
        buffer,
        sizeof(buffer)
    );
    const struct spa_pod *params[1];
    struct spa_rectangle size = SPA_RECTANGLE(
        stream_data->width,
        stream_data->height
    );
    int result;

    properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE,
        "Video",
        PW_KEY_MEDIA_CATEGORY,
        "Capture",
        PW_KEY_MEDIA_ROLE,
        "Screen",
        NULL
    );
    stream_data->stream = pw_stream_new(
        application->core,
        "uu-remote-wayland-cursor",
        properties
    );
    if (!stream_data->stream) {
        return -errno;
    }
    pw_stream_add_listener(
        stream_data->stream,
        &stream_data->listener,
        &stream_events,
        stream_data
    );
    params[0] = spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_Format,
        SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType,
        SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype,
        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(
            8,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_BGRA,
            SPA_VIDEO_FORMAT_RGBx,
            SPA_VIDEO_FORMAT_RGBA,
            SPA_VIDEO_FORMAT_xRGB,
            SPA_VIDEO_FORMAT_ARGB,
            SPA_VIDEO_FORMAT_xBGR,
            SPA_VIDEO_FORMAT_ABGR
        ),
        SPA_FORMAT_VIDEO_size,
        SPA_POD_Rectangle(&size)
    );
    result = pw_stream_connect(
        stream_data->stream,
        PW_DIRECTION_INPUT,
        stream_data->node,
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
        params,
        1
    );
    return result;
}

static void quit_signal(void *userdata, int signal_number)
{
    struct application *application = userdata;

    (void)signal_number;
    pw_main_loop_quit(application->loop);
}

static int self_test(void)
{
    unsigned char storage[
        sizeof(struct spa_meta_cursor) +
        sizeof(struct spa_meta_bitmap) + 16u
    ];
    struct spa_meta meta;
    struct spa_meta_cursor *cursor;
    struct spa_meta_bitmap *bitmap;
    unsigned char *pixels;
    struct cursor_event event;
    struct stream_data stream_data;
    int result;

    memset(storage, 0, sizeof(storage));
    cursor = (struct spa_meta_cursor *)storage;
    cursor->id = 1;
    cursor->hotspot.x = 1;
    cursor->hotspot.y = 0;
    cursor->bitmap_offset = sizeof(*cursor);
    bitmap = (struct spa_meta_bitmap *)(storage + cursor->bitmap_offset);
    bitmap->format = SPA_VIDEO_FORMAT_RGBA;
    bitmap->size = SPA_RECTANGLE(2, 2);
    bitmap->stride = 8;
    bitmap->offset = sizeof(*bitmap);
    pixels = (unsigned char *)bitmap + bitmap->offset;
    pixels[0] = 0x11;
    pixels[1] = 0x22;
    pixels[2] = 0x33;
    pixels[3] = 0x44;
    pixels[4] = 0x55;
    pixels[5] = 0x66;
    pixels[6] = 0x77;
    pixels[7] = 0x88;
    meta.type = SPA_META_Cursor;
    meta.size = sizeof(storage);
    meta.data = storage;
    result = extract_cursor(&meta, &event);
    if (
        result != 2 || !event.has_bitmap ||
        event.snapshot.width != 2 || event.snapshot.height != 2 ||
        event.snapshot.hotspot_x != 1 || event.snapshot.hotspot_y != 0 ||
        event.snapshot.pixel_size != 16 ||
        event.snapshot.pixels[0] != 0x33 ||
        event.snapshot.pixels[1] != 0x22 ||
        event.snapshot.pixels[2] != 0x11 ||
        event.snapshot.pixels[3] != 0x44
    ) {
        free(event.snapshot.pixels);
        fprintf(stderr, "PipeWire cursor metadata self-test failed\n");
        return 1;
    }
    free(event.snapshot.pixels);

    memset(&stream_data, 0, sizeof(stream_data));
    stream_data.width = 2560;
    stream_data.height = 1440;
    event.position_x = 2559;
    event.position_y = 1439;
    if (!cursor_inside_stream(&stream_data, &event)) {
        fprintf(stderr, "PipeWire active cursor stream self-test failed\n");
        return 1;
    }
    event.position_x = -1;
    if (cursor_inside_stream(&stream_data, &event)) {
        fprintf(stderr, "PipeWire inactive cursor stream self-test failed\n");
        return 1;
    }
    printf("PipeWire cursor metadata self-test passed\n");
    return 0;
}

static bool parse_unsigned(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (
        errno != 0 || !end || *end != '\0' || parsed == 0 ||
        parsed > UINT32_MAX
    ) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    struct application application;
    char *end = NULL;
    long descriptor;
    int index;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return self_test();
    }
    if (argc < 6 || (argc - 3) % 3 != 0) {
        fprintf(
            stderr,
            "usage: %s OUTPUT FD NODE WIDTH HEIGHT [NODE WIDTH HEIGHT ...]\n",
            argv[0]
        );
        return 2;
    }
    memset(&application, 0, sizeof(application));
    application.output_path = argv[1];
    errno = 0;
    descriptor = strtol(argv[2], &end, 10);
    if (
        errno != 0 || !end || *end != '\0' || descriptor < 0 ||
        descriptor > INT_MAX
    ) {
        fprintf(stderr, "invalid PipeWire descriptor\n");
        return 2;
    }
    application.n_streams = (uint32_t)((argc - 3) / 3);
    if (
        application.n_streams == 0 ||
        application.n_streams > UUCI_MAX_STREAMS
    ) {
        fprintf(stderr, "invalid PipeWire stream count\n");
        return 2;
    }
    for (index = 0; index < (int)application.n_streams; ++index) {
        struct stream_data *stream_data = &application.streams[index];
        stream_data->application = &application;
        if (
            !parse_unsigned(argv[3 + index * 3], &stream_data->node) ||
            !parse_unsigned(argv[4 + index * 3], &stream_data->width) ||
            !parse_unsigned(argv[5 + index * 3], &stream_data->height)
        ) {
            fprintf(stderr, "invalid PipeWire stream descriptor\n");
            return 2;
        }
    }

    pw_init(&argc, &argv);
    application.loop = pw_main_loop_new(NULL);
    if (!application.loop) {
        fprintf(stderr, "unable to create PipeWire main loop\n");
        goto cleanup;
    }
    application.context = pw_context_new(
        pw_main_loop_get_loop(application.loop),
        NULL,
        0
    );
    if (!application.context) {
        fprintf(stderr, "unable to create PipeWire context\n");
        goto cleanup;
    }
    application.core = pw_context_connect_fd(
        application.context,
        (int)descriptor,
        NULL,
        0
    );
    if (!application.core) {
        fprintf(stderr, "unable to connect Portal PipeWire remote: %m\n");
        goto cleanup;
    }
    pw_loop_add_signal(
        pw_main_loop_get_loop(application.loop),
        SIGINT,
        quit_signal,
        &application
    );
    pw_loop_add_signal(
        pw_main_loop_get_loop(application.loop),
        SIGTERM,
        quit_signal,
        &application
    );
    for (index = 0; index < (int)application.n_streams; ++index) {
        result = connect_stream(&application.streams[index]);
        if (result < 0) {
            fprintf(
                stderr,
                "unable to connect PipeWire node %" PRIu32 ": %s\n",
                application.streams[index].node,
                spa_strerror(result)
            );
            application.failed = true;
            goto cleanup;
        }
    }
    fprintf(
        stderr,
        "cursor-bridge-ready streams=%" PRIu32 "\n",
        application.n_streams
    );
    fflush(stderr);
    pw_main_loop_run(application.loop);
    result = application.failed ? 1 : 0;

cleanup:
    for (index = 0; index < (int)application.n_streams; ++index) {
        free(application.streams[index].cursor.pixels);
        if (application.streams[index].stream) {
            pw_stream_destroy(application.streams[index].stream);
        }
    }
    if (application.core) {
        pw_core_disconnect(application.core);
    }
    if (application.context) {
        pw_context_destroy(application.context);
    }
    if (application.loop) {
        pw_main_loop_destroy(application.loop);
    }
    pw_deinit();
    return result;
}
