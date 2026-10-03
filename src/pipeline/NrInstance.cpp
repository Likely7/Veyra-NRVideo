#include "veyra/pipeline/NrInstance.h"

namespace veyra::pipeline {
bool NrInstance::create(ID3D12Device* device, uint32_t width, uint32_t height,
                        uint32_t fullWidth, uint32_t fullHeight,
                        ID3D12Resource* zeroMotion, ID3D12Resource* zeroDepth,
                        ID3D12Resource* borrowedInput, ID3D12Resource* borrowedMotion) {
    if (device == nullptr || width == 0 || height == 0 || zeroMotion == nullptr || zeroDepth == nullptr) return false;
    if (fullWidth == 0 || fullHeight == 0) return false;
    close();
    zeroMotion_ = zeroMotion;
    zeroDepth_ = zeroDepth;
    width_ = width;
    height_ = height;
    fullWidth_ = fullWidth;
    fullHeight_ = fullHeight;
    // Encode proxy and neural output are the 8-bit pair Feature 18 consumes;
    // the decoded output and residual scratch stay linear FP16. Every layer gets
    // its own set: sharing them would make the layers' histories interfere.
    // A borrowed input keeps the graph's original aliasing behaviour (no copy,
    // no downsample); otherwise the layer owns an NR-extent input.
    inputIsBorrowed_ = borrowedInput != nullptr;
    if (borrowedInput != nullptr) input_ = borrowedInput;   // ComPtr holds its own reference to the graph's input.
    else input_ = makeTexture(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    proxy_ = makeTexture(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    neural_ = makeTexture(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    finalRgba_ = makeTexture(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    residual_ = makeTexture(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    // This layer's full-extent output; the last layer's is owned by the graph,
    // so an unused allocation there is harmless but wasteful, hence the caller
    // telling us the extent rather than us guessing the role.
    outputFull_ = makeTexture(device, fullWidth, fullHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    if(borrowedMotion)motion_=borrowedMotion;
    else motion_=makeTexture(device,width,height,DXGI_FORMAT_R16G16_FLOAT,true);
    if (!input_ || !proxy_ || !neural_ || !finalRgba_ || !residual_ || !outputFull_ || !motion_) { close(); return false; }
    historyValid = false;
    return true;
}

void NrInstance::close() {
    amd.reset(); // runtime releases its references before the layer textures
    temporal_.close();
    // Both owned and borrowed inputs are ComPtrs. Release this reference in
    // either case; the graph retains its own reference to a borrowed input.
    input_.Reset();
    inputIsBorrowed_ = false;
    proxy_.Reset();
    neural_.Reset();
    finalRgba_.Reset();
    residual_.Reset();
    outputFull_.Reset();
    motion_.Reset();
    baseFull_ = nullptr;
    fullTarget_ = nullptr;
    zeroMotion_ = zeroDepth_ = nullptr;
    handle_ = nullptr;
    parameters_ = nullptr;
    width_ = height_ = fullWidth_ = fullHeight_ = 0;
    inputIsBorrowed_ = false;
    historyValid = false;
}
}
