/*
 * UU Remote D3D11-to-CUDA NVENC bridge.
 *
 * Linux libnvidia-encode cannot consume a Wine/DXVK ID3D11Device or texture.
 * Translate the session to CUDA and copy each registered D3D11 input texture
 * through a staging resource into pitched CUDA memory before NVENC maps it.
 */

#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <d3d11.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define UU_NVENC_ENCODER_MAGIC  0x55554e56454e4331ULL
#define UU_NVENC_RESOURCE_MAGIC 0x55554e5652455331ULL
#define UU_CUDA_SUCCESS 0

#define UU_NV_ENC_ERR_NO_ENCODE_DEVICE 1
#define UU_NV_ENC_ERR_INVALID_DEVICE 4
#define UU_NV_ENC_ERR_INVALID_PARAM 8
#define UU_NV_ENC_ERR_OUT_OF_MEMORY 10
#define UU_NV_ENC_ERR_MAP_FAILED 16
#define UU_NV_ENC_ERR_GENERIC 20
#define UU_NV_ENC_ERR_RESOURCE_NOT_MAPPED 23

enum uu_nv_enc_input_resource_type
{
    UU_NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX = 0,
    UU_NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR = 1,
    UU_NV_ENC_INPUT_RESOURCE_TYPE_CUDAARRAY = 2,
    UU_NV_ENC_INPUT_RESOURCE_TYPE_OPENGL_TEX = 3
};

/* The relay header intentionally leaves same-ABI structures opaque. */
struct _NV_ENC_MAP_INPUT_RESOURCE
{
    uint32_t version;
    uint32_t subResourceIndex;
    void *inputResource;
    NV_ENC_REGISTERED_PTR registeredResource;
    NV_ENC_INPUT_PTR mappedResource;
    NV_ENC_BUFFER_FORMAT mappedBufferFmt;
    uint32_t reserved1[251];
    void *reserved2[63];
};

struct _NV_ENC_REGISTER_RESOURCE
{
    uint32_t version;
    int resourceType;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t subResourceIndex;
    void *resourceToRegister;
    NV_ENC_REGISTERED_PTR registeredResource;
    NV_ENC_BUFFER_FORMAT bufferFormat;
    int bufferUsage;
    void *pInputFencePoint;
    uint32_t chromaOffset[2];
    uint32_t reserved1[245];
    void *reserved2[61];
};

typedef int uu_CUresult;
typedef int uu_CUdevice;
typedef struct CUctx_st *uu_CUcontext;
typedef uint64_t uu_CUdeviceptr;

struct uu_CUDA_MEMCPY2D
{
    size_t srcXInBytes;
    size_t srcY;
    unsigned int srcMemoryType;
    const void *srcHost;
    uu_CUdeviceptr srcDevice;
    void *srcArray;
    size_t srcPitch;
    size_t dstXInBytes;
    size_t dstY;
    unsigned int dstMemoryType;
    void *dstHost;
    uu_CUdeviceptr dstDevice;
    void *dstArray;
    size_t dstPitch;
    size_t WidthInBytes;
    size_t Height;
};

#define UU_CU_MEMORYTYPE_HOST 1u
#define UU_CU_MEMORYTYPE_DEVICE 2u

struct uu_nvenc_encoder;

struct uu_nvenc_resource
{
    uint64_t magic;
    struct uu_nvenc_encoder *owner;
    NV_ENC_REGISTERED_PTR native_resource;
    NV_ENC_INPUT_PTR mapped_resource;
    ID3D11Texture2D *texture;
    ID3D11Texture2D *staging;
    uu_CUdeviceptr cuda_memory;
    size_t cuda_pitch;
    uint32_t source_subresource;
    uint32_t row_bytes;
    uint32_t rows;
    NV_ENC_BUFFER_FORMAT format;
    BOOL upload_fresh;
    struct uu_nvenc_resource *next;
};

struct uu_nvenc_encoder
{
    uint64_t magic;
    void *native_encoder;
    uu_CUdevice cuda_device;
    uu_CUcontext cuda_context;
    pthread_mutex_t mutex;
    BOOL destroying;
    struct uu_nvenc_resource *resources;
    struct uu_nvenc_encoder *next;
};

