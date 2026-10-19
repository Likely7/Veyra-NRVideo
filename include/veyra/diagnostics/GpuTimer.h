#pragma once
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/Log.h"
#include <format>
#include <deque>
#include <vector>
namespace veyra::diagnostics {
// Timestamp query ring, resolved only after the producing fence is complete.
// No image readback, no CPU waits. Samples carry the identity they measured.
class GpuTimer {
    static constexpr unsigned stages=unsigned(GpuStage::Count),slots=4,stride=stages*2;
    struct Pending{pipeline::FrameIdentity id;uint64_t mask=0;uint64_t fence=0;};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    std::array<Pending,slots> pending_{};uint64_t frequency_=0;int active_=-1;
    FrameMetrics last_;uint64_t lastCollectedFence_=0;
    std::deque<GpuFrameTiming> completed_;
    uint64_t skipped_=0,overflow_=0;
    bool recordCompleted_=false;
public:
    bool initialize(ID3D12Device* device,ID3D12CommandQueue* queue){
        close();HRESULT hr=queue->GetTimestampFrequency(&frequency_);if(FAILED(hr)||!frequency_)return false;
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=slots*stride;hr=device->CreateQueryHeap(&q,IID_PPV_ARGS(&heap_));if(FAILED(hr)){log::error("gpu-timestamp",std::format("query heap hr=0x{:X}",unsigned(hr)));return false;}
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=slots*stride*8;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr=device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_));if(FAILED(hr)){log::error("gpu-timestamp",std::format("readback hr=0x{:X}",unsigned(hr)));return false;}return true;
    }
    void collect(ID3D12Fence* fence){if(!readback_||!fence)return;const auto completed=fence->GetCompletedValue();
        for(unsigned slot=0;slot<slots;++slot){auto& p=pending_[slot];if(!p.fence||p.fence>completed)continue;void* data=nullptr;D3D12_RANGE range{slot*stride*8,(slot+1)*stride*8};auto hr=readback_->Map(0,&range,&data);if(FAILED(hr)){log::error("gpu-timestamp",std::format("Map hr=0x{:X}",unsigned(hr)));p.fence=0;continue;}
            FrameMetrics m;m.identity=p.id;auto values=static_cast<const uint64_t*>(data)+slot*stride;
            for(unsigned stage=0;stage<stages;++stage)if(p.mask&(1ull<<stage)){auto& v=m.gpu[stage];v.begin=values[stage*2];v.end=values[stage*2+1];v.frequency=frequency_;if(v.end>=v.begin){v.state=SampleState::Measured;v.milliseconds=double(v.end-v.begin)*1000/frequency_;}else v.state=SampleState::Unavailable;
                if(log::verboseFrameLogs())log::info("gpu-timestamp",std::format("frame={} epoch={} revision={} stage={} begin={} end={} frequency={} ms={} (GPU queue timestamps)",p.id.sourceFrameId,p.id.epoch,p.id.settingsRevision,stage,v.begin,v.end,frequency_,v.milliseconds.value_or(-1)));
            }
            D3D12_RANGE written{0,0};readback_->Unmap(0,&written);if(p.fence>=lastCollectedFence_){last_=m;lastCollectedFence_=p.fence;}
            if(recordCompleted_){
                if(completed_.size()==64){completed_.pop_front();++overflow_;if((overflow_&(overflow_-1))==0)log::warn("gpu-timestamp",std::format("timing telemetry overflow={} (old timing samples dropped, not video frames)",overflow_));}
                completed_.push_back({m.identity,m.gpu});
            }p.fence=0;
        }
    }
    void frame(pipeline::FrameIdentity id,ID3D12Fence* fence){collect(fence);active_=-1;if(!heap_||!readback_)return;
        for(unsigned offset=0;offset<slots;++offset){const auto i=unsigned((id.sourceFrameId+offset)%slots);if(pending_[i].fence)continue;active_=int(i);pending_[i]={id,0,0};return;}
        ++skipped_;if((skipped_&(skipped_-1))==0)log::warn("gpu-timestamp",std::format("no free query slot; skipped={} frame={}",skipped_,id.sourceFrameId));
    }
    void identity(pipeline::FrameIdentity id){if(active_>=0)pending_[active_].id=id;}
    void mark(ID3D12GraphicsCommandList* list,GpuStage stage,bool end=false){if(active_<0)return;unsigned i=unsigned(stage);list->EndQuery(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,active_*stride+i*2+unsigned(end));if(end)pending_[active_].mask|=1ull<<i;}
    void resolve(ID3D12GraphicsCommandList* list){if(active_<0)return;auto mask=pending_[active_].mask;for(unsigned i=0;i<stages;++i)if(mask&(1ull<<i)){unsigned index=active_*stride+i*2;list->ResolveQueryData(heap_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,index,2,readback_.Get(),index*8);}}
    void submitted(uint64_t fence){if(active_>=0)pending_[active_].fence=fence;active_=-1;}
    const FrameMetrics& last()const{return last_;}
    void recordCompleted(){recordCompleted_=true;}
    std::vector<GpuFrameTiming> takeCompleted(){std::vector<GpuFrameTiming> result;result.reserve(completed_.size());while(!completed_.empty()){result.push_back(std::move(completed_.front()));completed_.pop_front();}return result;}
    uint64_t skipped()const{return skipped_;}
    uint64_t overflow()const{return overflow_;}
    void close(){heap_.Reset();readback_.Reset();pending_={};last_={};lastCollectedFence_=0;completed_.clear();skipped_=overflow_=0;active_=-1;frequency_=0;}
};
}
