/*
 * Native PipeWire DMA-BUF screen capture for the Wayland bridge.
 *
 * This helper negotiates SPA_DATA_DmaBuf directly, imports each producer
 * buffer into EGL, reads it into one of three independent staging slots, and
 * queues the PipeWire buffer back before notifying the Python consumer.  The
 * Python bridge therefore never maps compositor-owned video buffers and does
 * not depend on a second media framework or a CPU-copy fallback.
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
#include <sys/mman.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>
#include <spa/param/format-utils.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#define UUDC_STAGE_SLOTS 3u
#define UUDC_PIPEWIRE_BUFFERS 3
#define UUDC_MAX_MODIFIERS 64u
#define UUDC_NOTIFICATION_MAGIC UINT32_C(0x43445555)

struct frame_notification {
    uint32_t magic;
    uint32_t slot;
    uint32_t sequence;
    uint32_t stride;
    uint64_t pts;
};

_Static_assert(
    sizeof(struct frame_notification) == 24,
    "native capture notification ABI must stay fixed"
);

struct image_cache {
    int fd;
    uint32_t stride;
    uint32_t offset;
    uint64_t modifier;
    EGLImageKHR image;
    GLuint texture;
    GLuint framebuffer;
};

struct modifier_set {
    uint32_t drm_format;
    uint64_t values[UUDC_MAX_MODIFIERS];
    uint32_t count;
};

struct application {
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct spa_hook core_listener;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    uint32_t node;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t drm_format;
    uint64_t modifier;
    int stage_fd;
    int notify_fd;
    int release_fd;
    unsigned char *stage;
    size_t frame_size;
    size_t stage_size;
    bool slot_free[UUDC_STAGE_SLOTS];
    uint32_t next_slot;
    uint32_t sequence;
    uint64_t frames;
    uint64_t dropped;
    bool failed;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    PFNEGLCREATEIMAGEKHRPROC egl_create_image;
    PFNEGLDESTROYIMAGEKHRPROC egl_destroy_image;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture;
    struct image_cache images[UUDC_PIPEWIRE_BUFFERS];
    unsigned char *rgba_scratch;
    bool direct_bgra_read;
    bool modifier_import;
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC query_dma_buf_modifiers;
    struct modifier_set modifier_sets[2];
    char egl_device[PATH_MAX];
};

static struct application *signal_application;

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_fd(const char *text, int *value)
{
    uint32_t parsed;

    if (!parse_u32(text, &parsed) || parsed > INT32_MAX) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static void quit_signal(int signal_number)
{
    if (signal_application && signal_application->loop) {
        fprintf(stderr, "native capture received signal %d\n", signal_number);
        pw_main_loop_quit(signal_application->loop);
    }
}

static bool has_extension(const char *extensions, const char *needle)
{
    const char *position = extensions;
    size_t length = strlen(needle);

    if (!extensions || !needle[0] || strchr(needle, ' ')) {
        return false;
    }
    while ((position = strstr(position, needle))) {
        if ((position == extensions || position[-1] == ' ') &&
            (position[length] == '\0' || position[length] == ' ')) {
            return true;
        }
        position += length;
    }
    return false;
}

static bool is_nvidia_render_node(const char *path)
{
    const char *name;
    char vendor_path[PATH_MAX];
    char vendor[32];
    FILE *handle;

    if (!path || !path[0]) {
        return false;
    }
    name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (snprintf(
            vendor_path,
            sizeof(vendor_path),
            "/sys/class/drm/%s/device/vendor",
            name
        ) < 0) {
        return false;
    }
    handle = fopen(vendor_path, "r");
    if (!handle) {
        return false;
    }
    vendor[0] = '\0';
    if (!fgets(vendor, sizeof(vendor), handle)) {
        fclose(handle);
        return false;
    }
    fclose(handle);
    return strncmp(vendor, "0x10de", 6) == 0;
}

static EGLDisplay choose_egl_display(struct application *application)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    PFNEGLQUERYDEVICESEXTPROC query_devices;
    PFNEGLQUERYDEVICESTRINGEXTPROC query_device_string;
    EGLDeviceEXT devices[16];
    EGLint count = 0;
    EGLint index;
    EGLDeviceEXT selected = EGL_NO_DEVICE_EXT;
    const char *selected_path = NULL;

    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    query_devices = (PFNEGLQUERYDEVICESEXTPROC)
        eglGetProcAddress("eglQueryDevicesEXT");
    query_device_string = (PFNEGLQUERYDEVICESTRINGEXTPROC)
        eglGetProcAddress("eglQueryDeviceStringEXT");
    if (get_platform_display && query_devices && query_device_string &&
        query_devices(16, devices, &count)) {
        for (index = 0; index < count; ++index) {
            const char *path = query_device_string(
                devices[index],
                EGL_DRM_RENDER_NODE_FILE_EXT
            );

            if (!path) {
                path = query_device_string(
                    devices[index],
                    EGL_DRM_DEVICE_FILE_EXT
                );
            }
            if (!selected && path) {
                selected = devices[index];
                selected_path = path;
            }
            if (is_nvidia_render_node(path)) {
                selected = devices[index];
                selected_path = path;
                break;
            }
        }
        if (selected) {
            if (selected_path) {
                snprintf(
                    application->egl_device,
                    sizeof(application->egl_device),
                    "%s",
                    selected_path
                );
            }
            return get_platform_display(
                EGL_PLATFORM_DEVICE_EXT,
                selected,
                NULL
            );
        }
    }
    if (get_platform_display) {
        snprintf(
            application->egl_device,
            sizeof(application->egl_device),
            "%s",
            "surfaceless-default"
        );
        return get_platform_display(
            EGL_PLATFORM_SURFACELESS_MESA,
            EGL_DEFAULT_DISPLAY,
            NULL
        );
    }
    snprintf(
        application->egl_device,
        sizeof(application->egl_device),
        "%s",
        "egl-default"
    );
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static void destroy_image_cache(
    struct application *application,
    struct image_cache *cache
)
{
    if (cache->framebuffer) {
        glDeleteFramebuffers(1, &cache->framebuffer);
    }
    if (cache->texture) {
        glDeleteTextures(1, &cache->texture);
    }
    if (cache->image != EGL_NO_IMAGE_KHR && cache->image) {
        application->egl_destroy_image(application->egl_display, cache->image);
    }
    memset(cache, 0, sizeof(*cache));
    cache->fd = -1;
    cache->image = EGL_NO_IMAGE_KHR;
}

static void add_modifier(struct modifier_set *set, uint64_t modifier)
{
    uint32_t index;

    for (index = 0; index < set->count; ++index) {
        if (set->values[index] == modifier) {
            return;
        }
    }
    if (set->count < UUDC_MAX_MODIFIERS) {
        set->values[set->count++] = modifier;
    }
}

static void query_importable_modifiers(
    struct application *application,
    struct modifier_set *set,
    uint32_t drm_format
)
{
    EGLuint64KHR modifiers[UUDC_MAX_MODIFIERS];
    EGLBoolean external_only[UUDC_MAX_MODIFIERS];
    EGLint available = 0;
    EGLint returned = 0;
    EGLint index;

    memset(set, 0, sizeof(*set));
    set->drm_format = drm_format;
    if (application->query_dma_buf_modifiers &&
        application->query_dma_buf_modifiers(
            application->egl_display,
            (EGLint)drm_format,
            0,
            NULL,
            NULL,
            &available
        ) && available > 0 &&
        application->query_dma_buf_modifiers(
            application->egl_display,
            (EGLint)drm_format,
            SPA_MIN(available, (EGLint)UUDC_MAX_MODIFIERS),
            modifiers,
            external_only,
            &returned
        )) {
        returned = SPA_MIN(returned, (EGLint)UUDC_MAX_MODIFIERS);
        for (index = 0; index < returned; ++index) {
            /* GL_TEXTURE_EXTERNAL_OES images cannot back our read FBO. */
            if (external_only[index] == EGL_FALSE) {
                add_modifier(set, (uint64_t)modifiers[index]);
            }
        }
    }
    /* Always retain the legacy implicit-modifier path as a final fallback. */
    add_modifier(set, DRM_FORMAT_MOD_INVALID);
    fprintf(
        stderr,
        "native capture DRM format 0x%08" PRIx32
        " importable modifiers=%" PRIu32 "\n",
        drm_format,
        set->count
    );
}