static void *uu_cuda_handle;
static pthread_mutex_t uu_cuda_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t uu_bridge_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct uu_nvenc_encoder *uu_encoders;
static uint64_t uu_map_calls;
static uint64_t uu_map_matches;
static uint64_t uu_encode_calls;
static uint64_t uu_encode_matches;
static uint64_t uu_encode_uploads;
static uint64_t uu_unmap_calls;
static uint64_t uu_upload_calls;
static uint64_t uu_upload_2d_calls;
static uint64_t uu_upload_hash_changes;
static uint64_t uu_last_upload_hash;

static uu_CUresult (*uu_cuInit)(unsigned int);
static uu_CUresult (*uu_cuDeviceGetCount)(int *);
static uu_CUresult (*uu_cuDeviceGet)(uu_CUdevice *, int);
static uu_CUresult (*uu_cuDevicePrimaryCtxRetain)(uu_CUcontext *, uu_CUdevice);
static uu_CUresult (*uu_cuDevicePrimaryCtxRelease)(uu_CUdevice);
static uu_CUresult (*uu_cuCtxPushCurrent)(uu_CUcontext);
static uu_CUresult (*uu_cuCtxPopCurrent)(uu_CUcontext *);
static uu_CUresult (*uu_cuMemAllocPitch)(uu_CUdeviceptr *, size_t *, size_t,
                                         size_t, unsigned int);
static uu_CUresult (*uu_cuMemFree)(uu_CUdeviceptr);
static uu_CUresult (*uu_cuMemcpy2D)(const struct uu_CUDA_MEMCPY2D *);

static void uu_free_resource(struct uu_nvenc_resource *resource);
static NVENCSTATUS uu_upload_d3d11_resource_locked(
    struct uu_nvenc_encoder *encoder,
    struct uu_nvenc_resource *resource);

static void uu_write_bridge_status(void)
{
    char path[160];
    char status[1024];
    int descriptor;
    int length;

    snprintf(path, sizeof(path),
             "/tmp/uu-remote-nvenc-bridge-%lu.status",
             (unsigned long)getpid());
    length = snprintf(
        status, sizeof(status),
        "version=3\n"
        "pid=%lu\n"
        "map_calls=%llu\n"
        "map_matches=%llu\n"
        "encode_calls=%llu\n"
        "encode_matches=%llu\n"
        "encode_uploads=%llu\n"
        "unmap_calls=%llu\n"
        "upload_calls=%llu\n"
        "upload_2d_calls=%llu\n"
        "upload_hash_changes=%llu\n"
        "last_upload_hash=%016llx\n",
        (unsigned long)getpid(),
        (unsigned long long)__atomic_load_n(&uu_map_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&uu_map_matches, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&uu_encode_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&uu_encode_matches, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&uu_encode_uploads, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&uu_unmap_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&uu_upload_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(
            &uu_upload_2d_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(
            &uu_upload_hash_changes, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(
            &uu_last_upload_hash, __ATOMIC_RELAXED));
    if (length <= 0 || (size_t)length >= sizeof(status))
        return;
    descriptor = open(path,
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                      0600);
    if (descriptor < 0)
        return;
    if (write(descriptor, status, (size_t)length) < 0)
        WARN("failed to write NVENC bridge status: %s\n", strerror(errno));
    close(descriptor);
}

static uint64_t uu_sample_mapped_texture(
    const D3D11_MAPPED_SUBRESOURCE *mapped,
    const struct uu_nvenc_resource *resource)
{
    uint64_t hash = 1469598103934665603ULL;
    uint32_t row_step = resource->rows > 32u ? resource->rows / 32u : 1u;
    uint32_t byte_step = resource->row_bytes > 128u ?
        resource->row_bytes / 128u : 1u;
    uint32_t row;

    for (row = 0; row < resource->rows; row += row_step)
    {
        const BYTE *source = (const BYTE *)mapped->pData +
                             (size_t)row * mapped->RowPitch;
        uint32_t offset;

        for (offset = 0; offset < resource->row_bytes; offset += byte_step)
        {
            hash ^= source[offset];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

#include "uu-remote-nvenc-lifecycle.h"
#include "uu-remote-nvenc-texture.h"

static BOOL uu_load_cuda_symbol(void *handle, void **target, const char *name)
{
    *target = dlsym(handle, name);
    if (!*target)
    {
        ERR("missing CUDA symbol %s: %s\n", name, dlerror());
        return FALSE;
    }
    return TRUE;
}

static BOOL uu_remote_load_cuda(void)
{
    void *handle;
    BOOL loaded = FALSE;

    pthread_mutex_lock(&uu_cuda_mutex);
    if (uu_cuda_handle)
    {
        loaded = TRUE;
        goto done;
    }

    handle = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        ERR("cannot load libcuda.so.1: %s\n", dlerror());
        goto done;
    }

    loaded = uu_load_cuda_symbol(handle, (void **)&uu_cuInit, "cuInit") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuDeviceGetCount,
                                 "cuDeviceGetCount") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuDeviceGet,
                                 "cuDeviceGet") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuDevicePrimaryCtxRetain,
                                 "cuDevicePrimaryCtxRetain") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuDevicePrimaryCtxRelease,
                                 "cuDevicePrimaryCtxRelease_v2") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuCtxPushCurrent,
                                 "cuCtxPushCurrent_v2") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuCtxPopCurrent,
                                 "cuCtxPopCurrent_v2") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuMemAllocPitch,
                                 "cuMemAllocPitch_v2") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuMemFree,
                                 "cuMemFree_v2") &&
             uu_load_cuda_symbol(handle, (void **)&uu_cuMemcpy2D,
                                 "cuMemcpy2D_v2");
    if (loaded)
    {
        /* Publish the handle only after every function pointer is valid. */
        uu_cuda_handle = handle;
    }
    else
    {
        uu_cuInit = NULL;
        uu_cuDeviceGetCount = NULL;
        uu_cuDeviceGet = NULL;
        uu_cuDevicePrimaryCtxRetain = NULL;
        uu_cuDevicePrimaryCtxRelease = NULL;
        uu_cuCtxPushCurrent = NULL;
        uu_cuCtxPopCurrent = NULL;
        uu_cuMemAllocPitch = NULL;
        uu_cuMemFree = NULL;
        uu_cuMemcpy2D = NULL;
        dlclose(handle);
    }

