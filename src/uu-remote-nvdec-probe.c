/*
 * SPDX-License-Identifier: 0BSD
 *
 * Query the NVIDIA driver directly instead of guessing NVDEC capabilities
 * from a marketing name. Output is one tab-separated row per CUDA device:
 *
 * index  name  PCI bus id  codecs  maximum coded resolution
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>

struct decoder_cap
{
    int supported;
    unsigned int max_width;
    unsigned int max_height;
};

static void *cuda_handle;
static void *cuvid_handle;
static tcuInit *p_cuInit;
static tcuDeviceGetCount *p_cuDeviceGetCount;
static tcuDeviceGet *p_cuDeviceGet;
static tcuDeviceGetName *p_cuDeviceGetName;
static tcuDeviceGetPCIBusId *p_cuDeviceGetPCIBusId;
static tcuCtxCreate_v2 *p_cuCtxCreate_v2;
static tcuCtxDestroy_v2 *p_cuCtxDestroy_v2;
static tcuvidGetDecoderCaps *p_cuvidGetDecoderCaps;

static int load_symbol(void **target, void *handle, const char *name)
{
    *target = dlsym(handle, name);
    if (!*target)
    {
        fprintf(stderr, "missing symbol %s: %s\n", name, dlerror());
        return 0;
    }
    return 1;
}

static int load_driver(void)
{
    cuda_handle = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!cuda_handle)
    {
        fprintf(stderr, "cannot load libcuda.so.1: %s\n", dlerror());
        return 0;
    }
    cuvid_handle = dlopen("libnvcuvid.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!cuvid_handle)
    {
        fprintf(stderr, "cannot load libnvcuvid.so.1: %s\n", dlerror());
        return 0;
    }

    return load_symbol((void **)&p_cuInit, cuda_handle, "cuInit") &&
           load_symbol((void **)&p_cuDeviceGetCount, cuda_handle, "cuDeviceGetCount") &&
           load_symbol((void **)&p_cuDeviceGet, cuda_handle, "cuDeviceGet") &&
           load_symbol((void **)&p_cuDeviceGetName, cuda_handle, "cuDeviceGetName") &&
           load_symbol((void **)&p_cuDeviceGetPCIBusId, cuda_handle, "cuDeviceGetPCIBusId") &&
           load_symbol((void **)&p_cuCtxCreate_v2, cuda_handle, "cuCtxCreate_v2") &&
           load_symbol((void **)&p_cuCtxDestroy_v2, cuda_handle, "cuCtxDestroy_v2") &&
           load_symbol((void **)&p_cuvidGetDecoderCaps, cuvid_handle, "cuvidGetDecoderCaps");
}

static struct decoder_cap query_cap(cudaVideoCodec codec, unsigned int bit_depth_minus_8,
                                    cudaVideoChromaFormat chroma)
{
    CUVIDDECODECAPS caps;
    struct decoder_cap result = {0, 0, 0};

    memset(&caps, 0, sizeof(caps));
    caps.eCodecType = codec;
    caps.eChromaFormat = chroma;
    caps.nBitDepthMinus8 = bit_depth_minus_8;
    if (p_cuvidGetDecoderCaps(&caps) != CUDA_SUCCESS || !caps.bIsSupported)
        return result;

    result.supported = 1;
    result.max_width = caps.nMaxWidth;
    result.max_height = caps.nMaxHeight;
    return result;
}

static void append_codec(char *buffer, size_t size, const char *codec)
{
    size_t used = strlen(buffer);

    if (used && used + 1 < size)
        buffer[used++] = ',';
    if (used < size)
        snprintf(buffer + used, size - used, "%s", codec);
}

static void sanitize_field(char *text)
{
    for (; *text; text++)
    {
        if (*text == '\t' || *text == '\n' || *text == '\r')
            *text = ' ';
    }
}

int main(void)
{
    int count = 0;
    int index;

    if (!load_driver() || p_cuInit(0) != CUDA_SUCCESS ||
        p_cuDeviceGetCount(&count) != CUDA_SUCCESS)
        return 1;

    for (index = 0; index < count; index++)
    {
        CUdevice device;
        CUcontext context = NULL;
        char name[256] = "NVIDIA GPU";
        char pci_bus_id[32] = "unknown";
        char codecs[160] = "";
        struct decoder_cap h264_8;
        struct decoder_cap h264_10;
        struct decoder_cap hevc_8;
        struct decoder_cap hevc_10;
        struct decoder_cap hevc_444_8;
        struct decoder_cap hevc_444_10;
        unsigned int max_width = 0;
        unsigned int max_height = 0;

        if (p_cuDeviceGet(&device, index) != CUDA_SUCCESS ||
            p_cuCtxCreate_v2(&context, 0, device) != CUDA_SUCCESS)
            continue;

        p_cuDeviceGetName(name, sizeof(name), device);
        p_cuDeviceGetPCIBusId(pci_bus_id, sizeof(pci_bus_id), device);
        sanitize_field(name);
        sanitize_field(pci_bus_id);

        h264_8 = query_cap(cudaVideoCodec_H264, 0, cudaVideoChromaFormat_420);
        h264_10 = query_cap(cudaVideoCodec_H264, 2, cudaVideoChromaFormat_420);
        hevc_8 = query_cap(cudaVideoCodec_HEVC, 0, cudaVideoChromaFormat_420);
        hevc_10 = query_cap(cudaVideoCodec_HEVC, 2, cudaVideoChromaFormat_420);
        hevc_444_8 = query_cap(cudaVideoCodec_HEVC, 0, cudaVideoChromaFormat_444);
        hevc_444_10 = query_cap(cudaVideoCodec_HEVC, 2, cudaVideoChromaFormat_444);

#define RECORD_CAP(cap, label)                                                     \
        do                                                                         \
        {                                                                          \
            if ((cap).supported)                                                   \
            {                                                                      \
                append_codec(codecs, sizeof(codecs), (label));                     \
                if ((cap).max_width * (unsigned long long)(cap).max_height >       \
                    max_width * (unsigned long long)max_height)                    \
                {                                                                  \
                    max_width = (cap).max_width;                                   \
                    max_height = (cap).max_height;                                 \
                }                                                                  \
            }                                                                      \
        } while (0)

        RECORD_CAP(h264_8, "H264-8bit-420");
        RECORD_CAP(h264_10, "H264-10bit-420");
        RECORD_CAP(hevc_8, "H265-8bit-420");
        RECORD_CAP(hevc_10, "H265-10bit-420");
        RECORD_CAP(hevc_444_8, "H265-8bit-444");
        RECORD_CAP(hevc_444_10, "H265-10bit-444");
#undef RECORD_CAP

        printf("%d\t%s\t%s\t%s\t%ux%u\n", index, name, pci_bus_id,
               codecs[0] ? codecs : "none", max_width, max_height);
        p_cuCtxDestroy_v2(context);
    }

    dlclose(cuvid_handle);
    dlclose(cuda_handle);
    return 0;
}