static bool initialize_egl(struct application *application)
{
    EGLConfig config;
    EGLint count = 0;
    EGLint major = 0;
    EGLint minor = 0;
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    const EGLint surface_attributes[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE,
    };
    const char *egl_extensions;
    const char *gl_extensions;
    GLint read_format = 0;
    uint32_t index;

    application->egl_display = EGL_NO_DISPLAY;
    application->egl_context = EGL_NO_CONTEXT;
    application->egl_surface = EGL_NO_SURFACE;
    for (index = 0; index < UUDC_PIPEWIRE_BUFFERS; ++index) {
        application->images[index].fd = -1;
        application->images[index].image = EGL_NO_IMAGE_KHR;
    }

    application->egl_display = choose_egl_display(application);
    if (application->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(application->egl_display, &major, &minor)) {
        fprintf(stderr, "unable to initialize a surfaceless EGL display\n");
        return false;
    }
    egl_extensions = eglQueryString(application->egl_display, EGL_EXTENSIONS);
    if (!has_extension(egl_extensions, "EGL_EXT_image_dma_buf_import")) {
        fprintf(stderr, "EGL_EXT_image_dma_buf_import is unavailable\n");
        return false;
    }
    application->modifier_import = has_extension(
        egl_extensions,
        "EGL_EXT_image_dma_buf_import_modifiers"
    );
    if (application->modifier_import) {
        application->query_dma_buf_modifiers =
            (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)
            eglGetProcAddress("eglQueryDmaBufModifiersEXT");
    }
    query_importable_modifiers(
        application,
        &application->modifier_sets[0],
        DRM_FORMAT_ARGB8888
    );
    query_importable_modifiers(
        application,
        &application->modifier_sets[1],
        DRM_FORMAT_XRGB8888
    );
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(
            application->egl_display,
            config_attributes,
            &config,
            1,
            &count
        ) || count != 1) {
        fprintf(stderr, "unable to choose an EGL GLES configuration\n");
        return false;
    }
    application->egl_context = eglCreateContext(
        application->egl_display,
        config,
        EGL_NO_CONTEXT,
        context_attributes
    );
    application->egl_surface = eglCreatePbufferSurface(
        application->egl_display,
        config,
        surface_attributes
    );
    if (application->egl_context == EGL_NO_CONTEXT ||
        application->egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(
            application->egl_display,
            application->egl_surface,
            application->egl_surface,
            application->egl_context
        )) {
        fprintf(stderr, "unable to create the EGL capture context\n");
        return false;
    }
    application->egl_create_image = (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    application->egl_destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
    application->image_target_texture =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!application->egl_create_image || !application->egl_destroy_image ||
        !application->image_target_texture) {
        fprintf(stderr, "required EGL image entry points are unavailable\n");
        return false;
    }
    gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &read_format);
    application->direct_bgra_read =
        read_format == GL_BGRA_EXT ||
        has_extension(gl_extensions, "GL_EXT_read_format_bgra");
    if (!application->direct_bgra_read) {
        application->rgba_scratch = malloc(application->frame_size);
        if (!application->rgba_scratch) {
            fprintf(stderr, "unable to allocate the EGL readback scratch\n");
            return false;
        }
    }
    fprintf(
        stderr,
        "native capture EGL %d.%d device=%s renderer=%s direct_bgra=%d\n",
        major,
        minor,
        application->egl_device,
        glGetString(GL_RENDERER),
        application->direct_bgra_read ? 1 : 0
    );
    return true;
}

