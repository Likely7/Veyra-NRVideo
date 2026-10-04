#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>
#include <string>

#include "veyra/Result.h"

namespace veyra::gfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Rotating command-slot ring (Playbook section 6.2). Each slot owns its
// command allocator, recycled command list, fence value and two timestamp
// query indices. A frame only ever waits on the fence of the slot it is about
// to reuse, never on the frame it just submitted.
//
// Fence ownership: this ring is the ONLY signaler on the shared fence
// timeline handed in via initialize(); the device context waits but never
// signals, so fence values stay strictly monotonic.
class CommandSlotRing {
public:
    CommandSlotRing() = default;
    ~CommandSlotRing();

    CommandSlotRing(const CommandSlotRing&) = delete;
    CommandSlotRing& operator=(const CommandSlotRing&) = delete;

    // `fenceEvent` is shared with the owning device context; waits use
    // SetEventOnCompletion + WaitForSingleObject on that event.
    bool initialize(ID3D12Device* device,
                    ID3D12CommandQueue* queue,
                    ID3D12Fence* fence,
                    HANDLE fenceEvent,
                    uint32_t slotCount,
                    Status& status);
    void shutdown();

    uint32_t slotCount() const { return slotCount_; }
    bool initialized() const { return initialized_; }
    ID3D12CommandQueue* queue() const {return queue_;}

    // Waits for this slot's outstanding fence value (if any), then resets the
    // allocator and command list. Returns the recycled list ready for record.
    ID3D12GraphicsCommandList* acquire(uint32_t slot, Status& status);
    // Shared cursor for graph and presenter: independent cursors can select
    // the allocator just submitted by the other consumer and serialize frames.
    ID3D12GraphicsCommandList* acquireNext(uint32_t& slot,Status& status) {
        if(!slotCount_){status=Status::InvalidArgument;return nullptr;}
        slot=nextSlot_++%slotCount_;return acquire(slot,status);
    }

    // Closes, submits and signals the slot; advances the fence timeline.
    bool submitAndSignal(uint32_t slot);
    bool discardRecording(); // Error rollback: never execute a partially recorded list.
    void tag(uint32_t slot,const char* label){slots_.at(slot).label=label;}

    // The fence value most recently signaled by this ring. Callers that need
    // a GPU-side dependency on the just-submitted work (e.g. NVOF input
    // fence points) use this value.
    uint64_t lastSignaledValue() const { return nextFenceValue_ - 1; }

    // Blocks until every slot's work completed.
    bool waitIdle();

    // Enqueues a FRESH fence signal after everything previously submitted
    // (including the last Present, which waitIdle does not cover) and waits
    // for it. Call before swapchain release, or the D3D12 debug layer
    // reports final-release with GPU operations in-flight (id=921 -> 0x87D).
    bool drainQueue();

    // Phase 0 skeleton proof: for each slot, acquire (wait+reset), record a
    // real begin/end timestamp query pair, submit, signal, then wait idle.
    bool exerciseAll();

    // Two timestamp query indices per slot (begin at 2*slot, end at 2*slot+1).
    uint32_t timestampQueryIndex(uint32_t slot, bool end) const;

    // Query heap for D3D12_QUERY_TYPE_TIMESTAMP entries (two per slot).
    ID3D12QueryHeap* timestampHeap() const { return timestampHeap_.Get(); }
    uint64_t cpuWaitCount() const { return cpuWaitCount_; }
    double cpuWaitMilliseconds() const { return cpuWaitMilliseconds_; }
    uint64_t submitCount() const { return submitCount_; }
    uint32_t inFlightCount() const;
    const std::vector<double>& gpuCommandTimesMs() const { return gpuCommandTimesMs_; }

private:
    struct Slot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        uint64_t fenceValue = 0; // 0 means "never submitted"
        bool timed = false,recording=false;
        std::string label;
    };

    bool initialized_ = false;
    uint32_t slotCount_ = 0;
    uint32_t nextSlot_ = 0;
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE fenceEvent_ = nullptr;
    uint64_t nextFenceValue_ = 1;
    ComPtr<ID3D12QueryHeap> timestampHeap_;
    std::vector<Slot> slots_;
    ComPtr<ID3D12Resource> timingReadback_;
    uint64_t timestampFrequency_ = 0, cpuWaitCount_ = 0, submitCount_ = 0;
    double cpuWaitMilliseconds_ = 0;
    std::vector<double> gpuCommandTimesMs_; // 16384-entry ring once full (sweep E6)
    size_t gpuCommandTimesCursor_=0;
};

} // namespace veyra::gfx
