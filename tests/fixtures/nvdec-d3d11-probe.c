#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cuda.h"

#define CU_MEMORYTYPE_HOST 1
#define CU_MEMORYTYPE_DEVICE 2
#define CU_MEMORYTYPE_ARRAY 3
#define CU_ERROR_INVALID_DEVICE 101

typedef CUresult (WINAPI *cu_init_fn)(unsigned int);
typedef CUresult (WINAPI *cu_device_get_fn)(CUdevice *, int);
typedef CUresult (WINAPI *cu_d3d11_get_device_fn)(CUdevice *, IDXGIAdapter *);
typedef CUresult (WINAPI *cu_ctx_create_fn)(CUcontext *, unsigned int, CUdevice);
typedef CUresult (WINAPI *cu_ctx_destroy_fn)(CUcontext);
typedef CUresult (WINAPI *cu_mem_alloc_fn)(CUdeviceptr *, size_t);
typedef CUresult (WINAPI *cu_mem_free_fn)(CUdeviceptr);
typedef CUresult (WINAPI *cu_memcpy_htod_fn)(CUdeviceptr, const void *, size_t);
typedef CUresult (WINAPI *cu_graphics_register_fn)(
    CUgraphicsResource *, ID3D11Resource *, unsigned int
);
typedef CUresult (WINAPI *cu_graphics_map_fn)(
    unsigned int, CUgraphicsResource *, CUstream
);
typedef CUresult (WINAPI *cu_graphics_get_array_fn)(
    CUarray *, CUgraphicsResource, unsigned int, unsigned int
);
typedef CUresult (WINAPI *cu_memcpy_2d_fn)(const CUDA_MEMCPY2D *);
typedef CUresult (WINAPI *cu_graphics_unmap_fn)(
    unsigned int, CUgraphicsResource *, CUstream
);
typedef CUresult (WINAPI *cu_graphics_unregister_fn)(CUgraphicsResource);

struct cuda_api {
    cu_init_fn init;
    cu_device_get_fn device_get;
    cu_d3d11_get_device_fn d3d11_get_device;
    cu_ctx_create_fn ctx_create;
    cu_ctx_destroy_fn ctx_destroy;
    cu_mem_alloc_fn mem_alloc;
    cu_mem_free_fn mem_free;
    cu_memcpy_htod_fn memcpy_htod;
    cu_graphics_register_fn graphics_register;
    cu_graphics_map_fn graphics_map;
    cu_graphics_get_array_fn graphics_get_array;
    cu_memcpy_2d_fn memcpy_2d;
    cu_graphics_unmap_fn graphics_unmap;
    cu_graphics_unregister_fn graphics_unregister;
};

static int load_cuda(struct cuda_api *api) {
    HMODULE module = LoadLibraryA("nvcuda.dll");

    if (!module) {
        fprintf(stderr, "LoadLibrary(nvcuda.dll) failed: %lu\n", GetLastError());
        return 0;
    }
#define LOAD_FIELD(field, symbol) do { \
    FARPROC address = GetProcAddress(module, symbol); \
    memcpy(&api->field, &address, sizeof(api->field)); \
    if (!api->field) { \
        fprintf(stderr, "missing CUDA symbol: %s\n", symbol); \
        return 0; \
    } \
} while (0)
    LOAD_FIELD(init, "cuInit");
    LOAD_FIELD(device_get, "cuDeviceGet");
    LOAD_FIELD(d3d11_get_device, "cuD3D11GetDevice");
    LOAD_FIELD(ctx_create, "cuCtxCreate_v2");
    LOAD_FIELD(ctx_destroy, "cuCtxDestroy_v2");
    LOAD_FIELD(mem_alloc, "cuMemAlloc_v2");
    LOAD_FIELD(mem_free, "cuMemFree_v2");
    LOAD_FIELD(memcpy_htod, "cuMemcpyHtoD_v2");
    LOAD_FIELD(graphics_register, "cuGraphicsD3D11RegisterResource");
    LOAD_FIELD(graphics_map, "cuGraphicsMapResources");
    LOAD_FIELD(
        graphics_get_array, "cuGraphicsSubResourceGetMappedArray"
    );
    LOAD_FIELD(memcpy_2d, "cuMemcpy2D_v2");
    LOAD_FIELD(graphics_unmap, "cuGraphicsUnmapResources");
    LOAD_FIELD(graphics_unregister, "cuGraphicsUnregisterResource");