static void destroy_egl(struct application *application)
{
    uint32_t index;

    if (application->egl_display != EGL_NO_DISPLAY &&
        application->egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(
            application->egl_display,
            application->egl_surface,
            application->egl_surface,
            application->egl_context
        );
        for (index = 0; index < UUDC_PIPEWIRE_BUFFERS; ++index) {
            destroy_image_cache(application, &application->images[index]);
        }
    }
    if (application->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(
            application->egl_display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        );
        if (application->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(
                application->egl_display,
                application->egl_surface
            );
        }
        if (application->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(
                application->egl_display,
                application->egl_context
            );
        }
        eglTerminate(application->egl_display);
    }
    free(application->rgba_scratch);
    application->rgba_scratch = NULL;
}

static struct image_cache *get_image_cache(
    struct application *application,
    int fd,
    uint32_t stride,
    uint32_t offset
)
{
    struct image_cache *cache = NULL;
    EGLint attributes[32];
    size_t n = 0;
    uint32_t index;

    for (index = 0; index < UUDC_PIPEWIRE_BUFFERS; ++index) {
        struct image_cache *candidate = &application->images[index];

        if (candidate->fd == fd && candidate->stride == stride &&
            candidate->offset == offset &&
            candidate->modifier == application->modifier) {
            return candidate;
        }
        if (!cache && candidate->fd < 0) {
            cache = candidate;
        }
    }
    if (!cache) {
        cache = &application->images[0];
        destroy_image_cache(application, cache);
    }

#define ADD_ATTRIBUTE(key, value) \
    do { \
        attributes[n++] = (key); \
        attributes[n++] = (EGLint)(value); \
    } while (0)
    ADD_ATTRIBUTE(EGL_WIDTH, application->width);
    ADD_ATTRIBUTE(EGL_HEIGHT, application->height);
    ADD_ATTRIBUTE(EGL_LINUX_DRM_FOURCC_EXT, application->drm_format);
    ADD_ATTRIBUTE(EGL_DMA_BUF_PLANE0_FD_EXT, fd);
    ADD_ATTRIBUTE(EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset);
    ADD_ATTRIBUTE(EGL_DMA_BUF_PLANE0_PITCH_EXT, stride);
    if (application->modifier != DRM_FORMAT_MOD_INVALID &&
        application->modifier_import) {
        ADD_ATTRIBUTE(
            EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
            application->modifier & UINT32_MAX
        );
        ADD_ATTRIBUTE(
            EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
            application->modifier >> 32
        );
    }
    attributes[n++] = EGL_NONE;
#undef ADD_ATTRIBUTE

    cache->image = application->egl_create_image(
        application->egl_display,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        NULL,
        attributes
    );
    if (cache->image == EGL_NO_IMAGE_KHR) {
        fprintf(
            stderr,
            "EGL DMA-BUF import failed fd=%d modifier=%" PRIu64
            " error=0x%x\n",
            fd,
            application->modifier,
            eglGetError()
        );
        destroy_image_cache(application, cache);
        return NULL;
    }
    glGenTextures(1, &cache->texture);
    glBindTexture(GL_TEXTURE_2D, cache->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    application->image_target_texture(
        GL_TEXTURE_2D,
        (GLeglImageOES)cache->image
    );
    glGenFramebuffers(1, &cache->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, cache->framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        cache->texture,
        0
    );
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "imported DMA-BUF framebuffer is incomplete\n");
        destroy_image_cache(application, cache);
        return NULL;
    }
    cache->fd = fd;
    cache->stride = stride;
    cache->offset = offset;
    cache->modifier = application->modifier;
    return cache;
}