done:
    pthread_mutex_unlock(&uu_cuda_mutex);
    return loaded;
}

static BOOL uu_selected_cuda_device(uu_CUdevice *device)
{
    const char *configured = getenv("UU_REMOTE_CUDA_DEVICE");
    char *end = NULL;
    unsigned long ordinal = 0;
    int count = 0;

    if (configured && *configured)
    {
        errno = 0;
        ordinal = strtoul(configured, &end, 10);
        if (errno || end == configured || *end || ordinal > INT_MAX)
        {
            ERR("invalid UU_REMOTE_CUDA_DEVICE value: %s\n", configured);
            return FALSE;
        }
    }
    if (uu_cuInit(0) != UU_CUDA_SUCCESS ||
        uu_cuDeviceGetCount(&count) != UU_CUDA_SUCCESS ||
        ordinal >= (unsigned int)count ||
        uu_cuDeviceGet(device, ordinal) != UU_CUDA_SUCCESS)
    {
        ERR("CUDA device ordinal %lu is unavailable (count=%d)\n", ordinal, count);
        return FALSE;
    }
    return TRUE;
}

static struct uu_nvenc_resource *uu_find_resource(
    struct uu_nvenc_encoder *encoder, NV_ENC_REGISTERED_PTR handle)
{
    struct uu_nvenc_resource *resource;

    if (!encoder)
        return NULL;
    for (resource = encoder->resources; resource; resource = resource->next)
        if (resource->magic == UU_NVENC_RESOURCE_MAGIC &&
            (NV_ENC_REGISTERED_PTR)resource == handle)
            return resource;
    return NULL;
}

static struct uu_nvenc_resource *uu_find_mapped_resource(
    struct uu_nvenc_encoder *encoder, NV_ENC_INPUT_PTR handle)
{
    struct uu_nvenc_resource *resource;

    if (!encoder || !handle)
        return NULL;
    for (resource = encoder->resources; resource; resource = resource->next)
        if (resource->magic == UU_NVENC_RESOURCE_MAGIC &&
            resource->mapped_resource == handle)
            return resource;
    return NULL;
}

