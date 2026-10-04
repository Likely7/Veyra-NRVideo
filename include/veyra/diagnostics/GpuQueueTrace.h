#pragma once
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/Log.h"
#include <format>
namespace veyra::diagnostics {
// Test-only timestamp trace. Cross-queue timestamps require each queue's
// calibration; numerical equality of raw counters alone proves no overlap.
class GpuQueueTrace {
    ID3D12CommandQueue* queue_=nullptr;const char* role_="";
public:
    bool initialize(ID3D12CommandQueue* queue,const char* role){queue_=queue;role_=role;return calibrate();}
    bool calibrate(){
        if(!queue_)return false;UINT64 gpu=0,cpu=0,frequency=0;LARGE_INTEGER before{},after{},qpc{};
        QueryPerformanceFrequency(&qpc);QueryPerformanceCounter(&before);
        const HRESULT a=queue_->GetTimestampFrequency(&frequency),b=queue_->GetClockCalibration(&gpu,&cpu);
        QueryPerformanceCounter(&after);
        log::info("prefetch-clock",std::format("role={} frequency={} gpu={} cpu={} qpcFrequency={} uncertaintyTicks={} frequencyHr=0x{:X} calibrationHr=0x{:X}",
            role_,frequency,gpu,cpu,qpc.QuadPart,after.QuadPart-before.QuadPart,unsigned(a),unsigned(b)));
        return SUCCEEDED(a)&&SUCCEEDED(b)&&frequency&&qpc.QuadPart;
    }
    void emit(const GpuFrameTiming& frame){
        if(!queue_)return;
        for(const auto stage:{GpuStage::Flow,GpuStage::Sr,GpuStage::NrLayer0,GpuStage::NrLayer1}){
            const auto& s=frame.gpu[size_t(stage)];if(s.state!=SampleState::Measured||!s.frequency)continue;
            log::info("prefetch-gpu",std::format("role={} source={} stage={} begin={} end={} frequency={}",
                role_,frame.identity.sourceFrameId,unsigned(stage),s.begin,s.end,s.frequency));
        }
    }
};
}