#undef LOAD_FIELD
    return 1;
}

static HRESULT create_device(
    ID3D11Device **device,
    ID3D11DeviceContext **context
) {
    D3D_FEATURE_LEVEL level;

    return D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        NULL,
        0,
        D3D11_SDK_VERSION,
        device,
        &level,
        context
    );
}

static int verify_texture(
    ID3D11Device *device,
    ID3D11DeviceContext *context,
    ID3D11Texture2D *shared_texture,
    const unsigned char *expected,
    UINT width,
    UINT height
) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Texture2D *staging = NULL;
    HRESULT result;
    UINT row;
    int matches = 1;

    ID3D11Texture2D_GetDesc(shared_texture, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    result = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(result)) {
        fprintf(stderr, "CreateTexture2D(staging) failed: %#lx\n", result);
        return 0;
    }
    ID3D11DeviceContext_CopyResource(
        context, (ID3D11Resource *)staging, (ID3D11Resource *)shared_texture
    );
    result = ID3D11DeviceContext_Map(
        context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped
    );
    if (FAILED(result)) {
        fprintf(stderr, "Map(staging) failed: %#lx\n", result);
        ID3D11Texture2D_Release(staging);
        return 0;
    }
    for (row = 0; row < height + height / 2; ++row) {
        const unsigned char *actual =
            (const unsigned char *)mapped.pData + row * mapped.RowPitch;
        if (memcmp(actual, expected + row * width, width) != 0) {
            fprintf(
                stderr,
                "texture mismatch at row %u: got=%02x expected=%02x pitch=%u\n",
                row,
                actual[0],
                expected[row * width],
                mapped.RowPitch
            );
            matches = 0;
            break;
        }
    }
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    ID3D11Texture2D_Release(staging);
    return matches;
}

