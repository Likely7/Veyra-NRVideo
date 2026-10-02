#pragma once
#include "veyra/diagnostics/FrameMetrics.h"
#include <algorithm>
#include <cmath>

namespace veyra::diagnostics {
// One completed graph record, one source frame / FG batch. All these queries
// use the graph's direct queue clock. Merge overlapping intervals instead of
// counting nested work twice. Gaps, input color, presenter, audio and CPU
// waits are excluded. Flow includes its GPU-side dependency wait; this is
// measured stage time, not a hardware-engine utilization measurement.
// XeSS SDK-internal FG has no query here and must be disclosed by the UI.
inline std::optional<double> enhancementProcessingMs(const GpuFrameTiming& frame) {
    // Presenter records only contain Blit. Never let them add zero samples.
    if(frame.gpu[size_t(GpuStage::Color)].state!=SampleState::Measured)return {};
    // Every NR layer marks the shared Nr slot, so it only holds the last layer;
    // the per-layer spans carry the others (several layers read as one, 2026-10-02).
    constexpr GpuStage stages[]={GpuStage::Flow,GpuStage::Sr,GpuStage::Nr,GpuStage::NrLayer0,GpuStage::NrLayer1,
        GpuStage::NrLayer2,GpuStage::NrLayer3,GpuStage::Residual,GpuStage::FgBatch};
    struct Interval {uint64_t begin,end;};
    std::array<Interval,std::size(stages)> intervals{};
    size_t count=0;uint64_t frequency=0;
    for(auto stage:stages){
        const auto& sample=frame.gpu[size_t(stage)];
        if(sample.state==SampleState::NotExecuted)continue;
        if(sample.state!=SampleState::Measured||!sample.milliseconds||
           !std::isfinite(*sample.milliseconds)||*sample.milliseconds<0||
           !sample.frequency||sample.end<sample.begin)return {};
        if(frequency&&frequency!=sample.frequency)return {};
        frequency=sample.frequency;intervals[count++]={sample.begin,sample.end};
    }
    if(!count)return 0.0; // A measured graph with all enhancement stages off.
    std::sort(intervals.begin(),intervals.begin()+count,[](auto a,auto b){return a.begin<b.begin;});
    auto current=intervals[0];double ticks=0;
    for(size_t i=1;i<count;++i){
        const auto next=intervals[i];
        if(next.begin<=current.end)current.end=std::max(current.end,next.end);
        else{ticks+=double(current.end-current.begin);current=next;}
    }
    ticks+=double(current.end-current.begin);
    return ticks*1000.0/double(frequency);
}
}
