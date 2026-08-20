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
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

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

struct uu_nvenc_encoder;

struct uu_nvenc_resource
{
    uint64_t magic;
    struct uu_nvenc_encoder *owner;
    NV_ENC_REGISTERED_PTR native_resource;
    ID3D11Texture2D *texture;
    ID3D11Texture2D *staging;
    uu_CUdeviceptr cuda_memory;
    size_t cuda_pitch;
    uint32_t source_subresource;
    uint32_t row_bytes;
    uint32_t rows;
    NV_ENC_BUFFER_FORMAT format;
    struct uu_nvenc_resource *next;
};

struct uu_nvenc_encoder
{
    uint64_t magic;
    void *native_encoder;
    uu_CUdevice cuda_device;
    uu_CUcontext cuda_context;
    pthread_mutex_t mutex;
    struct uu_nvenc_resource *resources;
    struct uu_nvenc_encoder *next;
};

static void *uu_cuda_handle;
static pthread_mutex_t uu_bridge_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct uu_nvenc_encoder *uu_encoders;

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
static uu_CUresult (*uu_cuMemcpyHtoD)(uu_CUdeviceptr, const void *, size_t);

static BOOL uu_load_cuda_symbol(void **target, const char *name)
{
    *target = dlsym(uu_cuda_handle, name);
    if (!*target)
    {
        ERR("missing CUDA symbol %s: %s\n", name, dlerror());
        return FALSE;
    }
    return TRUE;
}

static BOOL uu_remote_load_cuda(void)
{
    if (uu_cuda_handle)
        return TRUE;

    uu_cuda_handle = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!uu_cuda_handle)
    {
        ERR("cannot load libcuda.so.1: %s\n", dlerror());
        return FALSE;
    }

    return uu_load_cuda_symbol((void **)&uu_cuInit, "cuInit") &&
           uu_load_cuda_symbol((void **)&uu_cuDeviceGetCount, "cuDeviceGetCount") &&
           uu_load_cuda_symbol((void **)&uu_cuDeviceGet, "cuDeviceGet") &&
           uu_load_cuda_symbol((void **)&uu_cuDevicePrimaryCtxRetain,
                               "cuDevicePrimaryCtxRetain") &&
           uu_load_cuda_symbol((void **)&uu_cuDevicePrimaryCtxRelease,
                               "cuDevicePrimaryCtxRelease_v2") &&
           uu_load_cuda_symbol((void **)&uu_cuCtxPushCurrent,
                               "cuCtxPushCurrent_v2") &&
           uu_load_cuda_symbol((void **)&uu_cuCtxPopCurrent,
                               "cuCtxPopCurrent_v2") &&
           uu_load_cuda_symbol((void **)&uu_cuMemAllocPitch,
                               "cuMemAllocPitch_v2") &&
           uu_load_cuda_symbol((void **)&uu_cuMemFree, "cuMemFree_v2") &&
           uu_load_cuda_symbol((void **)&uu_cuMemcpyHtoD,
                               "cuMemcpyHtoD_v2");
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

static struct uu_nvenc_encoder *uu_find_encoder(void *native_encoder)
{
    struct uu_nvenc_encoder *encoder;

    for (encoder = uu_encoders; encoder; encoder = encoder->next)
        if (encoder->magic == UU_NVENC_ENCODER_MAGIC &&
            encoder->native_encoder == native_encoder)
            return encoder;
    return NULL;
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

static BOOL uu_push_encoder(struct uu_nvenc_encoder *encoder)
{
    return encoder && uu_cuCtxPushCurrent(encoder->cuda_context) == UU_CUDA_SUCCESS;
}

static void uu_pop_encoder(void)
{
    uu_CUcontext previous = NULL;
    uu_cuCtxPopCurrent(&previous);
}

static struct uu_nvenc_encoder *uu_lock_encoder(void *encoder_handle)
{
    struct uu_nvenc_encoder *encoder;