int main(int argc, char **argv) {
    const UINT width = 64;
    const UINT height = 64;
    const size_t frame_size = width * (height + height / 2);
    const int explicit_flush = argc > 1 && strcmp(argv[1], "--flush") == 0;
    const int expect_invalid_device =
        argc > 1 && strcmp(argv[1], "--expect-invalid-device") == 0;
    struct cuda_api cuda = {0};
    unsigned char frame[64 * 96];
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Device *producer_device = NULL;
    ID3D11DeviceContext *producer_context = NULL;
    ID3D11Device *consumer_device = NULL;
    ID3D11DeviceContext *consumer_context = NULL;
    ID3D11Texture2D *producer_texture = NULL;
    ID3D11Texture2D *consumer_texture = NULL;
    IDXGIResource *dxgi_resource = NULL;
    HANDLE shared_handle = NULL;
    CUdevice cuda_device;
    CUcontext cuda_context = NULL;
    CUdeviceptr cuda_frame = 0;
    CUgraphicsResource cuda_resource = NULL;
    CUarray cuda_array = NULL;
    CUDA_MEMCPY2D copy;
    HRESULT hresult;
    CUresult result;
    size_t index;
    int success = 0;

    for (index = 0; index < frame_size; ++index) {
        frame[index] = index < width * height ?
            (unsigned char)(16 + (index % 200)) :
            (unsigned char)(64 + (index % 128));
    }
    if (!load_cuda(&cuda)) {
        goto cleanup;
    }
    result = cuda.init(0);
    if (result == CUDA_SUCCESS && expect_invalid_device) {
        result = cuda.d3d11_get_device(&cuda_device, NULL);
        success = result == CU_ERROR_INVALID_DEVICE;
        printf(
            "NVDEC CUDA device selection probe: %s (result=%d)\n",
            success ? "PASS" : "FAIL",
            result
        );
        goto cleanup;
    }
    if (result != CUDA_SUCCESS ||
        (result = cuda.device_get(&cuda_device, 0)) != CUDA_SUCCESS ||
        (result = cuda.ctx_create(&cuda_context, 0, cuda_device)) != CUDA_SUCCESS) {
        fprintf(stderr, "CUDA initialization failed: %d\n", result);
        goto cleanup;
    }
    hresult = create_device(&producer_device, &producer_context);
    if (FAILED(hresult)) {
        fprintf(stderr, "producer D3D11CreateDevice failed: %#lx\n", hresult);
        goto cleanup;
    }
    hresult = create_device(&consumer_device, &consumer_context);
    if (FAILED(hresult)) {
        fprintf(stderr, "consumer D3D11CreateDevice failed: %#lx\n", hresult);
        goto cleanup;
    }
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hresult = ID3D11Device_CreateTexture2D(
        producer_device, &desc, NULL, &producer_texture
    );
    if (FAILED(hresult)) {
        fprintf(stderr, "CreateTexture2D(NV12 shared) failed: %#lx\n", hresult);
        goto cleanup;
    }
    hresult = ID3D11Texture2D_QueryInterface(
        producer_texture, &IID_IDXGIResource, (void **)&dxgi_resource
    );
    if (FAILED(hresult) ||
        FAILED(IDXGIResource_GetSharedHandle(dxgi_resource, &shared_handle))) {
        fprintf(stderr, "GetSharedHandle failed: %#lx\n", hresult);
        goto cleanup;
    }
    hresult = ID3D11Device_OpenSharedResource(
        consumer_device,
        shared_handle,
        &IID_ID3D11Texture2D,
        (void **)&consumer_texture
    );
    if (FAILED(hresult)) {
        fprintf(stderr, "OpenSharedResource failed: %#lx\n", hresult);
        goto cleanup;
    }
    result = cuda.mem_alloc(&cuda_frame, frame_size);
    if (result != CUDA_SUCCESS ||
        (result = cuda.memcpy_htod(cuda_frame, frame, frame_size)) != CUDA_SUCCESS ||
        (result = cuda.graphics_register(
            &cuda_resource, (ID3D11Resource *)producer_texture, 0
        )) != CUDA_SUCCESS ||
        (result = cuda.graphics_map(1, &cuda_resource, NULL)) != CUDA_SUCCESS ||
        (result = cuda.graphics_get_array(
            &cuda_array, cuda_resource, 0, 0
        )) != CUDA_SUCCESS) {
        fprintf(stderr, "CUDA resource setup failed: %d\n", result);
        goto cleanup;
    }
    ZeroMemory(&copy, sizeof(copy));
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = cuda_frame;
    copy.srcPitch = width;
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = cuda_array;
    copy.WidthInBytes = width;
    copy.Height = height + height / 2;
    result = cuda.memcpy_2d(&copy);
    if (result != CUDA_SUCCESS ||
        (result = cuda.graphics_unmap(1, &cuda_resource, NULL)) != CUDA_SUCCESS) {
        fprintf(stderr, "CUDA texture upload failed: %d\n", result);
        goto cleanup;
    }
    if (explicit_flush) {
        ID3D11DeviceContext_Flush(producer_context);
    }
    success = verify_texture(
        consumer_device,
        consumer_context,
        consumer_texture,
        frame,
        width,
        height
    );
    printf(
        "NVDEC D3D11 shared NV12 probe: %s (flush=%s)\n",
        success ? "PASS" : "FAIL",
        explicit_flush ? "yes" : "no"
    );

cleanup:
    if (cuda_resource) {
        cuda.graphics_unregister(cuda_resource);
    }
    if (cuda_frame) {
        cuda.mem_free(cuda_frame);
    }
    if (cuda_context) {
        cuda.ctx_destroy(cuda_context);
    }
    if (dxgi_resource) IDXGIResource_Release(dxgi_resource);
    if (consumer_texture) ID3D11Texture2D_Release(consumer_texture);
    if (producer_texture) ID3D11Texture2D_Release(producer_texture);
    if (consumer_context) ID3D11DeviceContext_Release(consumer_context);
    if (consumer_device) ID3D11Device_Release(consumer_device);
    if (producer_context) ID3D11DeviceContext_Release(producer_context);
    if (producer_device) ID3D11Device_Release(producer_device);
    return success ? 0 : 1;
}