static void rgba_to_bgra(
    unsigned char *destination,
    const unsigned char *source,
    size_t pixels
)
{
    size_t index;

    for (index = 0; index < pixels; ++index) {
        destination[index * 4u + 0u] = source[index * 4u + 2u];
        destination[index * 4u + 1u] = source[index * 4u + 1u];
        destination[index * 4u + 2u] = source[index * 4u + 0u];
        destination[index * 4u + 3u] = source[index * 4u + 3u];
    }
}

static bool read_dma_buf(
    struct application *application,
    const struct spa_data *data,
    unsigned char *destination
)
{
    struct image_cache *cache;
    uint32_t stride = application->stride;
    uint32_t offset = data->mapoffset;

    if (data->chunk) {
        if (data->chunk->stride > 0) {
            stride = (uint32_t)data->chunk->stride;
        }
        if (UINT32_MAX - offset < data->chunk->offset) {
            return false;
        }
        offset += data->chunk->offset;
    }
    cache = get_image_cache(application, data->fd, stride, offset);
    if (!cache) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, cache->framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(
        0,
        0,
        (GLsizei)application->width,
        (GLsizei)application->height,
        application->direct_bgra_read ? GL_BGRA_EXT : GL_RGBA,
        GL_UNSIGNED_BYTE,
        application->direct_bgra_read
            ? (void *)destination
            : (void *)application->rgba_scratch
    );
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "DMA-BUF glReadPixels failed\n");
        return false;
    }
    glFinish();
    if (!application->direct_bgra_read) {
        rgba_to_bgra(
            destination,
            application->rgba_scratch,
            (size_t)application->width * application->height
        );
    }
    return true;
}