static BOOL uu_push_encoder(struct uu_nvenc_encoder *encoder)
{
    return encoder && uu_cuCtxPushCurrent(encoder->cuda_context) == UU_CUDA_SUCCESS;
}

static void uu_pop_encoder(void)
{
    uu_CUcontext previous = NULL;
    uu_cuCtxPopCurrent(&previous);
}

static struct uu_nvenc_encoder *uu_enter_encoder(
    void *encoder_handle, BOOL *bridged)
{
    struct uu_nvenc_encoder *encoder = uu_lock_encoder(encoder_handle, bridged);

    if (!encoder)
        return NULL;
    if (!uu_push_encoder(encoder))
    {
        uu_unlock_encoder(encoder);
        return NULL;
    }
    return encoder;
}

static void uu_leave_encoder(struct uu_nvenc_encoder *encoder)
{
    uu_pop_encoder();
    uu_unlock_encoder(encoder);
}

static NVENCSTATUS uu_initialize_encoder(
    void *encoder_handle, NV_ENC_INITIALIZE_PARAMS *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle, &bridged);
    NVENCSTATUS status;

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncInitializeEncoder(encoder_handle,
                                                              params);
    status = origFunctions.nvEncInitializeEncoder(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_create_bitstream_buffer(
    void *encoder_handle, NV_ENC_CREATE_BITSTREAM_BUFFER *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle, &bridged);
    NVENCSTATUS status;

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncCreateBitstreamBuffer(encoder_handle,
                                                                  params);
    status = origFunctions.nvEncCreateBitstreamBuffer(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_destroy_bitstream_buffer(
    void *encoder_handle, NV_ENC_OUTPUT_PTR buffer)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle, &bridged);
    NVENCSTATUS status;

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncDestroyBitstreamBuffer(encoder_handle,
                                                                   buffer);
    status = origFunctions.nvEncDestroyBitstreamBuffer(encoder_handle, buffer);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_encode_picture(
    void *encoder_handle, NV_ENC_PIC_PARAMS *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_lock_encoder(encoder_handle, &bridged);
    struct uu_nvenc_resource *resource;
    NVENCSTATUS status;
    uint64_t calls = __atomic_add_fetch(
        &uu_encode_calls, 1u, __ATOMIC_RELAXED);

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncEncodePicture(encoder_handle, params);

    resource = params ?
        uu_find_mapped_resource(encoder, params->inputBuffer) : NULL;
    if (resource)
        __atomic_add_fetch(&uu_encode_matches, 1u, __ATOMIC_RELAXED);
    if (resource && uu_nvenc_encode_upload_required(&resource->upload_fresh))
    {
        __atomic_add_fetch(&uu_encode_uploads, 1u, __ATOMIC_RELAXED);
        status = uu_upload_d3d11_resource_locked(encoder, resource);
        if (status != NV_ENC_SUCCESS)
        {
            uu_unlock_encoder(encoder);
            return status;
        }
    }
    if (!uu_push_encoder(encoder))
    {
        uu_unlock_encoder(encoder);
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    }
    status = origFunctions.nvEncEncodePicture(encoder_handle, params);
    uu_pop_encoder();
    uu_unlock_encoder(encoder);
    if (calls == 1u || calls % 120u == 0u)
        uu_write_bridge_status();
    return status;
}

static NVENCSTATUS uu_lock_bitstream(
    void *encoder_handle, NV_ENC_LOCK_BITSTREAM *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle, &bridged);
    NVENCSTATUS status;

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncLockBitstream(encoder_handle, params);
    status = origFunctions.nvEncLockBitstream(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_unlock_bitstream(
    void *encoder_handle, NV_ENC_OUTPUT_PTR buffer)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle, &bridged);
    NVENCSTATUS status;

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncUnlockBitstream(encoder_handle,
                                                            buffer);
    status = origFunctions.nvEncUnlockBitstream(encoder_handle, buffer);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_unmap_input_resource(
    void *encoder_handle, NV_ENC_INPUT_PTR resource)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_lock_encoder(encoder_handle, &bridged);
    struct uu_nvenc_resource *bridge_resource;
    NVENCSTATUS status;
    uint64_t calls;

    calls = __atomic_add_fetch(&uu_unmap_calls, 1u, __ATOMIC_RELAXED);

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncUnmapInputResource(encoder_handle,
                                                               resource);
    bridge_resource = uu_find_mapped_resource(encoder, resource);
    if (!uu_push_encoder(encoder))
    {
        uu_unlock_encoder(encoder);
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    }
    status = origFunctions.nvEncUnmapInputResource(encoder_handle, resource);
    uu_pop_encoder();
    if (status == NV_ENC_SUCCESS && bridge_resource)
    {
        bridge_resource->mapped_resource = NULL;
        bridge_resource->upload_fresh = FALSE;
    }
    uu_unlock_encoder(encoder);
    if (calls == 1u || calls % 120u == 0u)
        uu_write_bridge_status();
    return status;
}

