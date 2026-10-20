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
#include <stddef.h>
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
#define UUDC_MAX_DIMENSION 16384u
#define UUDC_NOTIFICATION_MAGIC UINT32_C(0x43445555)

/*
 * buffer_width/height report the negotiated PipeWire buffer size.  With
 * scaled stage views Mutter produces physical-pixel buffers while the Portal
 * describes the stream in logical pixels; the staging slot always holds the
 * logical size the Python bridge requested.
 */
struct frame_notification {
    uint32_t magic;
    uint32_t slot;
    uint32_t sequence;
    uint32_t stride;
    uint64_t pts;
    uint32_t buffer_width;
    uint32_t buffer_height;
};

_Static_assert(
    sizeof(struct frame_notification) == 32,
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
    uint32_t source_width;
    uint32_t source_height;
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
    GLuint scale_program;
    GLuint scale_texture;
    GLuint scale_framebuffer;
    GLint scale_position;
    GLint scale_texcoord;
    GLint scale_source;
    GLint scale_step;
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
    const char *preferred = getenv("UU_REMOTE_EGL_DEVICE");

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
            if (preferred && *preferred) {
                if (path && strcmp(path, preferred) == 0) {
                    selected = devices[index];
                    selected_path = path;
                    break;
                }
                continue;
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
    if (preferred && *preferred) {
        fprintf(stderr, "requested EGL device unavailable: %s\n", preferred);
        return EGL_NO_DISPLAY;
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

static void destroy_image_caches(struct application *application)
{
    uint32_t index;

    for (index = 0; index < UUDC_PIPEWIRE_BUFFERS; ++index) {
        destroy_image_cache(application, &application->images[index]);
    }
}

static void destroy_scale_pipeline(struct application *application)
{
    if (application->scale_framebuffer) {
        glDeleteFramebuffers(1, &application->scale_framebuffer);
    }
    if (application->scale_texture) {
        glDeleteTextures(1, &application->scale_texture);
    }
    if (application->scale_program) {
        glDeleteProgram(application->scale_program);
    }
    application->scale_framebuffer = 0;
    application->scale_texture = 0;
    application->scale_program = 0;
}

static void destroy_egl(struct application *application)
{
    if (application->egl_display != EGL_NO_DISPLAY &&
        application->egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(
            application->egl_display,
            application->egl_surface,
            application->egl_surface,
            application->egl_context
        );
        destroy_image_caches(application);
        destroy_scale_pipeline(application);
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

/*
 * Mutter sizes a scaled stream as roundf(logical * scale) per axis, so the
 * cross products of a genuine uniform scale differ by at most half of each
 * logical extent.  Anything else means the Portal layout is stale.
 */
static bool uniform_scale(
    uint32_t logical_width,
    uint32_t logical_height,
    uint32_t buffer_width,
    uint32_t buffer_height
)
{
    uint64_t left = (uint64_t)buffer_width * logical_height;
    uint64_t right = (uint64_t)buffer_height * logical_width;
    uint64_t difference = left > right ? left - right : right - left;

    return difference <= ((uint64_t)logical_width + logical_height) / 2u + 1u;
}

static bool scaling_required(const struct application *application)
{
    return application->source_width != application->width ||
        application->source_height != application->height;
}

static void set_sampling(GLint filter)
{
    /* GLES2 samples NPOT textures as black unless they clamp to edge. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint compiled = GL_FALSE;
    char message[512];

    if (!shader) {
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        message[0] = '\0';
        glGetShaderInfoLog(shader, sizeof(message), NULL, message);
        fprintf(stderr, "native capture shader failed: %s\n", message);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/*
 * Mutter's scaled stage views produce physical-pixel buffers for a stream the
 * Portal and XWayland describe in logical pixels.  Downscale on the GPU into
 * the logical staging size.  Four bilinear taps at quarter-pixel offsets
 * form an exact box filter for 2x and 4x and a close one for fractional
 * scales.
 */
static bool ensure_scale_pipeline(struct application *application)
{
    static const char vertex_source[] =
        "attribute vec2 a_position;\n"
        "attribute vec2 a_texcoord;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    v_texcoord = a_texcoord;\n"
        "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
        "}\n";
    static const char fragment_source[] =
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
        "precision highp float;\n"
        "#else\n"
        "precision mediump float;\n"
        "#endif\n"
        "uniform sampler2D u_source;\n"
        "uniform vec2 u_step;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    gl_FragColor = 0.25 * (\n"
        "        texture2D(u_source, v_texcoord + vec2(-u_step.x, -u_step.y)) +\n"
        "        texture2D(u_source, v_texcoord + vec2(u_step.x, -u_step.y)) +\n"
        "        texture2D(u_source, v_texcoord + vec2(-u_step.x, u_step.y)) +\n"
        "        texture2D(u_source, v_texcoord + vec2(u_step.x, u_step.y)));\n"
        "}\n";
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLint linked = GL_FALSE;

    if (application->scale_program) {
        return true;
    }
    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex_shader && fragment_shader) {
        application->scale_program = glCreateProgram();
    }
    if (application->scale_program) {
        glAttachShader(application->scale_program, vertex_shader);
        glAttachShader(application->scale_program, fragment_shader);
        glLinkProgram(application->scale_program);
        glGetProgramiv(application->scale_program, GL_LINK_STATUS, &linked);
    }
    if (vertex_shader) {
        glDeleteShader(vertex_shader);
    }
    if (fragment_shader) {
        glDeleteShader(fragment_shader);
    }
    if (linked != GL_TRUE) {
        fprintf(stderr, "native capture scale program failed to link\n");
        destroy_scale_pipeline(application);
        return false;
    }
    application->scale_position = glGetAttribLocation(
        application->scale_program,
        "a_position"
    );
    application->scale_texcoord = glGetAttribLocation(
        application->scale_program,
        "a_texcoord"
    );
    application->scale_source = glGetUniformLocation(
        application->scale_program,
        "u_source"
    );
    application->scale_step = glGetUniformLocation(
        application->scale_program,
        "u_step"
    );

    glGenTextures(1, &application->scale_texture);
    glBindTexture(GL_TEXTURE_2D, application->scale_texture);
    set_sampling(GL_NEAREST);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        (GLsizei)application->width,
        (GLsizei)application->height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL
    );
    glGenFramebuffers(1, &application->scale_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, application->scale_framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        application->scale_texture,
        0
    );
    if (
        application->scale_position < 0 ||
        application->scale_texcoord < 0 ||
        application->scale_source < 0 ||
        application->scale_step < 0 ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        glGetError() != GL_NO_ERROR
    ) {
        fprintf(stderr, "native capture scale target is incomplete\n");
        destroy_scale_pipeline(application);
        return false;
    }
    return true;
}

static bool render_scaled(struct application *application, GLuint texture)
{
    /* Texture row 0 is the first buffer row; keep it at framebuffer row 0. */
    static const GLfloat positions[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
    };
    static const GLfloat texcoords[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    };

    if (!ensure_scale_pipeline(application)) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, application->scale_framebuffer);
    glViewport(
        0,
        0,
        (GLsizei)application->width,
        (GLsizei)application->height
    );
    glUseProgram(application->scale_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(application->scale_source, 0);
    glUniform2f(
        application->scale_step,
        0.25f / (GLfloat)application->width,
        0.25f / (GLfloat)application->height
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(
        (GLuint)application->scale_position,
        2,
        GL_FLOAT,
        GL_FALSE,
        0,
        positions
    );
    glVertexAttribPointer(
        (GLuint)application->scale_texcoord,
        2,
        GL_FLOAT,
        GL_FALSE,
        0,
        texcoords
    );
    glEnableVertexAttribArray((GLuint)application->scale_position);
    glEnableVertexAttribArray((GLuint)application->scale_texcoord);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray((GLuint)application->scale_position);
    glDisableVertexAttribArray((GLuint)application->scale_texcoord);
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "native capture GPU downscale failed\n");
        return false;
    }
    return true;
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
    ADD_ATTRIBUTE(EGL_WIDTH, application->source_width);
    ADD_ATTRIBUTE(EGL_HEIGHT, application->source_height);
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
    set_sampling(scaling_required(application) ? GL_LINEAR : GL_NEAREST);
    application->image_target_texture(
        GL_TEXTURE_2D,
        (GLeglImageOES)cache->image
    );
    if (!scaling_required(application)) {
        glGenFramebuffers(1, &cache->framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, cache->framebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            cache->texture,
            0
        );
        if (
            glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE
        ) {
            fprintf(stderr, "imported DMA-BUF framebuffer is incomplete\n");
            destroy_image_cache(application, cache);
            return NULL;
        }
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
    if (scaling_required(application)) {
        if (!render_scaled(application, cache->texture)) {
            return false;
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, cache->framebuffer);
    }
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
    notification.buffer_width = application->source_width;
    notification.buffer_height = application->source_height;
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
        raw.size.width == 0 || raw.size.height == 0 ||
        raw.size.width > UUDC_MAX_DIMENSION ||
        raw.size.height > UUDC_MAX_DIMENSION ||
        (raw.format != SPA_VIDEO_FORMAT_BGRA &&
         raw.format != SPA_VIDEO_FORMAT_BGRx)) {
        pw_stream_set_error(
            application->stream,
            -EINVAL,
            "unsupported native capture format"
        );
        return;
    }
    if (application->source_width == 0) {
        if (!uniform_scale(
                application->width,
                application->height,
                raw.size.width,
                raw.size.height
            )) {
            pw_stream_set_error(
                application->stream,
                -EINVAL,
                "native capture buffer does not scale the Portal stream"
            );
            return;
        }
        application->source_width = raw.size.width;
        application->source_height = raw.size.height;
        fprintf(
            stderr,
            "native capture node %" PRIu32 " buffer %" PRIu32 "x%" PRIu32
            " staged as %" PRIu32 "x%" PRIu32 "\n",
            application->node,
            application->source_width,
            application->source_height,
            application->width,
            application->height
        );
    } else if (raw.size.width != application->source_width ||
        raw.size.height != application->source_height) {
        /*
         * A monitor mode or scale change leaves the bridge's logical layout
         * stale.  Exit so its supervisor rebuilds the Portal session at the
         * new geometry instead of stretching frames into the old slot.
         */
        fprintf(
            stderr,
            "native capture node %" PRIu32 " buffer changed %" PRIu32
            "x%" PRIu32 " -> %" PRIu32 "x%" PRIu32
            "; restarting to rebuild the stream layout\n",
            application->node,
            application->source_width,
            application->source_height,
            raw.size.width,
            raw.size.height
        );
        application->failed = true;
        pw_main_loop_quit(application->loop);
        return;
    }
    application->drm_format = raw.format == SPA_VIDEO_FORMAT_BGRA
        ? DRM_FORMAT_ARGB8888
        : DRM_FORMAT_XRGB8888;
    application->stride = application->source_width * 4u;
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
        SPA_POD_Int((int32_t)(
            (size_t)application->stride * application->source_height
        )),
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

static void on_remove_buffer(void *userdata, struct pw_buffer *buffer)
{
    struct application *application = userdata;
    uint32_t index;
    int fd;

    if (!buffer || !buffer->buffer || buffer->buffer->n_datas < 1) {
        return;
    }
    /*
     * PipeWire closes the descriptor after this callback; the kernel can hand
     * its number to the next buffer.  Drop the import now so a reused number
     * cannot resolve to the old buffer's EGL image.
     */
    fd = (int)buffer->buffer->datas[0].fd;
    for (index = 0; index < UUDC_PIPEWIRE_BUFFERS; ++index) {
        if (fd >= 0 && application->images[index].fd == fd) {
            destroy_image_cache(application, &application->images[index]);
        }
    }
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .remove_buffer = on_remove_buffer,
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
    struct spa_rectangle minimum_size = SPA_RECTANGLE(1, 1);
    struct spa_rectangle maximum_size = SPA_RECTANGLE(
        UUDC_MAX_DIMENSION,
        UUDC_MAX_DIMENSION
    );
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
    /*
     * Prefer the Portal's logical size but accept the producer's own: with
     * scaled stage views Mutter offers physical-pixel buffers only.
     */
    spa_pod_builder_add(
        builder,
        SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(size, &minimum_size, &maximum_size),
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
    notification.buffer_width = UUDC_MAX_DIMENSION;
    notification.buffer_height = UUDC_MAX_DIMENSION;
    if (sizeof(notification) != 32 ||
        offsetof(struct frame_notification, buffer_width) != 24 ||
        notification.magic != UUDC_NOTIFICATION_MAGIC ||
        notification.slot >= UUDC_STAGE_SLOTS) {
        fprintf(stderr, "native capture notification self-test failed\n");
        return 1;
    }
    if (!uniform_scale(1920u, 1080u, 1920u, 1080u) ||
        !uniform_scale(1920u, 1080u, 3840u, 2160u) ||
        !uniform_scale(1920u, 1080u, 2400u, 1350u) ||
        !uniform_scale(1365u, 767u, 1706u, 959u) ||
        uniform_scale(1920u, 1080u, 2560u, 1600u) ||
        uniform_scale(1080u, 1920u, 1920u, 1080u)) {
        fprintf(stderr, "native capture scale validation self-test failed\n");
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

/*
 * Run the GPU downscale on a synthetic source whose channels encode the
 * column and row.  Each output pixel must be the box average of its
 * factor x factor footprint, and output row 0 must come from source row 0
 * exactly like the unscaled readback path.
 */
static bool egl_scale_case(uint32_t factor)
{
    struct application application;
    unsigned char source[8u * 8u * 4u];
    unsigned char output[2u * 2u * 4u];
    uint32_t size = 2u * factor;
    uint32_t row;
    uint32_t column;
    GLuint texture = 0;
    bool ok;

    memset(&application, 0, sizeof(application));
    application.width = 2u;
    application.height = 2u;
    application.source_width = size;
    application.source_height = size;
    application.frame_size = sizeof(output);
    if (!initialize_egl(&application)) {
        destroy_egl(&application);
        return false;
    }
    for (row = 0; row < size; ++row) {
        for (column = 0; column < size; ++column) {
            unsigned char *pixel = source + (row * size + column) * 4u;

            pixel[0] = (unsigned char)(column * 30u);
            pixel[1] = (unsigned char)(row * 30u);
            pixel[2] = 200u;
            pixel[3] = 255u;
        }
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    set_sampling(GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        (GLsizei)size,
        (GLsizei)size,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        source
    );
    ok = scaling_required(&application) &&
        render_scaled(&application, texture);
    if (ok) {
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, output);
        ok = glGetError() == GL_NO_ERROR;
    }
    for (row = 0; ok && row < 2u; ++row) {
        for (column = 0; ok && column < 2u; ++column) {
            const unsigned char *pixel = output + (row * 2u + column) * 4u;
            /* Mean of factor consecutive indices starting at index*factor. */
            int expected_red = (int)((2u * column * factor + factor - 1u) * 15u);
            int expected_green = (int)((2u * row * factor + factor - 1u) * 15u);

            ok = abs((int)pixel[0] - expected_red) <= 1 &&
                abs((int)pixel[1] - expected_green) <= 1 &&
                abs((int)pixel[2] - 200) <= 1;
            if (!ok) {
                fprintf(
                    stderr,
                    "native capture %ux downscale pixel %u,%u = %d,%d,%d"
                    " expected %d,%d,200\n",
                    factor,
                    column,
                    row,
                    pixel[0],
                    pixel[1],
                    pixel[2],
                    expected_red,
                    expected_green
                );
            }
        }
    }
    if (texture) {
        glDeleteTextures(1, &texture);
    }
    destroy_egl(&application);
    return ok;
}

static int egl_scale_self_test(void)
{
    if (!egl_scale_case(2u) || !egl_scale_case(4u)) {
        fprintf(stderr, "native capture GPU downscale self-test failed\n");
        return 1;
    }
    printf("PipeWire DMA-BUF GPU downscale self-test passed\n");
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
    if (argc == 2 && strcmp(argv[1], "--egl-scale-self-test") == 0) {
        return egl_scale_self_test();
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