static void drain_releases(struct application *application)
{
    unsigned char slots[64];
    ssize_t count;
    ssize_t index;

    while ((count = read(application->release_fd, slots, sizeof(slots))) > 0) {
        for (index = 0; index < count; ++index) {
            if (slots[index] < UUDC_STAGE_SLOTS) {
                application->slot_free[slots[index]] = true;
            }
        }
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        application->failed = true;
        pw_main_loop_quit(application->loop);
    }
}

static int choose_stage_slot(struct application *application)
{
    uint32_t attempt;

    for (attempt = 0; attempt < UUDC_STAGE_SLOTS; ++attempt) {
        uint32_t slot = (application->next_slot + attempt) % UUDC_STAGE_SLOTS;

        if (application->slot_free[slot]) {
            application->slot_free[slot] = false;
            application->next_slot = (slot + 1u) % UUDC_STAGE_SLOTS;
            return (int)slot;
        }
    }
    return -1;
}

static void on_process(void *userdata)
{
    struct application *application = userdata;
    struct pw_buffer *latest = NULL;
    struct pw_buffer *current;
    struct spa_buffer *buffer;
    struct spa_meta_header *header;
    struct frame_notification notification;
    int slot;
    bool ok;

    drain_releases(application);
    while ((current = pw_stream_dequeue_buffer(application->stream))) {
        if (latest) {
            pw_stream_queue_buffer(application->stream, latest);
        }
        latest = current;
    }
    if (!latest) {
        return;
    }
    buffer = latest->buffer;
    if (!buffer || buffer->n_datas != 1 ||
        buffer->datas[0].type != SPA_DATA_DmaBuf ||
        buffer->datas[0].fd < 0 || !buffer->datas[0].chunk ||
        buffer->datas[0].chunk->size == 0 ||
        (buffer->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED)) {
        application->dropped++;
        pw_stream_queue_buffer(application->stream, latest);
        return;
    }
    slot = choose_stage_slot(application);
    if (slot < 0) {
        application->dropped++;
        pw_stream_queue_buffer(application->stream, latest);
        return;
    }
    ok = read_dma_buf(
        application,
        &buffer->datas[0],
        application->stage + (size_t)slot * application->frame_size
    );
    header = spa_buffer_find_meta_data(
        buffer,
        SPA_META_Header,
        sizeof(*header)
    );
    pw_stream_queue_buffer(application->stream, latest);
    if (!ok) {
        application->slot_free[slot] = true;
        application->failed = true;
        pw_stream_set_error(
            application->stream,
            -EIO,
            "DMA-BUF EGL import/readback failed"
        );
        return;
    }
    application->sequence++;
    application->frames++;
    notification.magic = UUDC_NOTIFICATION_MAGIC;
    notification.slot = (uint32_t)slot;
    notification.sequence = application->sequence;
    notification.stride = application->width * 4u;
    notification.pts = header ? (uint64_t)header->pts : 0u;
    if (write(
            application->notify_fd,
            &notification,
            sizeof(notification)
        ) != (ssize_t)sizeof(notification)) {
        application->slot_free[slot] = true;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            application->failed = true;
            pw_main_loop_quit(application->loop);
        }
    }
}

static void on_state_changed(
    void *userdata,
    enum pw_stream_state old,
    enum pw_stream_state state,
    const char *error
)
{
    struct application *application = userdata;

    if (state == PW_STREAM_STATE_ERROR ||
        (state == PW_STREAM_STATE_UNCONNECTED &&
         old != PW_STREAM_STATE_UNCONNECTED)) {
        fprintf(
            stderr,
            "native capture node %" PRIu32 " stopped: %s\n",
            application->node,
            error ? error : pw_stream_state_as_string(state)
        );
        application->failed = true;
        pw_main_loop_quit(application->loop);
    }
}

