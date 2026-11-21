// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <d3d12.h>
#include <memory>

namespace veyra::gfx {

// One D3D12 HQ Scaler and one set of interop resources per SR stage.
class AmfVideoSrBackend {
public:
    AmfVideoSrBackend();
    ~AmfVideoSrBackend();
    static bool available();

    bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue, unsigned inWidth, unsigned inHeight,
                    unsigned outWidth, unsigned outHeight, bool version11);
    bool copyIn(ID3D12GraphicsCommandList* list, ID3D12Resource* input);
    bool evaluate();
    bool copyOut(ID3D12GraphicsCommandList* list, ID3D12Resource* output);
    bool retireSubmitted();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::gfx
