#include "veyra/gfx/CommandSlotRing.h"

#include <chrono>
#include <format>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"

namespace veyra::gfx {

CommandSlotRing::~CommandSlotRing()
{
    shutdown();
}

bool CommandSlotRing::initialize(ID3D12Device* device,
                                 ID3D12CommandQueue* queue,
                                 ID3D12Fence* fence,
                                 HANDLE fenceEvent,
                                 uint32_t slotCount,
                                 Status& status)
{
    if (initialized_) {
        shutdown();
    }
    device_ = device;
    queue_ = queue;
    fence_ = fence;
    fenceEvent_ = fenceEvent;
    slotCount_ = slotCount;
    nextSlot_ = 0;
    cpuWaitCount_=0;cpuWaitMilliseconds_=0;submitCount_=0;

    if (device_ == nullptr || queue_ == nullptr || fence_ == nullptr || fenceEvent_ == nullptr || slotCount_ == 0) {
        status = Status::InvalidArgument;
        veyra::log::error("gfx", "slot-ring: invalid initialize arguments");
        return false;
    }

    D3D12_QUERY_HEAP_DESC timestampHeapDesc{};
    timestampHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    timestampHeapDesc.Count = slotCount_ * 2; // begin+end pair per slot
    timestampHeapDesc.NodeMask = 0;
    const HRESULT heapResult = device_->CreateQueryHeap(&timestampHeapDesc, IID_PPV_ARGS(&timestampHeap_));
    if (FAILED(heapResult)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("slot-ring: CreateQueryHeap failed hr={}", veyra::hresultString(heapResult)));
        return false;
    }
    UINT64 frequency = 0;
    const HRESULT freqResult = queue_->GetTimestampFrequency(&frequency);
    if(FAILED(freqResult)||frequency==0)return false;
    timestampFrequency_=frequency;
    D3D12_HEAP_PROPERTIES rh{};rh.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=slotCount_*2*sizeof(uint64_t);rb.Height=1;rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device_->CreateCommittedResource(&rh,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timingReadback_))))return false;
    veyra::log::info("gfx", std::format("slot-ring: timestamp heap created slots={} pairs={} frequencyHz={} freqQuery={}",
        slotCount_, timestampHeapDesc.Count, frequency, veyra::hresultString(freqResult)));

    slots_.resize(slotCount_);
    for (uint32_t i = 0; i < slotCount_; ++i) {
        HRESULT result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slots_[i].allocator));
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("slot-ring: CreateCommandAllocator slot={} failed hr={}", i, veyra::hresultString(result)));
            return false;
        }
        result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slots_[i].allocator.Get(), nullptr, IID_PPV_ARGS(&slots_[i].list));
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("slot-ring: CreateCommandList slot={} failed hr={}", i, veyra::hresultString(result)));
            return false;
        }
        // Command lists are created in the recording state; close them so
        // acquire() can reset them deterministically.
        result = slots_[i].list->Close();
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("slot-ring: initial Close slot={} failed hr={}", i, veyra::hresultString(result)));
            return false;
        }
        slots_[i].fenceValue = 0;
    }

    initialized_ = true;
    veyra::log::info("gfx", std::format("slot-ring: initialized slots={}", slotCount_));
    return true;
}

void CommandSlotRing::shutdown()
{
    if (!initialized_) {
        return;
    }
    (void)waitIdle();
    slots_.clear();
    timestampHeap_.Reset();
    timingReadback_.Reset();
    cpuWaitCount_=0;cpuWaitMilliseconds_=0;submitCount_=0;
    initialized_ = false;
    veyra::log::info("gfx", "slot-ring: shutdown complete");
}