static void on_param_changed(
    void *userdata,
    uint32_t id,
    const struct spa_pod *param
)
{
    struct application *application = userdata;
    struct spa_video_info_raw raw;
    const struct spa_pod_prop *modifier_property;
    const struct spa_pod *modifier_value;
    uint8_t buffer[512];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[2];
    uint32_t modifier_choice;
    uint32_t modifier_values;
    int64_t modifier;
    int result;

    if (!param || id != SPA_PARAM_Format) {
        return;
    }
    memset(&raw, 0, sizeof(raw));
    if (spa_format_video_raw_parse(param, &raw) < 0 ||
        raw.size.width != application->width ||
        raw.size.height != application->height ||
        (raw.format != SPA_VIDEO_FORMAT_BGRA &&
         raw.format != SPA_VIDEO_FORMAT_BGRx)) {
        pw_stream_set_error(
            application->stream,
            -EINVAL,
            "unsupported native capture format"
        );
        return;
    }
    application->drm_format = raw.format == SPA_VIDEO_FORMAT_BGRA
        ? DRM_FORMAT_ARGB8888
        : DRM_FORMAT_XRGB8888;
    application->stride = application->width * 4u;
    application->modifier = DRM_FORMAT_MOD_INVALID;
    modifier_property = spa_pod_find_prop(
        param,
        NULL,
        SPA_FORMAT_VIDEO_modifier
    );
    if (!modifier_property) {
        pw_stream_set_error(
            application->stream,
            -EINVAL,
            "PipeWire did not negotiate a DMA-BUF modifier"
        );
        return;
    }
    if (modifier_property->flags & SPA_POD_PROP_FLAG_DONT_FIXATE) {
        /*
         * This is the first half of PipeWire's modifier handshake.  Mutter
         * receives the intersected Choice, picks a modifier supported by its
         * renderer, and updates the stream with a fixed Long.  Requesting
         * buffers before that second Format event races the producer and can
         * allocate the wrong memory type.
         */
        fprintf(
            stderr,
            "native capture node %" PRIu32
            " waiting for DMA-BUF modifier fixation\n",
            application->node
        );
        return;
    }
    modifier_value = spa_pod_get_values(
        &modifier_property->value,
        &modifier_values,
        &modifier_choice
    );
    if (modifier_values != 1 || modifier_choice != SPA_CHOICE_None ||
        spa_pod_get_long(modifier_value, &modifier) < 0) {
        pw_stream_set_error(
            application->stream,
            -EINVAL,
            "PipeWire negotiated an invalid DMA-BUF modifier"
        );
        return;
    }
    application->modifier = (uint64_t)modifier;
    params[0] = spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_ParamBuffers,
        SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers,
        SPA_POD_Int(UUDC_PIPEWIRE_BUFFERS),
        SPA_PARAM_BUFFERS_blocks,
        SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_size,
        SPA_POD_Int((int32_t)application->frame_size),
        SPA_PARAM_BUFFERS_stride,
        SPA_POD_Int((int32_t)application->stride),
        SPA_PARAM_BUFFERS_dataType,
        SPA_POD_CHOICE_FLAGS_Int(1u << SPA_DATA_DmaBuf)
    );
    params[1] = spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_ParamMeta,
        SPA_PARAM_Meta,
        SPA_PARAM_META_type,
        SPA_POD_Id(SPA_META_Header),
        SPA_PARAM_META_size,
        SPA_POD_Int(sizeof(struct spa_meta_header))
    );
    result = pw_stream_update_params(application->stream, params, 2);
    if (result < 0) {
        pw_stream_set_error(
            application->stream,
            result,
            "unable to request three DMA-BUFs: %s",
            spa_strerror(result)
        );
    }
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .process = on_process,
};

static void on_core_error(
    void *userdata,
    uint32_t id,
    int sequence,
    int result,
    const char *message
)
{
    struct application *application = userdata;

    fprintf(
        stderr,
        "native capture PipeWire core failed id=%" PRIu32
        " seq=%d result=%d %s\n",
        id,
        sequence,
        result,
        message ? message : "unknown error"
    );
    application->failed = true;
    pw_main_loop_quit(application->loop);
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .error = on_core_error,
};

