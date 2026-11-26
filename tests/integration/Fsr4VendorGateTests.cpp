// FSR4 backend vendor gate regression: on a non-Intel adapter the backend must
// refuse initialize() before loading any AMD DLL or installing any hook, and
// repeated initialize/release cycles must stay clean. Runs on whatever adapter
// the host has; on an Intel host the gate test is skipped (the positive path
// needs the user-supplied DLL and is covered by tools/fsr4_driver_probe).
#include "veyra/gfx/Fsr4SrBackend.h"
#include "veyra/gfx/FsrSrBackend.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

int main() {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        std::printf("FAIL factory creation\n");
        return 1;
    }
    ComPtr<IDXGIAdapter1> adapter;
    UINT vendor = 0;
    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        adapter = candidate;
        vendor = desc.VendorId;
        break;
    }
    if (!adapter) {
        std::printf("SKIP no hardware adapter\n");
        return 0;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
        std::printf("SKIP D3D12 device creation failed\n");
        return 0;
    }
    if (vendor == 0x8086) {
        std::printf("SKIP Intel host: positive path needs the user DLL (fsr4_driver_probe)\n");
        return 0;
    }
    // Non-Intel: initialize must refuse, repeatedly, without side effects.
    for (int cycle = 0; cycle < 3; ++cycle) {
        veyra::gfx::Fsr4SrBackend backend;
        if (backend.initialize(device.Get(), 1920, 1080, 2560, 1440, false)) {
            std::printf("FAIL vendor gate accepted non-Intel adapter (cycle %d)\n", cycle);
            return 1;
        }
        if (backend.created()) {
            std::printf("FAIL backend reports created after refusal (cycle %d)\n", cycle);
            return 1;
        }
        backend.release();
    }
    // The AMD driver DLL must not be loaded by the refused initialize.
    if (GetModuleHandleW(L"amdxcffx64.dll") != nullptr) {
        std::printf("FAIL amdxcffx64.dll loaded despite vendor refusal\n");
        return 1;
    }
    // FSR3 must stay usable after the FSR4 refusals: its runtime directory is
    // shared, and a leaked hook or DLL state would break the existing path.
    {
        veyra::gfx::FsrSrBackend fsr3;
        const bool fsr3Up = fsr3.initialize(device.Get(), 1920, 1080, 2560, 1440, false);
        // The FSR3 runtime may legitimately be absent on a dev host; only a
        // present runtime that fails to initialize is a regression.
        if (!fsr3Up && veyra::gfx::Fsr4SrBackend::runtimeAvailable()) {
            std::printf("FAIL FSR3 initialize failed after FSR4 refusals while runtime present\n");
            return 1;
        }
        fsr3.release();
        std::printf("FSR3 after FSR4 refusal: %s\n", fsr3Up ? "initialized+released clean" : "runtime absent (skip)");
    }
    std::printf("PASS FSR4 vendor gate: non-Intel refused, no DLL loaded, 3 cycles clean\n");
    return 0;
}
