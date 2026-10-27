/* SPDX-License-Identifier: 0BSD
 * Shared capture/cursor device order. Listing does not initialize a GPU.
 */
#ifndef UU_REMOTE_EGL_DEVICE_H
#define UU_REMOTE_EGL_DEVICE_H

#define UU_EGL_MAX_DEVICES 16
#define UU_CAPTURE_DEVICE_FAILURE 68

static bool uu_capture_cpu(void)
{
    const char *mode = getenv("UU_REMOTE_CAPTURE_MODE");
    return mode && strcmp(mode, "cpu") == 0;
}

struct uu_egl_device {
    EGLDeviceEXT device;
    const char *path;
};

static bool uu_egl_is_nvidia(const char *path)
{
    char vendor_path[PATH_MAX];
    char vendor[32] = {0};
    const char *name = strrchr(path, '/');
    FILE *file;

    name = name ? name + 1 : path;
    if (snprintf(vendor_path, sizeof(vendor_path),
                 "/sys/class/drm/%s/device/vendor", name) >= (int)sizeof(vendor_path))
        return false;
    file = fopen(vendor_path, "r");
    if (!file)
        return false;
    bool read = fgets(vendor, sizeof(vendor), file) != NULL;
    fclose(file);
    return read && strncmp(vendor, "0x10de", 6) == 0;
}

static int uu_egl_devices(struct uu_egl_device *result)
{
    PFNEGLQUERYDEVICESEXTPROC query = (PFNEGLQUERYDEVICESEXTPROC)
        eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC string = (PFNEGLQUERYDEVICESTRINGEXTPROC)
        eglGetProcAddress("eglQueryDeviceStringEXT");
    EGLDeviceEXT devices[UU_EGL_MAX_DEVICES];
    EGLint count = 0;
    int used = 0;

    if (!query || !string || !query(UU_EGL_MAX_DEVICES, devices, &count))
        return 0;
    if (count > UU_EGL_MAX_DEVICES)
        count = UU_EGL_MAX_DEVICES;
    for (int i = 0; i < count; ++i) {
        const char *path = string(devices[i], EGL_DRM_RENDER_NODE_FILE_EXT);
        if (!path)
            path = string(devices[i], EGL_DRM_DEVICE_FILE_EXT);
        if (!path || path[0] != '/' || strlen(path) >= PATH_MAX ||
            strchr(path, '\n') || strchr(path, '\r'))
            continue;
        bool duplicate = false;
        for (int j = 0; j < used; ++j)
            if (strcmp(result[j].path, path) == 0)
                duplicate = true;
        if (!duplicate)
            result[used++] = (struct uu_egl_device){devices[i], path};
    }
    /* Stable partition: NVIDIA first, retain EGL enumeration order within
     * each group. The supervisor validates actual frames, not this ordering.
     */
    for (int i = 0, next = 0; i < used; ++i) {
        if (uu_egl_is_nvidia(result[i].path)) {
            struct uu_egl_device device = result[i];
            memmove(&result[next + 1], &result[next],
                    (size_t)(i - next) * sizeof(*result));
            result[next++] = device;
        }
    }
    return used;
}

static int uu_egl_list_devices(void)
{
    struct uu_egl_device devices[UU_EGL_MAX_DEVICES];
    int count = uu_egl_devices(devices);
    for (int i = 0; i < count; ++i)
        puts(devices[i].path);
    return 0;
}

static EGLDisplay uu_egl_choose_device(char *name, size_t size)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    struct uu_egl_device devices[UU_EGL_MAX_DEVICES];
    const char *preferred = getenv("UU_REMOTE_EGL_DEVICE");
    int count = uu_egl_devices(devices);

    if (platform) {
        for (int i = 0; i < count; ++i) {
            if (preferred && *preferred && strcmp(preferred, devices[i].path) != 0)
                continue;
            snprintf(name, size, "%s", devices[i].path);
            return platform(EGL_PLATFORM_DEVICE_EXT, devices[i].device, NULL);
        }
    }
    if (preferred && *preferred) {
        fprintf(stderr, "requested EGL device unavailable: %s\n", preferred);
        return EGL_NO_DISPLAY;
    }
    if (platform) {
        snprintf(name, size, "%s", "surfaceless-default");
        return platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    }
    snprintf(name, size, "%s", "egl-default");
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

#endif
