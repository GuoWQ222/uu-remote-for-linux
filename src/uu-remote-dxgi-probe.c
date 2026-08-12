#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <stdio.h>

int main(void) {
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    DXGI_ADAPTER_DESC1 description;
    HRESULT result;
    UINT index;
    int found = 0;

    result = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(result)) {
        fprintf(stderr, "CreateDXGIFactory1 failed: %#lx\n", result);
        return 1;
    }
    for (index = 0; ; ++index) {
        result = IDXGIFactory1_EnumAdapters1(factory, index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(result)) {
            fprintf(stderr, "EnumAdapters1 failed: %#lx\n", result);
            break;
        }
        result = IDXGIAdapter1_GetDesc1(adapter, &description);
        if (SUCCEEDED(result) &&
            !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            printf(
                "%u\t%ld\t%u\t%u\n",
                description.DeviceId,
                description.AdapterLuid.LowPart,
                description.VendorId,
                index
            );
            found = 1;
        }
        IDXGIAdapter1_Release(adapter);
        adapter = NULL;
    }
    if (adapter) {
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    return found ? 0 : 1;
}
