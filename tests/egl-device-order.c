#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

static const char *paths[] = {"/dev/dri/intel", "/dev/dri/nvidia0", "/dev/dri/amd",
                             "/dev/dri/nvidia1", "/dev/dri/intel", NULL};
static bool enumerate = true;
static EGLBoolean query_devices(EGLint maximum, EGLDeviceEXT *devices, EGLint *count)
{
    if (!enumerate) return EGL_FALSE;
    *count = 6;
    for (int i = 0; i < *count && i < maximum; ++i)
        devices[i] = (EGLDeviceEXT)(uintptr_t)(i + 1);
    return EGL_TRUE;
}
static const char *query_string(EGLDeviceEXT device, EGLint attribute)
{
    (void)attribute;
    return paths[(uintptr_t)device - 1];
}
static EGLDisplay platform_display(EGLenum platform, void *device, const EGLint *attributes)
{
    (void)attributes;
    return platform == EGL_PLATFORM_DEVICE_EXT ? (EGLDisplay)device : (EGLDisplay)(uintptr_t)99;
}
static __eglMustCastToProperFunctionPointerType get_proc(const char *name)
{
    if (!strcmp(name, "eglQueryDevicesEXT"))
        return (__eglMustCastToProperFunctionPointerType)query_devices;
    if (!strcmp(name, "eglQueryDeviceStringEXT"))
        return (__eglMustCastToProperFunctionPointerType)query_string;
    if (!strcmp(name, "eglGetPlatformDisplayEXT"))
        return (__eglMustCastToProperFunctionPointerType)platform_display;
    return NULL;
}
static FILE *vendor_file(const char *path, const char *mode)
{
    (void)mode;
    FILE *file = tmpfile();
    assert(file);
    fputs(strstr(path, "nvidia") ? "0x10de\n" : "0x8086\n", file);
    rewind(file);
    return file;
}
#define eglGetProcAddress get_proc
#define fopen vendor_file
#include "../src/uu-remote-egl-device.h"
#undef fopen

int main(void)
{
    struct uu_egl_device devices[UU_EGL_MAX_DEVICES];
    char name[PATH_MAX];
    unsetenv("UU_REMOTE_EGL_DEVICE");
    assert(uu_egl_devices(devices) == 4);
    const char *expected[] = {paths[1], paths[3], paths[0], paths[2]};
    for (int i = 0; i < 4; ++i) assert(!strcmp(devices[i].path, expected[i]));
    assert(uu_egl_choose_device(name, sizeof(name)) == (EGLDisplay)(uintptr_t)2);
    setenv("UU_REMOTE_EGL_DEVICE", paths[0], 1);
    assert(uu_egl_choose_device(name, sizeof(name)) == (EGLDisplay)(uintptr_t)1);
    setenv("UU_REMOTE_EGL_DEVICE", "/missing", 1);
    assert(uu_egl_choose_device(name, sizeof(name)) == EGL_NO_DISPLAY);
    unsetenv("UU_REMOTE_EGL_DEVICE");
    enumerate = false;
    assert(uu_egl_choose_device(name, sizeof(name)) == (EGLDisplay)(uintptr_t)99);
    assert(uu_egl_list_devices() == 0);
    setenv("UU_REMOTE_CAPTURE_MODE", "cpu", 1);
    assert(uu_capture_cpu());
    puts("EGL NVIDIA-first ordering, deduplication and explicit selection tests passed");
    return 0;
}