ID3D12GraphicsCommandList* CommandSlotRing::acquire(uint32_t slot, Status& status)
{
    if (!initialized_ || slot >= slotCount_) {
        status = Status::InvalidArgument;
        veyra::log::error("gfx", std::format("slot-ring: acquire slot={} out of range", slot));
        return nullptr;
    }
    Slot& target = slots_[slot];
    const uint64_t completed = fence_->GetCompletedValue();
    if(completed==UINT64_MAX){status=Status::DeviceFailure;veyra::log::error("gfx",std::format("slot-ring: removed-device fence sentinel slot={} removedReason=0x{:X}",slot,unsigned(device_->GetDeviceRemovedReason())));return nullptr;}
    if (target.fenceValue != 0) {
        if (completed < target.fenceValue) {
            const HRESULT waitResult = fence_->SetEventOnCompletion(target.fenceValue, fenceEvent_);
            if (FAILED(waitResult)) {
                status = Status::DeviceFailure;
                veyra::log::error("gfx", std::format("slot-ring: SetEventOnCompletion slot={} failed hr={}", slot, veyra::hresultString(waitResult)));
                return nullptr;
            }
            ++cpuWaitCount_;
            const auto waitStart=std::chrono::steady_clock::now();
            const DWORD wait = WaitForSingleObject(fenceEvent_, 10000);
            cpuWaitMilliseconds_+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-waitStart).count();
            if (wait != WAIT_OBJECT_0) {
                status = Status::DeviceFailure;
                veyra::log::error("gfx", std::format("slot-ring: fence wait slot={} waitResult={} fenceValue={}", slot, wait, target.fenceValue));
                return nullptr;
            }
        }
    }

    static const bool profileSlots=GetEnvironmentVariableW(L"VEYRA_PROFILE_SLOTS",nullptr,0)>0;
    if(target.timed && timingReadback_ && (profileSlots||veyra::log::verboseFrameLogs())) {
        uint64_t* data=nullptr;D3D12_RANGE range{slot*2*sizeof(uint64_t),(slot*2+2)*sizeof(uint64_t)};
        if(SUCCEEDED(timingReadback_->Map(0,&range,reinterpret_cast<void**>(&data)))) {
            const uint64_t begin=data[slot*2],end=data[slot*2+1];
            if(end>=begin){const double ms=double(end-begin)*1000.0/timestampFrequency_;if(gpuCommandTimesMs_.size()<16384)gpuCommandTimesMs_.push_back(ms);else gpuCommandTimesMs_[gpuCommandTimesCursor_++%16384]=ms;if(!target.label.empty()&&veyra::log::verboseFrameLogs())veyra::log::info("gpu-profile",std::format("{} {:.4f} ms",target.label,ms));}
            D3D12_RANGE empty{0,0};timingReadback_->Unmap(0,&empty);
        }target.timed=false;target.label.clear();
    }
    target.timed=false;
    HRESULT result = target.allocator->Reset();
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("slot-ring: allocator reset slot={} failed hr={}", slot, veyra::hresultString(result)));
        return nullptr;
    }
    result = target.list->Reset(target.allocator.Get(), nullptr);
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("slot-ring: list reset slot={} failed hr={}", slot, veyra::hresultString(result)));
        return nullptr;
    }
    target.recording=true;target.list->EndQuery(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2);
    return target.list.Get();
}

bool CommandSlotRing::submitAndSignal(uint32_t slot)
{
    if (!initialized_ || slot >= slotCount_) {
        veyra::log::error("gfx", std::format("slot-ring: submit slot={} out of range", slot));
        return false;
    }
    Slot& target = slots_[slot];

    target.list->EndQuery(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2+1);
    target.list->ResolveQueryData(timestampHeap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*2,2,timingReadback_.Get(),slot*2*sizeof(uint64_t));
    target.timed=true;
    const HRESULT closeResult = target.list->Close();target.recording=false;
    if (FAILED(closeResult)) {
        veyra::log::error("gfx", std::format("slot-ring: close slot={} failed hr={}", slot, veyra::hresultString(closeResult)));
        return false;
    }
    ID3D12CommandList* lists[] = { target.list.Get() };
    queue_->ExecuteCommandLists(1, lists);

    const uint64_t value = nextFenceValue_++;
    const HRESULT signalResult = queue_->Signal(fence_, value);
    if (FAILED(signalResult)) {
        veyra::log::error("gfx", std::format("slot-ring: signal slot={} failed hr={}", slot, veyra::hresultString(signalResult)));
        return false;
    }
    target.fenceValue = value;
    ++submitCount_;
    return true;
}

uint32_t CommandSlotRing::inFlightCount() const
{
    if (!initialized_ || fence_ == nullptr) {
        return 0;
    }
    const uint64_t completed = fence_->GetCompletedValue();
    uint32_t count = 0;
    for (const Slot& slot : slots_) {
        if (slot.fenceValue != 0 && slot.fenceValue > completed) {
            ++count;
        }
    }
    return count;
}