    pthread_mutex_lock(&uu_bridge_mutex);
    encoder = uu_find_encoder(encoder_handle);
    if (encoder)
        pthread_mutex_lock(&encoder->mutex);
    pthread_mutex_unlock(&uu_bridge_mutex);
    return encoder;
}

static void uu_unlock_encoder(struct uu_nvenc_encoder *encoder)
{
    pthread_mutex_unlock(&encoder->mutex);
}

static struct uu_nvenc_encoder *uu_enter_encoder(void *encoder_handle)
{
    struct uu_nvenc_encoder *encoder = uu_lock_encoder(encoder_handle);

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
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncInitializeEncoder(encoder_handle, params);
    status = origFunctions.nvEncInitializeEncoder(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_create_bitstream_buffer(
    void *encoder_handle, NV_ENC_CREATE_BITSTREAM_BUFFER *params)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncCreateBitstreamBuffer(encoder_handle, params);
    status = origFunctions.nvEncCreateBitstreamBuffer(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_destroy_bitstream_buffer(
    void *encoder_handle, NV_ENC_OUTPUT_PTR buffer)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncDestroyBitstreamBuffer(encoder_handle, buffer);
    status = origFunctions.nvEncDestroyBitstreamBuffer(encoder_handle, buffer);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_encode_picture(
    void *encoder_handle, NV_ENC_PIC_PARAMS *params)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncEncodePicture(encoder_handle, params);
    status = origFunctions.nvEncEncodePicture(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_lock_bitstream(
    void *encoder_handle, NV_ENC_LOCK_BITSTREAM *params)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncLockBitstream(encoder_handle, params);
    status = origFunctions.nvEncLockBitstream(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_unlock_bitstream(
    void *encoder_handle, NV_ENC_OUTPUT_PTR buffer)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncUnlockBitstream(encoder_handle, buffer);
    status = origFunctions.nvEncUnlockBitstream(encoder_handle, buffer);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_unmap_input_resource(
    void *encoder_handle, NV_ENC_INPUT_PTR resource)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncUnmapInputResource(encoder_handle, resource);
    status = origFunctions.nvEncUnmapInputResource(encoder_handle, resource);
    uu_leave_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_reconfigure_encoder(
    void *encoder_handle, NV_ENC_RECONFIGURE_PARAMS *params)
{
    struct uu_nvenc_encoder *encoder = uu_enter_encoder(encoder_handle);
    NVENCSTATUS status;

    if (!encoder)
        return origFunctions.nvEncReconfigureEncoder(encoder_handle, params);
    status = origFunctions.nvEncReconfigureEncoder(encoder_handle, params);
    uu_leave_encoder(encoder);
    return status;
}

static BOOL uu_buffer_layout(NV_ENC_BUFFER_FORMAT format, uint32_t width,
                             uint32_t height, uint32_t *row_bytes,
                             uint32_t *rows)
{
    switch (format)
    {
        case NV_ENC_BUFFER_FORMAT_NV12:
            *row_bytes = width;
            *rows = height + (height + 1) / 2;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_NV16:
            *row_bytes = width;
            if (height > UINT32_MAX / 2)
                return FALSE;
            *rows = height * 2;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
            if (width > UINT32_MAX / 2)
                return FALSE;
            *row_bytes = width * 2;
            *rows = height + (height + 1) / 2;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_P210:
            if (width > UINT32_MAX / 2 || height > UINT32_MAX / 2)
                return FALSE;
            *row_bytes = width * 2;
            *rows = height * 2;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_ARGB:
        case NV_ENC_BUFFER_FORMAT_ARGB10:
        case NV_ENC_BUFFER_FORMAT_AYUV:
        case NV_ENC_BUFFER_FORMAT_ABGR:
        case NV_ENC_BUFFER_FORMAT_ABGR10:
            if (width > UINT32_MAX / 4)
                return FALSE;
            *row_bytes = width * 4;
            *rows = height;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_YUV444:
            *row_bytes = width;
            if (height > UINT32_MAX / 3)
                return FALSE;
            *rows = height * 3;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
            if (width > UINT32_MAX / 2 || height > UINT32_MAX / 3)
                return FALSE;
            *row_bytes = width * 2;
            *rows = height * 3;
            return TRUE;
        default:
            ERR("unsupported D3D11 NVENC input format %#x\n", format);
            return FALSE;
    }
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
    pthread_mutex_lock(&uu_bridge_mutex);
    bridge->next = uu_encoders;
    uu_encoders = bridge;
    pthread_mutex_unlock(&uu_bridge_mutex);
    *encoder = bridge->native_encoder;
    TRACE("translated D3D11 NVENC session %p to CUDA device %d context %p\n",
          bridge->native_encoder, bridge->cuda_device, bridge->cuda_context);
    return NV_ENC_SUCCESS;
}

static NVENCSTATUS uu_register_d3d11_resource(
    void *encoder_handle, NV_ENC_REGISTER_RESOURCE *params)
{
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    NV_ENC_REGISTER_RESOURCE native_params;
    D3D11_TEXTURE2D_DESC source_desc;
    D3D11_TEXTURE2D_DESC staging_desc;
    NVENCSTATUS status;
    HRESULT hr;

    encoder = uu_lock_encoder(encoder_handle);
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
    if (!uu_buffer_layout(params->bufferFormat, params->width, params->height,
                          &resource->row_bytes, &resource->rows))
    {
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
    if (!uu_push_encoder(encoder) ||
        uu_cuMemAllocPitch(&resource->cuda_memory, &resource->cuda_pitch,
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
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    NV_ENC_MAP_INPUT_RESOURCE native_params;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    NVENCSTATUS status = UU_NV_ENC_ERR_GENERIC;
    HRESULT hr;
    uint32_t row;

    if (!params)
        return NV_ENC_ERR_INVALID_PTR;
    encoder = uu_lock_encoder(encoder_handle);
    resource = uu_find_resource(encoder, params->registeredResource);
    if (!resource)
    {
        if (encoder)
            uu_unlock_encoder(encoder);
        return origFunctions.nvEncMapInputResource(encoder_handle, params);
    }

    ID3D11Texture2D_GetDevice(resource->texture, &device);
    if (!device)
    {
        uu_unlock_encoder(encoder);
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    }
    ID3D11Device_GetImmediateContext(device, &context);
    ID3D11Device_Release(device);
    if (!context)
    {
        uu_unlock_encoder(encoder);
        return UU_NV_ENC_ERR_INVALID_DEVICE;
    }

    ID3D11DeviceContext_CopySubresourceRegion(
        context, (ID3D11Resource *)resource->staging, 0, 0, 0, 0,
        (ID3D11Resource *)resource->texture, resource->source_subresource, NULL);
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
    for (row = 0; row < resource->rows; ++row)
    {
        const BYTE *source = (const BYTE *)mapped.pData +
                             (size_t)row * mapped.RowPitch;
        uu_CUdeviceptr destination = resource->cuda_memory +
                                     (size_t)row * resource->cuda_pitch;
        if (uu_cuMemcpyHtoD(destination, source, resource->row_bytes) !=
            UU_CUDA_SUCCESS)
        {
            status = UU_NV_ENC_ERR_MAP_FAILED;
            uu_pop_encoder();
            goto unmap;
        }
    }

    native_params = *params;
    native_params.registeredResource = resource->native_resource;
    status = origFunctions.nvEncMapInputResource(encoder_handle, &native_params);
    uu_pop_encoder();
    if (status == NV_ENC_SUCCESS)
    {
        params->mappedResource = native_params.mappedResource;
        params->mappedBufferFmt = native_params.mappedBufferFmt;
    }

unmap:
    ID3D11DeviceContext_Unmap(context,
                              (ID3D11Resource *)resource->staging, 0);
done:
    ID3D11DeviceContext_Release(context);
    uu_unlock_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_unregister_resource(
    void *encoder_handle, NV_ENC_REGISTERED_PTR handle)
{
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_resource *resource;
    struct uu_nvenc_resource **cursor;
    NVENCSTATUS status;

    encoder = uu_lock_encoder(encoder_handle);
    resource = uu_find_resource(encoder, handle);
    if (resource)
    {
        for (cursor = &encoder->resources; *cursor; cursor = &(*cursor)->next)
            if (*cursor == resource)
            {
                *cursor = resource->next;
                break;
            }
    }
    if (!resource)
    {
        if (encoder)
            uu_unlock_encoder(encoder);
        return origFunctions.nvEncUnregisterResource(encoder_handle, handle);
    }

    if (!uu_push_encoder(encoder))
        status = UU_NV_ENC_ERR_INVALID_DEVICE;
    else
    {
        status = origFunctions.nvEncUnregisterResource(
            encoder_handle, resource->native_resource);
        uu_pop_encoder();
    }
    uu_free_resource(resource);
    uu_unlock_encoder(encoder);
    return status;
}

static NVENCSTATUS uu_destroy_encoder(void *encoder_handle)
{
    struct uu_nvenc_encoder *encoder;
    struct uu_nvenc_encoder **cursor;
    struct uu_nvenc_resource *resource;
    NVENCSTATUS status;

    pthread_mutex_lock(&uu_bridge_mutex);
    encoder = uu_find_encoder(encoder_handle);
    if (encoder)
    {
        for (cursor = &uu_encoders; *cursor; cursor = &(*cursor)->next)
            if (*cursor == encoder)
            {
                *cursor = encoder->next;
                break;
            }
        pthread_mutex_lock(&encoder->mutex);
    }
    pthread_mutex_unlock(&uu_bridge_mutex);
    if (!encoder)
        return origFunctions.nvEncDestroyEncoder(encoder_handle);

    while ((resource = encoder->resources))
    {
        encoder->resources = resource->next;
        uu_free_resource(resource);
    }
    if (uu_push_encoder(encoder))
    {
        status = origFunctions.nvEncDestroyEncoder(encoder_handle);
        uu_pop_encoder();
    }
    else
        status = UU_NV_ENC_ERR_INVALID_DEVICE;
    encoder->magic = 0;
    uu_cuDevicePrimaryCtxRelease(encoder->cuda_device);
    pthread_mutex_unlock(&encoder->mutex);
    pthread_mutex_destroy(&encoder->mutex);
    HeapFree(GetProcessHeap(), 0, encoder);
    return status;
}