static NVENCSTATUS uu_reconfigure_encoder(
    void *encoder_handle, NV_ENC_RECONFIGURE_PARAMS *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle, &bridged);
    NVENCSTATUS status;

    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncReconfigureEncoder(encoder_handle,
                                                               params);
    status = origFunctions.nvEncReconfigureEncoder(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static void uu_free_resource(struct uu_nvenc_resource *resource)
{
    if (!resource)
        return;
    resource->magic = 0;
    if (resource->cuda_memory)
    {
        if (uu_push_encoder(resource->owner))
        {
            uu_cuMemFree(resource->cuda_memory);
            uu_pop_encoder();
        }
    }
    if (resource->staging)
        ID3D11Texture2D_Release(resource->staging);
    if (resource->texture)
        ID3D11Texture2D_Release(resource->texture);
    HeapFree(GetProcessHeap(), 0, resource);
}

/*
 * Copy the current D3D11 texture contents into the CUDA allocation backing a
 * registered NVENC resource.  UU 4.38 keeps input resources mapped across
 * frames, so doing this only from NvEncMapInputResource freezes the encoded
 * video on the first mapped image even while NVENC continues at full rate.
 */
static NVENCSTATUS uu_upload_d3d11_resource_locked(
    struct uu_nvenc_encoder *encoder,
    struct uu_nvenc_resource *resource)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    struct uu_CUDA_MEMCPY2D copy;
    NVENCSTATUS status = UU_NV_ENC_ERR_GENERIC;
    HRESULT hr;
    uint64_t upload_hash;
    uint64_t previous_hash;

    ID3D11Texture2D_GetDevice(resource->texture, &device);
    if (!device)
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    ID3D11Device_GetImmediateContext(device, &context);
    ID3D11Device_Release(device);
    if (!context)
        return UU_NV_ENC_ERR_INVALID_DEVICE;

    ID3D11DeviceContext_CopySubresourceRegion(
        context, (ID3D11Resource *)resource->staging, 0, 0, 0, 0,
        (ID3D11Resource *)resource->texture,
        resource->source_subresource, NULL);
    memset(&mapped, 0, sizeof(mapped));
    hr = ID3D11DeviceContext_Map(context,
                                 (ID3D11Resource *)resource->staging, 0,
                                 D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        ERR("failed to map NVENC staging texture: %#x\n", (unsigned int)hr);
        status = UU_NV_ENC_ERR_RESOURCE_NOT_MAPPED;
        goto done;
    }
    if (mapped.RowPitch < resource->row_bytes || !uu_push_encoder(encoder))
    {
        status = UU_NV_ENC_ERR_INVALID_PARAM;
        goto unmap;
    }
    upload_hash = uu_sample_mapped_texture(&mapped, resource);
    previous_hash = __atomic_exchange_n(
        &uu_last_upload_hash, upload_hash, __ATOMIC_RELAXED);
    __atomic_add_fetch(&uu_upload_calls, 1u, __ATOMIC_RELAXED);
    if (previous_hash && previous_hash != upload_hash)
        __atomic_add_fetch(
            &uu_upload_hash_changes, 1u, __ATOMIC_RELAXED);
    memset(&copy, 0, sizeof(copy));
    copy.srcMemoryType = UU_CU_MEMORYTYPE_HOST;
    copy.srcHost = mapped.pData;
    copy.srcPitch = mapped.RowPitch;
    copy.dstMemoryType = UU_CU_MEMORYTYPE_DEVICE;
    copy.dstDevice = resource->cuda_memory;
    copy.dstPitch = resource->cuda_pitch;
    copy.WidthInBytes = resource->row_bytes;
    copy.Height = resource->rows;
    __atomic_add_fetch(&uu_upload_2d_calls, 1u, __ATOMIC_RELAXED);
    status = uu_cuMemcpy2D(&copy) == UU_CUDA_SUCCESS ?
        NV_ENC_SUCCESS : UU_NV_ENC_ERR_MAP_FAILED;
    uu_pop_encoder();

unmap:
    ID3D11DeviceContext_Unmap(context,
                              (ID3D11Resource *)resource->staging, 0);
done:
    ID3D11DeviceContext_Release(context);
    return status;
}