static const struct spa_pod *build_capture_format(
    struct spa_pod_builder *builder,
    enum spa_video_format format,
    const struct modifier_set *modifiers,
    const struct spa_rectangle *size
)
{
    struct spa_pod_frame format_frame;
    struct spa_pod_frame modifier_frame;
    uint32_t index;

    spa_pod_builder_push_object(
        builder,
        &format_frame,
        SPA_TYPE_OBJECT_Format,
        SPA_PARAM_EnumFormat
    );
    spa_pod_builder_add(
        builder,
        SPA_FORMAT_mediaType,
        SPA_POD_Id(SPA_MEDIA_TYPE_video),
        0
    );
    spa_pod_builder_add(
        builder,
        SPA_FORMAT_mediaSubtype,
        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        0
    );
    spa_pod_builder_add(
        builder,
        SPA_FORMAT_VIDEO_format,
        SPA_POD_Id(format),
        0
    );
    spa_pod_builder_prop(
        builder,
        SPA_FORMAT_VIDEO_modifier,
        SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE
    );
    spa_pod_builder_push_choice(
        builder,
        &modifier_frame,
        SPA_CHOICE_Enum,
        0
    );
    /* Choice value zero is the preferred/default value and is repeated. */
    spa_pod_builder_long(builder, modifiers->values[0]);
    for (index = 0; index < modifiers->count; ++index) {
        spa_pod_builder_long(builder, modifiers->values[index]);
    }
    spa_pod_builder_pop(builder, &modifier_frame);
    spa_pod_builder_add(
        builder,
        SPA_FORMAT_VIDEO_size,
        SPA_POD_Rectangle(size),
        0
    );
    return spa_pod_builder_pop(builder, &format_frame);
}

static int connect_stream(struct application *application)
{
    struct pw_properties *properties;
    uint8_t buffer[4096];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_rectangle size = SPA_RECTANGLE(
        application->width,
        application->height
    );
    const struct spa_pod *params[2];

    properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        PW_KEY_APP_NAME, "UU Remote native DMA-BUF capture",
        NULL
    );
    application->stream = pw_stream_new(
        application->core,
        "uu-remote-wayland-dmabuf",
        properties
    );
    if (!application->stream) {
        return errno ? -errno : -ENOMEM;
    }
    pw_stream_add_listener(
        application->stream,
        &application->stream_listener,
        &stream_events,
        application
    );
    params[0] = build_capture_format(
        &builder,
        SPA_VIDEO_FORMAT_BGRA,
        &application->modifier_sets[0],
        &size
    );
    params[1] = build_capture_format(
        &builder,
        SPA_VIDEO_FORMAT_BGRx,
        &application->modifier_sets[1],
        &size
    );
    return pw_stream_connect(
        application->stream,
        PW_DIRECTION_INPUT,
        application->node,
        PW_STREAM_FLAG_AUTOCONNECT,
        params,
        2
    );
}

