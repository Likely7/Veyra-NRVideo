#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <d3d12.h>
#include <wrl/client.h>
namespace veyra::pipeline {
// Fence values are ordered only within the same fence object. Never compare
// a graph fence value with one produced by an independent GPU queue.
inline bool fenceComplete(ID3D12Fence* fence,uint64_t value) {
    if(!value)return true;
    if(!fence)return false;
    const auto completed=fence->GetCompletedValue();
    return completed!=UINT64_MAX&&completed>=value;
}
enum class FrameKind { Real, Generated, Hold };
enum class GenerationValidity { NotApplicable, Pending, Valid, Disabled, Failed };
struct FrameIdentity {
    uint64_t epoch=0,settingsRevision=0,sourceFrameId=0;
    bool operator==(const FrameIdentity&) const = default;
};
// A lease keeps the resource alive; the producer additionally waits for the
// consumer fence before overwriting a pool slot. COM lifetime alone is insufficient.
struct FrameLease {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture,sourceReference,baseReference;
    Microsoft::WRL::ComPtr<ID3D12Fence> readyFenceObject;
    uint64_t readyFence=0,consumerFence=0;
    uint32_t slot=0;
    bool referencesValid=false;
    bool ready()const{return fenceComplete(readyFenceObject.Get(),readyFence);}
};
struct BatchFrame {
    FrameIdentity identity;
    FrameKind kind=FrameKind::Real;
    GenerationValidity validity=GenerationValidity::NotApplicable;
    uint32_t subframe=0;
    int64_t pts100ns=0;
    std::shared_ptr<FrameLease> lease;
};
struct FrameBatch {
    static constexpr uint32_t Capacity=8;
    uint64_t batchId=0;
    FrameIdentity identity;
    int64_t a100ns=0,b100ns=0;
    // 8X multi-frame generation needs one real frame plus up to seven generated
    // frames in a single batch.
    std::array<BatchFrame,Capacity> frames{};
    uint32_t count=0;
    static int64_t interpolate(int64_t a,int64_t b,uint32_t j,uint32_t n) {
        // The application accepts at most one second between continuous endpoints.
        if(n<2||n>Capacity||j==0||j>=n||b<=a||a>INT64_MAX-10000000||b>a+10000000)
            throw std::invalid_argument("invalid interpolation interval");
        const int64_t d=b-a;return a+(d/n)*j+((d%n)*j+n/2)/n;
    }
    void append(BatchFrame frame) {
        if(count>=frames.size()||frame.identity!=identity||(count&&frame.pts100ns<=frames[count-1].pts100ns))
            throw std::invalid_argument("cross-revision or unordered frame batch");
        frames[count++]=std::move(frame);
    }
};
}