static NVENCSTATUS uu_open_d3d11_session(
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *params, void **encoder)
{
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS native_params;
    struct uu_nvenc_encoder *bridge;
    NVENCSTATUS status;

    if (!params || !encoder || !params->device ||
        params->deviceType != NV_ENC_DEVICE_TYPE_DIRECTX)
        return origFunctions.nvEncOpenEncodeSessionEx(params, encoder);
    if (!uu_remote_load_cuda())
        return UU_NV_ENC_ERR_NO_ENCODE_DEVICE;

    bridge = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*bridge));
    if (!bridge)
        return UU_NV_ENC_ERR_OUT_OF_MEMORY;
    if (pthread_mutex_init(&bridge->mutex, NULL))
    {
        HeapFree(GetProcessHeap(), 0, bridge);
        return UU_NV_ENC_ERR_OUT_OF_MEMORY;
    }
    if (!uu_selected_cuda_device(&bridge->cuda_device) ||
        uu_cuDevicePrimaryCtxRetain(&bridge->cuda_context,
                                    bridge->cuda_device) != UU_CUDA_SUCCESS)
    {
        pthread_mutex_destroy(&bridge->mutex);
        HeapFree(GetProcessHeap(), 0, bridge);
        return UU_NV_ENC_ERR_NO_ENCODE_DEVICE;
    }
    if (!uu_push_encoder(bridge))
    {
        uu_cuDevicePrimaryCtxRelease(bridge->cuda_device);
        pthread_mutex_destroy(&bridge->mutex);
        HeapFree(GetProcessHeap(), 0, bridge);
        return UU_NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    native_params = *params;
    native_params.device = bridge->cuda_context;
    native_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    status = origFunctions.nvEncOpenEncodeSessionEx(&native_params,
                                                     &bridge->native_encoder);
    uu_pop_encoder();
    if (status != NV_ENC_SUCCESS || !bridge->native_encoder)
    {
        uu_cuDevicePrimaryCtxRelease(bridge->cuda_device);
        pthread_mutex_destroy(&bridge->mutex);
        HeapFree(GetProcessHeap(), 0, bridge);
        ERR("CUDA NvEncOpenEncodeSessionEx failed: %d\n", status);
        return status;
    }

    bridge->magic = UU_NVENC_ENCODER_MAGIC;
    uu_publish_encoder(bridge);
    *encoder = bridge->native_encoder;
    TRACE("translated D3D11 NVENC session %p to CUDA device %d context %p\n",
          bridge->native_encoder, bridge->cuda_device, bridge->cuda_context);
    return NV_ENC_SUCCESS;
}