static int self_test(void)
{
    struct frame_notification notification;
    unsigned char source[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    unsigned char destination[8] = { 0 };

    memset(&notification, 0, sizeof(notification));
    notification.magic = UUDC_NOTIFICATION_MAGIC;
    notification.slot = 2;
    notification.sequence = UINT32_MAX;
    notification.stride = 16;
    notification.pts = UINT64_MAX;
    if (sizeof(notification) != 24 ||
        notification.magic != UUDC_NOTIFICATION_MAGIC ||
        notification.slot >= UUDC_STAGE_SLOTS) {
        fprintf(stderr, "native capture notification self-test failed\n");
        return 1;
    }
    rgba_to_bgra(destination, source, 2);
    if (destination[0] != 3 || destination[1] != 2 ||
        destination[2] != 1 || destination[3] != 4 ||
        destination[4] != 7 || destination[5] != 6 ||
        destination[6] != 5 || destination[7] != 8) {
        fprintf(stderr, "native capture pixel conversion self-test failed\n");
        return 1;
    }
    printf("PipeWire DMA-BUF capture self-test passed\n");
    return 0;
}

int main(int argc, char **argv)
{
    struct application application;
    struct sigaction action;
    int remote_fd;
    int result;
    int loop_result;
    uint32_t index;

    memset(&application, 0, sizeof(application));
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return self_test();
    }
    if (argc == 2 && strcmp(argv[1], "--egl-smoke") == 0) {
        application.width = 64;
        application.height = 64;
        application.frame_size = 64u * 64u * 4u;
        if (!initialize_egl(&application)) {
            destroy_egl(&application);
            return 1;
        }
        printf("PipeWire DMA-BUF EGL smoke passed\n");
        destroy_egl(&application);
        return 0;
    }
    if (argc != 8 || !parse_fd(argv[1], &remote_fd) ||
        !parse_u32(argv[2], &application.node) ||
        !parse_u32(argv[3], &application.width) ||
        !parse_u32(argv[4], &application.height) ||
        !parse_fd(argv[5], &application.stage_fd) ||
        !parse_fd(argv[6], &application.notify_fd) ||
        !parse_fd(argv[7], &application.release_fd) ||
        application.width == 0 || application.height == 0) {
        fprintf(
            stderr,
            "usage: %s REMOTE_FD NODE WIDTH HEIGHT STAGE_FD NOTIFY_FD RELEASE_FD\n",
            argv[0]
        );
        return 64;
    }
    application.frame_size =
        (size_t)application.width * application.height * 4u;
    application.stage_size = application.frame_size * UUDC_STAGE_SLOTS;
    if (application.frame_size / 4u / application.width != application.height ||
        application.stage_size / UUDC_STAGE_SLOTS != application.frame_size) {
        fprintf(stderr, "native capture dimensions overflow\n");
        return 65;
    }
    application.stage = mmap(
        NULL,
        application.stage_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        application.stage_fd,
        0
    );
    if (application.stage == MAP_FAILED) {
        fprintf(
            stderr,
            "unable to map native capture staging: %s\n",
            strerror(errno)
        );
        return 66;
    }
    if (fcntl(application.release_fd, F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(application.notify_fd, F_SETFL, O_NONBLOCK) < 0) {
        fprintf(stderr, "unable to configure native capture pipes\n");
        munmap(application.stage, application.stage_size);
        return 67;
    }
    for (index = 0; index < UUDC_STAGE_SLOTS; ++index) {
        application.slot_free[index] = true;
    }
    application.modifier = DRM_FORMAT_MOD_INVALID;
    if (!initialize_egl(&application)) {
        destroy_egl(&application);
        munmap(application.stage, application.stage_size);
        return 68;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = quit_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal_application = &application;

    pw_init(&argc, &argv);
    application.loop = pw_main_loop_new(NULL);
    application.context = application.loop
        ? pw_context_new(pw_main_loop_get_loop(application.loop), NULL, 0)
        : NULL;
    application.core = application.context
        ? pw_context_connect_fd(application.context, remote_fd, NULL, 0)
        : NULL;
    if (!application.loop || !application.context || !application.core) {
        fprintf(stderr, "unable to connect the Portal PipeWire remote\n");
        application.failed = true;
    } else {
        pw_core_add_listener(
            application.core,
            &application.core_listener,
            &core_events,
            &application
        );
        result = connect_stream(&application);
        if (result < 0) {
            fprintf(
                stderr,
                "unable to connect native capture: %s\n",
                spa_strerror(result)
            );
            application.failed = true;
        }
    }
    loop_result = application.failed ? -1 : pw_main_loop_run(application.loop);
    if (loop_result < 0) {
        fprintf(
            stderr,
            "native capture main loop failed: %s\n",
            spa_strerror(loop_result)
        );
        application.failed = true;
    }
    fprintf(
        stderr,
        "native capture stopped frames=%" PRIu64 " dropped=%" PRIu64 "\n",
        application.frames,
        application.dropped
    );
    if (application.stream) {
        pw_stream_destroy(application.stream);
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
    signal_application = NULL;
    destroy_egl(&application);
    munmap(application.stage, application.stage_size);
    return application.failed ? 1 : 0;
}
