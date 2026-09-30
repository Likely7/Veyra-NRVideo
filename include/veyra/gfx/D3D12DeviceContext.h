#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "veyra/Result.h"

namespace veyra::gfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

struct AdapterInfo {
    std::wstring description;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;               // PCI device id, e.g. 0x2684 = AD102
    std::string vendorIdHex;             // "0x10DE"
    uint64_t luid = 0;                   // DXGI_ADAPTER_DESC1 LUID (High<<32 | Low)
    std::string luidString;              // "0xHigh:0xLow"
    uint64_t dedicatedVideoMemoryBytes = 0;
    bool isNvidia = false;
    bool isSoftware = false;
    std::wstring driverVersion;          // registry DisplayVersion when found
    std::wstring driverVersionSource;    // "registry" or "dxgi-raw"
};

struct DeviceContextDesc {
    bool enableDebugLayer = false;
    uint32_t commandSlotCount = 4;
    uint32_t requiredVendorId = 0; // 0: highest-performance hardware adapter
    uint64_t requiredLuid = 0; // 0: no adapter identity constraint
};

// One DXGI adapter + one ID3D12Device + one direct command queue + one fence
// timeline with a rotating command-slot ring (Playbook sections 6.1/6.2).
class D3D12DeviceContext {
public:
    D3D12DeviceContext() = default;
    ~D3D12DeviceContext();

    D3D12DeviceContext(const D3D12DeviceContext&) = delete;
    D3D12DeviceContext& operator=(const D3D12DeviceContext&) = delete;

    // Initialization order: optional debug layer -> factory -> NVIDIA adapter
    // (vendor 0x10DE, non-software) -> device at >= 12_0 -> direct queue ->
    // fence/event -> command slot ring. Every step logs its real result.
    bool initialize(const DeviceContextDesc& desc, Status& status);
    void shutdown(); // reverse-order teardown

    bool initialized() const { return initialized_; }
    bool debugLayerEnabled() const { return debugLayerEnabled_; }

    ID3D12Device* device() const { return device_.Get(); }
    ID3D12CommandQueue* directQueue() const { return queue_.Get(); }
    ID3D12Fence* fence() const { return fence_.Get(); }
    HANDLE fenceEvent() const { return fenceEvent_; }
    uint64_t fenceCompletedValue() const { return fence_->GetCompletedValue(); }

    // Fence-timeline ownership: CommandSlotRing is the ONLY component allowed
    // to Signal this fence. The device context provides the fence/event and
    // waits; a second independent counter here previously risked duplicate or
    // non-monotonic values once Evaluate loops arrive (Phase 1 Reviewer P2).
    const AdapterInfo& adapter() const { return adapterInfo_; }
    const std::string& featureLevelString() const { return featureLevel_; }
    uint32_t commandSlotCount() const { return commandSlotCount_; }
    bool videoMemoryInfo(uint64_t& budget,uint64_t& usage) const;

    // CPU wait until `value` completes; returns false on wait timeout/failure.
    bool waitForFenceValue(uint64_t value, uint32_t timeoutMs = 10000);

    // GetDeviceRemovedReason; returns true when the device is still alive.
    bool checkDeviceAlive(uint32_t& removedReason) const;
    // Bounded DRED snapshot on a failure path, never a normal per-frame readback.
    bool reportDeviceFailure(std::string_view operation, uint64_t requestedFence=0) const;

    // Exercises the full slot ring: acquire (wait+reset), empty command list
    // with timestamp begin/end markers, execute, signal, wait idle. Phase 0
    // skeleton proof that allocator/list/fence plumbing works end to end.
    bool exerciseSlotRing();

private:
    bool initialized_ = false;
    bool debugLayerEnabled_ = false;
    uint32_t commandSlotCount_ = 0;

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;

    AdapterInfo adapterInfo_{};
    std::string featureLevel_;
};

} // namespace veyra::gfx
