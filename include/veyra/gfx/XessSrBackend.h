#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>

namespace veyra::gfx {

// Each SR pass owns its XeSS context and temporal history. The graph keeps
// commands serialized, and drains the queue before releasing the context.
class XessSrBackend {
public:
    XessSrBackend();
    ~XessSrBackend();

    bool initialize(ID3D12Device*, uint32_t inputWidth, uint32_t inputHeight,
                    uint32_t outputWidth, uint32_t outputHeight);
    bool evaluate(ID3D12GraphicsCommandList*, ID3D12Resource* color, ID3D12Resource* motion,
                  ID3D12Resource* depth, ID3D12Resource* output, bool reset);
    bool created() const { return p_ != nullptr; }

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::gfx
