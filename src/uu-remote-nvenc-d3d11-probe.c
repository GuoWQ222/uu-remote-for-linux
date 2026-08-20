/*
 * SPDX-License-Identifier: 0BSD
 *
 * Verify the exact API boundary used by UU's NvEncoderD3D11 implementation:
 * a real DXGI adapter, a D3D11 device, and an nvencodeapi64 encode session.
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nvEncodeAPI.h"

typedef NVENCSTATUS(WINAPI *get_max_version_fn)(uint32_t *);
typedef NVENCSTATUS(WINAPI *create_instance_fn)(NV_ENCODE_API_FUNCTION_LIST *);

static const GUID h264_guid = {
    0x6bc82762, 0x4e63, 0x4ca4,
    {0xaa, 0x85, 0x1e, 0x50, 0xf3, 0x21, 0xf6, 0xbf}
};
static const GUID hevc_guid = {
    0x790cdc88, 0x4522, 0x4d7b,
    {0x94, 0x25, 0xbd, 0xa9, 0x97, 0x5f, 0x76, 0x03}
};

static int guid_equal(const GUID *left, const GUID *right)
{
    return !memcmp(left, right, sizeof(*left));
}

int main(void)
{
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *texture = NULL;
    ID3D11Texture2D *nv12_texture = NULL;
    ID3D11RenderTargetView *render_target = NULL;
    HMODULE nvenc_module = NULL;
    get_max_version_fn get_max_version = NULL;
    create_instance_fn create_instance = NULL;
    FARPROC symbol;
    NV_ENCODE_API_FUNCTION_LIST functions = {0};
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_params = {0};
    NV_ENC_INITIALIZE_PARAMS initialize_params = {0};
    NV_ENC_REGISTER_RESOURCE register_params = {0};
    NV_ENC_MAP_INPUT_RESOURCE map_params = {0};
    NV_ENC_REGISTER_RESOURCE nv12_register_params = {0};
    NV_ENC_MAP_INPUT_RESOURCE nv12_map_params = {0};
    D3D11_TEXTURE2D_DESC texture_desc;
    D3D11_TEXTURE2D_DESC nv12_texture_desc;
    D3D11_SUBRESOURCE_DATA nv12_initial_data;
    DXGI_ADAPTER_DESC1 adapter_desc;
    GUID encode_guids[16];
    uint32_t max_version = 0;
    uint32_t guid_count = 0;
    UINT adapter_index;
    void *encoder = NULL;
    HRESULT hr;
    NVENCSTATUS status;
    int h264 = 0;
    int hevc = 0;
    int texture_upload = 0;
    int nv12_upload = 0;
    BYTE *nv12_pixels = NULL;
    int result = 1;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr)) {
        fprintf(stderr, "CreateDXGIFactory1 failed: 0x%08lx\n", (unsigned long)hr);
        goto done;
    }
    for (adapter_index = 0; ; ++adapter_index) {
        hr = IDXGIFactory1_EnumAdapters1(factory, adapter_index, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            fprintf(stderr, "no NVIDIA DXGI adapter found\n");
            goto done;
        }
        if (FAILED(hr)) {
            fprintf(stderr, "EnumAdapters1 failed: 0x%08lx\n", (unsigned long)hr);
            goto done;
        }
        memset(&adapter_desc, 0, sizeof(adapter_desc));
        hr = IDXGIAdapter1_GetDesc1(adapter, &adapter_desc);
        if (SUCCEEDED(hr) && adapter_desc.VendorId == 0x10de &&
            !(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            break;
        }
        IDXGIAdapter1_Release(adapter);
        adapter = NULL;
    }

    hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN,
                           NULL, 0, NULL, 0,
                           D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) {
        fprintf(stderr, "D3D11CreateDevice failed: 0x%08lx\n", (unsigned long)hr);
        goto done;
    }
    memset(&texture_desc, 0, sizeof(texture_desc));
    texture_desc.Width = 1280;
    texture_desc.Height = 720;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    if (FAILED(hr)) {
        fprintf(stderr, "CreateTexture2D failed: 0x%08lx\n", (unsigned long)hr);
        goto done;
    }
    hr = ID3D11Device_CreateRenderTargetView(
        device, (ID3D11Resource *)texture, NULL, &render_target);
    if (FAILED(hr)) {
        fprintf(stderr, "CreateRenderTargetView failed: 0x%08lx\n",
                (unsigned long)hr);
        goto done;
    }
    {
        const float clear_color[4] = {0.125f, 0.25f, 0.5f, 1.0f};
        ID3D11DeviceContext_ClearRenderTargetView(
            context, render_target, clear_color);
        ID3D11DeviceContext_Flush(context);
    }

    nvenc_module = LoadLibraryA("nvEncodeAPI64.dll");
    if (!nvenc_module) {
        fprintf(stderr, "LoadLibrary(nvEncodeAPI64.dll) failed: %lu\n", GetLastError());
        goto done;
    }
    symbol = GetProcAddress(nvenc_module, "NvEncodeAPIGetMaxSupportedVersion");
    memcpy(&get_max_version, &symbol, sizeof(get_max_version));
    symbol = GetProcAddress(nvenc_module, "NvEncodeAPICreateInstance");
    memcpy(&create_instance, &symbol, sizeof(create_instance));
    if (!get_max_version || !create_instance) {
        fprintf(stderr, "nvencodeapi64 exports are incomplete\n");
        goto done;
    }
    status = get_max_version(&max_version);
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncodeAPIGetMaxSupportedVersion failed: %d\n", status);
        goto done;
    }

    memset(&functions, 0, sizeof(functions));
    functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    status = create_instance(&functions);
    if (status != NV_ENC_SUCCESS || !functions.nvEncOpenEncodeSessionEx ||
        !functions.nvEncGetEncodeGUIDs || !functions.nvEncInitializeEncoder ||
        !functions.nvEncRegisterResource || !functions.nvEncMapInputResource ||
        !functions.nvEncUnmapInputResource ||
        !functions.nvEncUnregisterResource ||
        !functions.nvEncDestroyEncoder) {
        fprintf(stderr, "NvEncodeAPICreateInstance failed: %d\n", status);
        goto done;
    }
    memset(&open_params, 0, sizeof(open_params));
    open_params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open_params.device = device;
    open_params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    open_params.apiVersion = NVENCAPI_VERSION;
    status = functions.nvEncOpenEncodeSessionEx(&open_params, &encoder);
    if (status != NV_ENC_SUCCESS || !encoder) {
        fprintf(stderr, "NvEncOpenEncodeSessionEx(D3D11) failed: %d\n", status);
        goto done;
    }
    status = functions.nvEncGetEncodeGUIDs(
        encoder, encode_guids, sizeof(encode_guids) / sizeof(encode_guids[0]),
        &guid_count);
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncGetEncodeGUIDs failed: %d\n", status);
        goto done;
    }
    for (uint32_t index = 0; index < guid_count; ++index) {
        h264 |= guid_equal(&encode_guids[index], &h264_guid);
        hevc |= guid_equal(&encode_guids[index], &hevc_guid);
    }

    memset(&initialize_params, 0, sizeof(initialize_params));
    initialize_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initialize_params.encodeGUID = NV_ENC_CODEC_HEVC_GUID;
    initialize_params.presetGUID = NV_ENC_PRESET_P1_GUID;
    initialize_params.encodeWidth = texture_desc.Width;
    initialize_params.encodeHeight = texture_desc.Height;
    initialize_params.darWidth = texture_desc.Width;
    initialize_params.darHeight = texture_desc.Height;
    initialize_params.frameRateNum = 60;
    initialize_params.frameRateDen = 1;
    initialize_params.enableEncodeAsync = 0;
    initialize_params.enablePTD = 1;
    initialize_params.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
    status = functions.nvEncInitializeEncoder(encoder, &initialize_params);
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncInitializeEncoder(HEVC) failed: %d\n", status);
        goto done;
    }

    memset(&register_params, 0, sizeof(register_params));
    register_params.version = NV_ENC_REGISTER_RESOURCE_VER;
    register_params.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    register_params.width = texture_desc.Width;
    register_params.height = texture_desc.Height;
    register_params.resourceToRegister = texture;
    register_params.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
    register_params.bufferUsage = NV_ENC_INPUT_IMAGE;
    status = functions.nvEncRegisterResource(encoder, &register_params);
    if (status != NV_ENC_SUCCESS || !register_params.registeredResource) {
        fprintf(stderr, "NvEncRegisterResource(D3D11) failed: %d\n", status);
        goto done;
    }

    memset(&map_params, 0, sizeof(map_params));
    map_params.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    map_params.registeredResource = register_params.registeredResource;
    status = functions.nvEncMapInputResource(encoder, &map_params);
    if (status != NV_ENC_SUCCESS || !map_params.mappedResource) {
        fprintf(stderr, "NvEncMapInputResource(D3D11) failed: %d\n", status);
        goto done;
    }
    texture_upload = 1;
    status = functions.nvEncUnmapInputResource(
        encoder, map_params.mappedResource);
    map_params.mappedResource = NULL;
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncUnmapInputResource failed: %d\n", status);
        texture_upload = 0;
        goto done;
    }
    status = functions.nvEncUnregisterResource(
        encoder, register_params.registeredResource);
    register_params.registeredResource = NULL;
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncUnregisterResource failed: %d\n", status);
        texture_upload = 0;
        goto done;
    }

    nv12_texture_desc = texture_desc;
    nv12_texture_desc.Format = DXGI_FORMAT_NV12;
    nv12_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    nv12_pixels = HeapAlloc(
        GetProcessHeap(), 0,
        (SIZE_T)nv12_texture_desc.Width *
            (nv12_texture_desc.Height + nv12_texture_desc.Height / 2));
    if (!nv12_pixels) {
        fprintf(stderr, "allocating NV12 probe pixels failed\n");
        goto done;
    }
    memset(nv12_pixels, 0x10,
           (SIZE_T)nv12_texture_desc.Width * nv12_texture_desc.Height);
    memset(nv12_pixels +
               (SIZE_T)nv12_texture_desc.Width * nv12_texture_desc.Height,
           0x80,
           (SIZE_T)nv12_texture_desc.Width * nv12_texture_desc.Height / 2);
    memset(&nv12_initial_data, 0, sizeof(nv12_initial_data));
    nv12_initial_data.pSysMem = nv12_pixels;
    nv12_initial_data.SysMemPitch = nv12_texture_desc.Width;
    nv12_initial_data.SysMemSlicePitch =
        nv12_texture_desc.Width *
        (nv12_texture_desc.Height + nv12_texture_desc.Height / 2);
    hr = ID3D11Device_CreateTexture2D(
        device, &nv12_texture_desc, &nv12_initial_data, &nv12_texture);
    if (FAILED(hr)) {
        fprintf(stderr, "CreateTexture2D(NV12) failed: 0x%08lx\n",
                (unsigned long)hr);
        goto done;
    }

    memset(&nv12_register_params, 0, sizeof(nv12_register_params));
    nv12_register_params.version = NV_ENC_REGISTER_RESOURCE_VER;
    nv12_register_params.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    nv12_register_params.width = nv12_texture_desc.Width;
    nv12_register_params.height = nv12_texture_desc.Height;
    nv12_register_params.resourceToRegister = nv12_texture;
    nv12_register_params.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
    nv12_register_params.bufferUsage = NV_ENC_INPUT_IMAGE;
    status = functions.nvEncRegisterResource(encoder, &nv12_register_params);
    if (status != NV_ENC_SUCCESS ||
        !nv12_register_params.registeredResource) {
        fprintf(stderr, "NvEncRegisterResource(NV12 D3D11) failed: %d\n",
                status);
        goto done;
    }

    memset(&nv12_map_params, 0, sizeof(nv12_map_params));
    nv12_map_params.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    nv12_map_params.registeredResource =
        nv12_register_params.registeredResource;
    status = functions.nvEncMapInputResource(encoder, &nv12_map_params);
    if (status != NV_ENC_SUCCESS || !nv12_map_params.mappedResource) {
        fprintf(stderr, "NvEncMapInputResource(NV12 D3D11) failed: %d\n",
                status);
        goto done;
    }
    nv12_upload = 1;
    status = functions.nvEncUnmapInputResource(
        encoder, nv12_map_params.mappedResource);
    nv12_map_params.mappedResource = NULL;
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncUnmapInputResource(NV12) failed: %d\n", status);
        nv12_upload = 0;
        goto done;
    }
    status = functions.nvEncUnregisterResource(
        encoder, nv12_register_params.registeredResource);
    nv12_register_params.registeredResource = NULL;
    if (status != NV_ENC_SUCCESS) {
        fprintf(stderr, "NvEncUnregisterResource(NV12) failed: %d\n", status);
        nv12_upload = 0;
        goto done;
    }
    printf("NVENC_D3D11_OK\tadapter=%u\tdevice=%u\tluid=%ld\tapi=0x%08x"
           "\th264=%d\thevc=%d\ttexture_upload=%d\tnv12_upload=%d\n",
           adapter_index, adapter_desc.DeviceId,
           (long)adapter_desc.AdapterLuid.LowPart, max_version, h264, hevc,
           texture_upload, nv12_upload);
    result = h264 && hevc && texture_upload && nv12_upload ? 0 : 1;

done:
    if (encoder && map_params.mappedResource &&
        functions.nvEncUnmapInputResource) {
        functions.nvEncUnmapInputResource(encoder, map_params.mappedResource);
    }
    if (encoder && register_params.registeredResource &&
        functions.nvEncUnregisterResource) {
        functions.nvEncUnregisterResource(
            encoder, register_params.registeredResource);
    }
    if (encoder && nv12_map_params.mappedResource &&
        functions.nvEncUnmapInputResource) {
        functions.nvEncUnmapInputResource(
            encoder, nv12_map_params.mappedResource);
    }
    if (encoder && nv12_register_params.registeredResource &&
        functions.nvEncUnregisterResource) {
        functions.nvEncUnregisterResource(
            encoder, nv12_register_params.registeredResource);
    }
    if (encoder && functions.nvEncDestroyEncoder) {
        functions.nvEncDestroyEncoder(encoder);
    }
    if (nvenc_module) {
        FreeLibrary(nvenc_module);
    }
    if (context) {
        ID3D11DeviceContext_Release(context);
    }
    if (render_target) {
        ID3D11RenderTargetView_Release(render_target);
    }
    if (texture) {
        ID3D11Texture2D_Release(texture);
    }
    if (nv12_texture) {
        ID3D11Texture2D_Release(nv12_texture);
    }
    if (nv12_pixels) {
        HeapFree(GetProcessHeap(), 0, nv12_pixels);
    }
    if (device) {
        ID3D11Device_Release(device);
    }
    if (adapter) {
        IDXGIAdapter1_Release(adapter);
    }
    if (factory) {
        IDXGIFactory1_Release(factory);
    }
    return result;
}
