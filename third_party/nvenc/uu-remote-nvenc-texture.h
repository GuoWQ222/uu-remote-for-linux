/* Safe D3D11 resource validation shared by the bridge and its unit test. */

static BOOL uu_d3d11_buffer_layout(
    NV_ENC_BUFFER_FORMAT format,
    DXGI_FORMAT dxgi_format,
    uint32_t width,
    uint32_t height,
    uint32_t *row_bytes,
    uint32_t *rows)
{
    uint64_t total_rows;

    switch (format)
    {
        case NV_ENC_BUFFER_FORMAT_NV12:
            if (dxgi_format != DXGI_FORMAT_NV12)
                return FALSE;
            total_rows = (uint64_t)height + ((uint64_t)height + 1u) / 2u;
            if (total_rows > UINT32_MAX)
                return FALSE;
            *row_bytes = width;
            *rows = (uint32_t)total_rows;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_NV16:
            if (dxgi_format != DXGI_FORMAT_P208 || height > UINT32_MAX / 2)
                return FALSE;
            *row_bytes = width;
            *rows = height * 2;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
            if (dxgi_format != DXGI_FORMAT_P010 || width > UINT32_MAX / 2)
                return FALSE;
            total_rows = (uint64_t)height + ((uint64_t)height + 1u) / 2u;
            if (total_rows > UINT32_MAX)
                return FALSE;
            *row_bytes = width * 2;
            *rows = (uint32_t)total_rows;
            return TRUE;
        case NV_ENC_BUFFER_FORMAT_ARGB:
            if (dxgi_format != DXGI_FORMAT_B8G8R8A8_UNORM &&
                dxgi_format != DXGI_FORMAT_B8G8R8A8_TYPELESS &&
                dxgi_format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                return FALSE;
            break;
        case NV_ENC_BUFFER_FORMAT_ABGR:
            if (dxgi_format != DXGI_FORMAT_R8G8B8A8_UNORM &&
                dxgi_format != DXGI_FORMAT_R8G8B8A8_TYPELESS &&
                dxgi_format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                return FALSE;
            break;
        case NV_ENC_BUFFER_FORMAT_ARGB10:
        case NV_ENC_BUFFER_FORMAT_ABGR10:
            if (dxgi_format != DXGI_FORMAT_R10G10B10A2_UNORM &&
                dxgi_format != DXGI_FORMAT_R10G10B10A2_TYPELESS)
                return FALSE;
            break;
        case NV_ENC_BUFFER_FORMAT_AYUV:
            if (dxgi_format != DXGI_FORMAT_AYUV)
                return FALSE;
            break;
        default:
            return FALSE;
    }
    if (width > UINT32_MAX / 4)
        return FALSE;
    *row_bytes = width * 4;
    *rows = height;
    return TRUE;
}

static BOOL uu_d3d11_subresource_dimensions(
    const D3D11_TEXTURE2D_DESC *description,
    uint32_t subresource,
    uint32_t *width,
    uint32_t *height)
{
    uint32_t mip;
    uint64_t subresource_count;

    if (!description || !description->MipLevels || !description->ArraySize ||
        description->SampleDesc.Count != 1)
        return FALSE;
    subresource_count = (uint64_t)description->MipLevels *
                        (uint64_t)description->ArraySize;
    if ((uint64_t)subresource >= subresource_count)
        return FALSE;
    mip = subresource % description->MipLevels;
    *width = mip >= 32u ? 1u : description->Width >> mip;
    *height = mip >= 32u ? 1u : description->Height >> mip;
    if (!*width)
        *width = 1u;
    if (!*height)
        *height = 1u;
    return TRUE;
}

/*
 * A map-time upload is sufficient for the first encode.  If the caller keeps
 * the NVENC input mapped, every later encode must refresh the CUDA copy from
 * the mutable D3D11 texture.  Returning TRUE means the encode path owns that
 * refresh; remapping sets the flag back to TRUE and avoids a duplicate copy.
 */
static BOOL uu_nvenc_encode_upload_required(BOOL *upload_fresh)
{
    if (!upload_fresh || !*upload_fresh)
        return TRUE;
    *upload_fresh = FALSE;
    return FALSE;
}