static NVENCSTATUS uu_register_d3d11_resource(
    void *encoder_handle, NV_ENC_REGISTER_RESOURCE *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    NV_ENC_REGISTER_RESOURCE native_params;
    D3D11_TEXTURE2D_DESC source_desc;
    D3D11_TEXTURE2D_DESC staging_desc;
    NVENCSTATUS status;
    HRESULT hr;
    uint32_t source_width = 0;
    uint32_t source_height = 0;

    encoder = uu_lock_encoder(encoder_handle, &bridged);
    if (!encoder && bridged)
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    if (!encoder || !params ||
        params->resourceType != UU_NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX)
    {
        if (encoder)
            uu_unlock_encoder(encoder);
        return origFunctions.nvEncRegisterResource(encoder_handle, params);
    }
    if (!params->resourceToRegister || !params->width || !params->height)
    {
        uu_unlock_encoder(encoder);
        return UU_NV_ENC_ERR_INVALID_PARAM;
    }

    resource = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*resource));
    if (!resource)
    {
        uu_unlock_encoder(encoder);
        return UU_NV_ENC_ERR_OUT_OF_MEMORY;
    }
    resource->owner = encoder;
    resource->texture = (ID3D11Texture2D *)params->resourceToRegister;
    ID3D11Texture2D_AddRef(resource->texture);
    ID3D11Texture2D_GetDesc(resource->texture, &source_desc);
    resource->source_subresource = params->subResourceIndex;
    resource->format = params->bufferFormat;
    if (!uu_d3d11_subresource_dimensions(
            &source_desc, params->subResourceIndex,
            &source_width, &source_height) ||
        source_width != params->width || source_height != params->height ||
        !uu_d3d11_buffer_layout(
            params->bufferFormat, source_desc.Format,
            params->width, params->height,
            &resource->row_bytes, &resource->rows))
    {
        ERR("invalid D3D11 NVENC texture: format=%#x nvenc=%#x "
            "resource=%ux%u requested=%ux%u subresource=%u\n",
            source_desc.Format, params->bufferFormat,
            source_width, source_height, params->width, params->height,
            params->subResourceIndex);
        status = NV_ENC_ERR_UNSUPPORTED_PARAM;
        goto failed;
    }

    staging_desc = source_desc;
    staging_desc.Width = params->width;
    staging_desc.Height = params->height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.SampleDesc.Quality = 0;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    {
        ID3D11Device *device = NULL;
        ID3D11Texture2D_GetDevice(resource->texture, &device);
        if (!device)
        {
            status = UU_NV_ENC_ERR_INVALID_DEVICE;
            goto failed;
        }
        hr = ID3D11Device_CreateTexture2D(device, &staging_desc, NULL,
                                          &resource->staging);
        ID3D11Device_Release(device);
    }
    if (FAILED(hr) || !resource->staging)
    {
        ERR("failed to create NVENC staging texture: %#x\n", (unsigned int)hr);
        status = UU_NV_ENC_ERR_OUT_OF_MEMORY;
        goto failed;
    }
    if (!uu_push_encoder(encoder))
    {
        status = UU_NV_ENC_ERR_INVALID_DEVICE;
        goto failed;
    }
    if (uu_cuMemAllocPitch(&resource->cuda_memory, &resource->cuda_pitch,
                           resource->row_bytes, resource->rows, 16) !=
        UU_CUDA_SUCCESS)
    {
        status = UU_NV_ENC_ERR_OUT_OF_MEMORY;
        goto failed_pop;
    }

    native_params = *params;
    native_params.resourceType = UU_NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    native_params.resourceToRegister = (void *)(uintptr_t)resource->cuda_memory;
    native_params.pitch = (uint32_t)resource->cuda_pitch;
    native_params.subResourceIndex = 0;
    native_params.registeredResource = NULL;
    status = origFunctions.nvEncRegisterResource(encoder_handle, &native_params);
    uu_pop_encoder();
    if (status != NV_ENC_SUCCESS)
        goto failed;

    resource->native_resource = native_params.registeredResource;
    resource->magic = UU_NVENC_RESOURCE_MAGIC;
    resource->next = encoder->resources;
    encoder->resources = resource;
    params->registeredResource = (NV_ENC_REGISTERED_PTR)resource;
    params->chromaOffset[0] = native_params.chromaOffset[0];
    params->chromaOffset[1] = native_params.chromaOffset[1];
    TRACE("registered D3D11 NVENC texture %ux%u format=%#x row=%u rows=%u cuda_pitch=%zu\n",
          params->width, params->height, params->bufferFormat,
          resource->row_bytes, resource->rows, resource->cuda_pitch);
    uu_unlock_encoder(encoder);
    return NV_ENC_SUCCESS;

failed_pop:
    uu_pop_encoder();