bool CommandSlotRing::discardRecording(){
    bool ok=true;for(uint32_t i=0;i<slots_.size();++i){auto& s=slots_[i];if(s.recording){const HRESULT hr=s.list->Close();s.recording=false;s.timed=false;s.label.clear();veyra::log::info("gfx",std::format("discard unsubmitted command list slot={} hr=0x{:X}",i,unsigned(hr)));if(FAILED(hr)){s.list.Reset();const HRESULT created=device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list));if(FAILED(created)||FAILED(s.list->Close()))ok=false;}}}return ok;
}
bool CommandSlotRing::drainQueue()
{
    // s10: waitIdle() only waits for ALREADY-signaled values. The last
    // Present is submitted after the final signal, so its queue operation is
    // not covered by waitIdle. This enqueues a FRESH signal (queue-ordered
    // after everything previously submitted, including the last Present) and
    // waits for it. Without this, the D3D12 debug layer reports
    // ID3D12Resource final-release with GPU operations in-flight at
    // swapchain release (message id=921, RaiseException 0x87D via
    // KERNELBASE; observed t10-L1 2026-09-04).
    if (!initialized_) {
        return true;
    }
    const uint64_t value = nextFenceValue_++;
    const HRESULT signalResult = queue_->Signal(fence_, value);
    if (FAILED(signalResult)) {
        veyra::log::error("gfx", std::format("slot-ring: drainQueue signal failed hr={}", veyra::hresultString(signalResult)));
        return false;
    }
    const HRESULT setResult = fence_->SetEventOnCompletion(value, fenceEvent_);
    if (FAILED(setResult)) {
        veyra::log::error("gfx", std::format("slot-ring: drainQueue SetEventOnCompletion failed hr={}", veyra::hresultString(setResult)));
        return false;
    }
    const DWORD wait = WaitForSingleObject(fenceEvent_, 30000);
    if (wait != WAIT_OBJECT_0) {
        veyra::log::error("gfx", std::format("slot-ring: drainQueue wait={} fenceValue={}", wait, value));
        return false;
    }
    veyra::log::info("gfx", std::format("slot-ring: drainQueue completed fenceValue={} (covers post-Present queue ops)", value));
    return true;
}

bool CommandSlotRing::waitIdle()
{
    if (!initialized_) {
        return true;
    }
    uint64_t highest = 0;
    for (const Slot& slot : slots_) {
        if (slot.fenceValue > highest) {
            highest = slot.fenceValue;
        }
    }
    if (highest == 0) {
        return true;
    }
    const auto completed=fence_->GetCompletedValue();
    if(completed==UINT64_MAX){veyra::log::error("gfx",std::format("slot-ring: drain refused removed-device fence sentinel removedReason=0x{:X}",unsigned(device_->GetDeviceRemovedReason())));return false;}
    if (completed >= highest) {
        return true;
    }
    const HRESULT setResult = fence_->SetEventOnCompletion(highest, fenceEvent_);
    if (FAILED(setResult)) {
        veyra::log::error("gfx", std::format("slot-ring: waitIdle SetEventOnCompletion failed hr={}", veyra::hresultString(setResult)));
        return false;
    }
    const DWORD wait = WaitForSingleObject(fenceEvent_, 30000);
    if (wait != WAIT_OBJECT_0) {
        veyra::log::error("gfx", std::format("slot-ring: waitIdle wait={} fenceValue={}", wait, highest));
        return false;
    }
    return fence_->GetCompletedValue()!=UINT64_MAX&&SUCCEEDED(device_->GetDeviceRemovedReason());
}

uint32_t CommandSlotRing::timestampQueryIndex(uint32_t slot, bool end) const
{
    return slot * 2 + (end ? 1u : 0u);
}

bool CommandSlotRing::exerciseAll()
{
    if (!initialized_) {
        veyra::log::error("gfx", "slot-ring: exerciseAll before initialize");
        return false;
    }
    for (uint32_t slot = 0; slot < slotCount_; ++slot) {
        Status status = Status::Ok;
        ID3D12GraphicsCommandList* list = acquire(slot, status);
        if (list == nullptr) {
            return false;
        }
        list->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryIndex(slot, false));
        list->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryIndex(slot, true));
        if (!submitAndSignal(slot)) {
            return false;
        }
        veyra::log::info("gfx", std::format("slot-ring: slot={} recorded timestamp pair [{}..{}] and signaled fenceValue={}",
            slot, timestampQueryIndex(slot, false), timestampQueryIndex(slot, true), slots_[slot].fenceValue));
    }
    if (!waitIdle()) {
        return false;
    }
    veyra::log::info("gfx", std::format("slot-ring: exercised {} slots acquire->timestamps->submit->signal->waitIdle ok", slotCount_));
    return true;
}

} // namespace veyra::gfx
