#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TRUE 1
#define FALSE 0
#define BOOL int

typedef enum {
    DXGI_FORMAT_R10G10B10A2_TYPELESS,
    DXGI_FORMAT_R10G10B10A2_UNORM,
    DXGI_FORMAT_R8G8B8A8_TYPELESS,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_B8G8R8A8_TYPELESS,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
    DXGI_FORMAT_AYUV,
    DXGI_FORMAT_NV12,
    DXGI_FORMAT_P010,
    DXGI_FORMAT_P208
} DXGI_FORMAT;

typedef enum {
    NV_ENC_BUFFER_FORMAT_NV12,
    NV_ENC_BUFFER_FORMAT_NV16,
    NV_ENC_BUFFER_FORMAT_YUV420_10BIT,
    NV_ENC_BUFFER_FORMAT_ARGB,
    NV_ENC_BUFFER_FORMAT_ARGB10,
    NV_ENC_BUFFER_FORMAT_AYUV,
    NV_ENC_BUFFER_FORMAT_ABGR,
    NV_ENC_BUFFER_FORMAT_ABGR10,
    NV_ENC_BUFFER_FORMAT_P210,
    NV_ENC_BUFFER_FORMAT_YUV444
} NV_ENC_BUFFER_FORMAT;

typedef struct {
    uint32_t Count;
    uint32_t Quality;
} sample_description;

typedef struct {
    uint32_t Width;
    uint32_t Height;
    uint32_t MipLevels;
    uint32_t ArraySize;
    DXGI_FORMAT Format;
    sample_description SampleDesc;
} D3D11_TEXTURE2D_DESC;

#include "../third_party/nvenc/uu-remote-nvenc-texture.h"

static void require(BOOL condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(void)
{
    D3D11_TEXTURE2D_DESC description = {0};
    uint32_t row_bytes;
    uint32_t rows;
    uint32_t width;
    uint32_t height;
    BOOL upload_fresh;

    require(uu_d3d11_buffer_layout(
                NV_ENC_BUFFER_FORMAT_ARGB,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                1280, 720, &row_bytes, &rows) &&
            row_bytes == 5120 && rows == 720,
            "valid ARGB texture was rejected");
    require(!uu_d3d11_buffer_layout(
                NV_ENC_BUFFER_FORMAT_ARGB,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                1280, 720, &row_bytes, &rows),
            "ARGB accepted an ABGR texture layout");
    require(uu_d3d11_buffer_layout(
                NV_ENC_BUFFER_FORMAT_NV12,
                DXGI_FORMAT_NV12,
                1280, 721, &row_bytes, &rows) &&
            row_bytes == 1280 && rows == 1082,
            "valid NV12 texture was rejected");
    require(!uu_d3d11_buffer_layout(
                NV_ENC_BUFFER_FORMAT_NV12,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                1280, 720, &row_bytes, &rows),
            "NV12 accepted a packed BGRA texture");
    require(!uu_d3d11_buffer_layout(
                NV_ENC_BUFFER_FORMAT_YUV444,
                DXGI_FORMAT_NV12,
                1280, 720, &row_bytes, &rows),
            "unsupported planar YUV444 DirectX texture was accepted");

    description.Width = 1280;
    description.Height = 720;
    description.MipLevels = 3;
    description.ArraySize = 2;
    description.SampleDesc.Count = 1;
    require(uu_d3d11_subresource_dimensions(
                &description, 4, &width, &height) &&
            width == 640 && height == 360,
            "array/mip subresource dimensions are incorrect");
    require(!uu_d3d11_subresource_dimensions(
                &description, 6, &width, &height),
            "out-of-range subresource was accepted");
    description.SampleDesc.Count = 4;
    require(!uu_d3d11_subresource_dimensions(
                &description, 0, &width, &height),
            "multisampled texture was accepted");

    upload_fresh = TRUE;
    require(!uu_nvenc_encode_upload_required(&upload_fresh) && !upload_fresh,
            "first encode after map requested a duplicate texture upload");
    require(uu_nvenc_encode_upload_required(&upload_fresh),
            "persistently mapped texture was not refreshed before re-encode");
    upload_fresh = TRUE;
    require(!uu_nvenc_encode_upload_required(&upload_fresh),
            "remapped texture did not publish its fresh upload");
    puts("PASS NVENC D3D11 texture validation");
    return 0;
}