failed:
    uu_free_resource(resource);
    uu_unlock_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_upload_and_map_resource(
    void *encoder_handle, NV_ENC_MAP_INPUT_RESOURCE *params)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    NV_ENC_MAP_INPUT_RESOURCE native_params;
    NVENCSTATUS status = UU_NV_ENC_ERR_GENERIC;
    uint64_t calls;

    calls = __atomic_add_fetch(&uu_map_calls, 1u, __ATOMIC_RELAXED);

    if (!params)
        return NV_ENC_ERR_INVALID_PTR;
    encoder = uu_lock_encoder(encoder_handle, &bridged);
    if (!encoder && bridged)
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    resource = uu_find_resource(encoder, params->registeredResource);
    if (resource)
        __atomic_add_fetch(&uu_map_matches, 1u, __ATOMIC_RELAXED);
    if (!resource)
    {
        if (encoder)
            uu_unlock_encoder(encoder);
        return origFunctions.nvEncMapInputResource(encoder_handle, params);
    }

    status = uu_upload_d3d11_resource_locked(encoder, resource);
    if (status != NV_ENC_SUCCESS)
        goto done;
    if (!uu_push_encoder(encoder))
    {
        status = UU_NV_ENC_ERR_INVALID_DEVICE;
        goto done;
    }
    native_params = *params;
    native_params.registeredResource = resource->native_resource;
    status = origFunctions.nvEncMapInputResource(encoder_handle, &native_params);
    uu_pop_encoder();
    if (status == NV_ENC_SUCCESS)
    {
        params->mappedResource = native_params.mappedResource;
        params->mappedBufferFmt = native_params.mappedBufferFmt;
        resource->mapped_resource = native_params.mappedResource;
        resource->upload_fresh = TRUE;
    }
done:
    uu_unlock_encoder(encoder);
    if (calls == 1u || calls % 120u == 0u)
        uu_write_bridge_status();
    return status;
}

static NVENCSTATUS uu_unregister_bridge_resource_locked(
    void *encoder_handle,
    struct uu_nvenc_encoder *encoder,
    struct uu_nvenc_resource *resource)
{
    NVENCSTATUS status;

    if (!uu_push_encoder(encoder))
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    status = origFunctions.nvEncUnregisterResource(
        encoder_handle, resource->native_resource);
    uu_pop_encoder();
    return uu_complete_resource_unregister(encoder, resource, status);
}

static NVENCSTATUS uu_unregister_resource(
    void *encoder_handle, NV_ENC_REGISTERED_PTR handle)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    NVENCSTATUS status;

    encoder = uu_lock_encoder(encoder_handle, &bridged);
    if (!encoder && bridged)
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    resource = uu_find_resource(encoder, handle);
    if (!resource)
    {
        if (encoder)
            uu_unlock_encoder(encoder);
        return origFunctions.nvEncUnregisterResource(encoder_handle, handle);
    }

    status = uu_unregister_bridge_resource_locked(
        encoder_handle, encoder, resource);
    uu_unlock_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_destroy_encoder(void *encoder_handle)
{
    BOOL bridged;
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    NVENCSTATUS status;

    encoder = uu_begin_encoder_destroy(encoder_handle, &bridged);
    if (!encoder)
        return bridged ? UU_NV_ENC_ERR_INVALID_DEVICE :
                         origFunctions.nvEncDestroyEncoder(encoder_handle);

    while ((resource = encoder->resources))
    {
        status = uu_unregister_bridge_resource_locked(
            encoder_handle, encoder, resource);
        if (status != NV_ENC_SUCCESS)
        {
            uu_cancel_encoder_destroy(encoder);
            return status;
        }
    }
    if (!uu_push_encoder(encoder))
    {
        uu_cancel_encoder_destroy(encoder);
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    }
    status = origFunctions.nvEncDestroyEncoder(encoder_handle);
    uu_pop_encoder();
    if (status != NV_ENC_SUCCESS)
    {
        uu_cancel_encoder_destroy(encoder);
        return status;
    }
    uu_cuDevicePrimaryCtxRelease(encoder->cuda_device);
    uu_retire_destroyed_encoder(encoder);
    pthread_mutex_unlock(&encoder->mutex);
    pthread_mutex_destroy(&encoder->mutex);
    encoder->magic = 0;
    HeapFree(GetProcessHeap(), 0, encoder);
    return status;
}
