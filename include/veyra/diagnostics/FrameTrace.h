#pragma once
#include "veyra/pipeline/FrameBatch.h"
#include <algorithm>
#include <array>
#include <vector>

namespace veyra::diagnostics {
enum class TraceKind { Submitted, Ready, Present, Gpu, Reset, Cancelled, FrameReady, Discarded, XessSleep, XessBind, XessPresent, ProviderOutput, ProviderDeadline };
inline const char* traceKindName(TraceKind kind) {
    switch(kind){
    case TraceKind::Submitted:return "Submitted";
    case TraceKind::Ready:return "Ready";
    case TraceKind::Present:return "Present";
    case TraceKind::Gpu:return "Gpu";
    case TraceKind::Reset:return "Reset";
    case TraceKind::Cancelled:return "Cancelled";
    case TraceKind::FrameReady:return "FrameReady";
    case TraceKind::Discarded:return "Discarded";
    case TraceKind::XessSleep:return "XessSleep";
    case TraceKind::XessBind:return "XessBind";
    case TraceKind::XessPresent:return "XessPresent";
    case TraceKind::ProviderOutput:return "ProviderOutput";
    case TraceKind::ProviderDeadline:return "ProviderDeadline";
    }
    return "Unknown";
}
struct FrameTraceEvent {
    int64_t host100ns=0;
    uint64_t session=0;
    pipeline::FrameIdentity identity;
    uint64_t batch=0,fence=0;
    int64_t pts100ns=0;
    TraceKind kind=TraceKind::Submitted;
    uint32_t detail=0,count=0;
    double milliseconds=0;
    // Populated for Present only. All host stamps use the same monotonic clock;
    // GPU readiness is CPU-observed, never physical display completion.
    int64_t decodedHost=0,processHost=0,readyHost=0,presentBeginHost=0,presentEndHost=0;
    double entryDeviationMs=0,returnDeviationMs=0;
    uint32_t queueDepth=0;
    bool mediaDeviationValid=false;
    // Provider instance disambiguates cycles across rebuilds. A preparation
    // cycle is not a source ID; several queued sources can share it.
    uint64_t providerInstance=0;
    uint32_t providerCycle=0,preparationCycle=0;
    uint32_t providerCallerRva=0;
    int64_t providerScheduleBeginHost=0;
    bool providerFenceSampled=false;
    uint64_t providerFenceBefore=0,providerFenceAtDeadline=0,providerFenceTarget=0;
};
// Owned by the logger, independent of the deduplicated error history. No
// allocation, formatting or disk I/O occurs when an event is recorded.
class FrameTrace {
public:
    static constexpr size_t capacity=8192;
    void add(FrameTraceEvent event){
        if(size_==capacity)++overwritten_;
        events_[next_]=event;next_=(next_+1)%capacity;size_=std::min(size_+1,capacity);
    }
    size_t size()const{return size_;}
    uint64_t overwritten()const{return overwritten_;}
    std::vector<FrameTraceEvent> snapshot()const{
        std::vector<FrameTraceEvent> result;result.reserve(size_);
        const auto first=(next_+capacity-size_)%capacity;
        for(size_t i=0;i<size_;++i)result.push_back(events_[(first+i)%capacity]);
        return result;
    }
private:
    std::array<FrameTraceEvent,capacity> events_;
    size_t next_=0,size_=0;
    uint64_t overwritten_=0;
};
}
