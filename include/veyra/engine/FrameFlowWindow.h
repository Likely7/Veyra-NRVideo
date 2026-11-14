#pragma once
#include "veyra/diagnostics/FrameMetrics.h"
#include "veyra/diagnostics/EnhancementProcessingTime.h"
#include "veyra/engine/FrameRateWindow.h"
#include "veyra/engine/FrameLineage.h"
#include <mutex>
#include <memory>
#include <cmath>
#include <vector>

namespace veyra::engine {
// Completion rates belong to a settings revision, not a temporal history.
// A capture drop resets motion history but must not erase measured throughput.
class FrameCompletionRates {
    mutable std::mutex mutex_;
    FrameRateWindow generated_,presented_,source_,output_,xess_;
    FrameRateWindow realPresented_,generatedPresented_;
    FramePaceWindow presentPace_;
    int64_t start_;
public:
    explicit FrameCompletionRates(int64_t now):start_(now){
        generated_.reset(now);presented_.reset(now);source_.reset(now);output_.reset(now);xess_.reset(now);
        realPresented_.reset(now);generatedPresented_.reset(now);
    }
    void ready(bool real,unsigned valid,int64_t now){
        std::lock_guard lock(mutex_);
        if(real){source_.complete(now);output_.complete(now);}
        for(unsigned i=0;i<valid;++i){generated_.complete(now);output_.complete(now);}
    }
    // paceAt: when the Present call began (0 = now). The 1% low spaces presents
    // by that time, as RTSS and PresentMon do; rates count completions at now.
    void presented(bool generated,int64_t now,int64_t paceAt=0){std::lock_guard lock(mutex_);presented_.complete(now);presentPace_.complete(paceAt>0&&paceAt<=now?paceAt:now);(generated?generatedPresented_:realPresented_).complete(now);}
    void xess(uint64_t count,int64_t now){std::lock_guard lock(mutex_);for(uint64_t i=0;i<count;++i)xess_.complete(now);}
    void snapshot(diagnostics::FrameFlowMetrics& m,int64_t now)const{
        std::lock_guard lock(mutex_);m.validGeneratedFps=generated_.rate(now);m.presentSubmitFps=presented_.rate(now);
        m.sourceCompletedFps=source_.rate(now);m.outputCompletedFps=output_.rate(now);m.xessSdkSubmitFps=xess_.rate(now);m.rateWindowReady=now-start_>=10000000;
        m.realPresentFps=realPresented_.rate(now);m.generatedPresentFps=generatedPresented_.rate(now);
        m.presentLow1Fps=presentPace_.lowFps(now);
    }
};
// Each asynchronous job retains its own window. Replacing the current window
// never lets an old job increment a new settings/hard-reset window's counters.
// Soft preview/mailbox drops retain that window and its measured throughput.
class FrameFlowWindow {
    mutable std::mutex mutex_;
    diagnostics::FrameFlowMetrics metrics_;
    std::shared_ptr<FrameCompletionRates> rates_;
    uint64_t firstTimingEpoch_=0,lastTimingEpoch_=0;
    std::array<uint64_t,8192> readyBatches_{};
    struct Latency {int64_t time;double ms;};
    std::deque<Latency> latency_;
    mutable int64_t latencyRefresh_=0;
    mutable std::optional<double> latencyMean_,latencyP95_;
    mutable uint64_t latencyCount_=0;
    struct StageTiming {int64_t time;double ms;unsigned stage;};
    static constexpr unsigned gpuCount=unsigned(diagnostics::GpuStage::Count),pairBegin=gpuCount+unsigned(diagnostics::CpuStage::Count),enhancementIndex=pairBegin+unsigned(diagnostics::PairTiming::Count),stageCount=enhancementIndex+1;
    std::vector<StageTiming> timing_=std::vector<StageTiming>(8192);size_t timingHead_=0,timingSize_=0;
    std::array<uint64_t,gpuCount> lastGpuEnd_{};
    mutable int64_t timingRefresh_=0;
    mutable std::array<diagnostics::TimingAggregate,stageCount> aggregates_{};
    void stage(unsigned stage,double ms,int64_t now){
        if(stage>=stageCount||!std::isfinite(ms)||ms<0)return;
        while(timingSize_&&timing_[timingHead_].time<=now-10000000){timingHead_=(timingHead_+1)%timing_.size();--timingSize_;}
        if(timingSize_==timing_.size()){timingHead_=(timingHead_+1)%timing_.size();--timingSize_;++metrics_.timingOverflow;}
        timing_[(timingHead_+timingSize_++)%timing_.size()]={now,ms,stage};
    }
public:
    FrameFlowWindow(uint64_t session,pipeline::FrameIdentity id,int64_t now,std::shared_ptr<FrameCompletionRates> rates={}):rates_(rates?std::move(rates):std::make_shared<FrameCompletionRates>(now)){
        metrics_.latest.sessionId=session;metrics_.latest.frame=id;
        firstTimingEpoch_=lastTimingEpoch_=id.epoch;
    }
    // A mailbox/preview drop invalidates temporal inputs, not GPU measurements
    // from the same settings. Explicit open/seek/settings resets create a new
    // window; asynchronous results outside its registered epoch range reject.
    void advanceHistory(pipeline::FrameIdentity id){
        std::lock_guard lock(mutex_);
        if(id.settingsRevision==metrics_.latest.frame.settingsRevision&&id.epoch>=lastTimingEpoch_)
            lastTimingEpoch_=id.epoch;
    }
    template<class F> void update(F action){std::lock_guard lock(mutex_);action(metrics_);}
    // CPU-observed reset/rebuild lifecycle; GPU execution remains in GPU timing.
    void resetLifecycle(const diagnostics::FrameFlowMetrics::ResetRecord& record){
        std::lock_guard lock(mutex_);
        if(record.sessionId!=metrics_.latest.sessionId)return;
        metrics_.reset=record;
    }
    void cpu(diagnostics::CpuStage index,double ms,int64_t now){std::lock_guard lock(mutex_);stage(gpuCount+unsigned(index),ms,now);}
    void pairArrived(const FrameLineage& pair){
        std::lock_guard lock(mutex_);
        if(!metrics_.latest.sameWindow(metrics_.latest.sessionId,pair.a.identity)||
           !metrics_.latest.sameWindow(metrics_.latest.sessionId,pair.b.identity))return;
        metrics_.pairSourceA=pair.a.identity.sourceFrameId;metrics_.pairSourceB=pair.b.identity.sourceFrameId;
        metrics_.pairCaptureCallbacks=pair.a.captureCallback&&pair.b.captureCallback;
        stage(pairBegin+unsigned(diagnostics::PairTiming::ArrivalInterval),double(pair.b.host100ns-pair.a.host100ns)/10000,pair.b.host100ns);
    }
    void generatedLatency(const FrameLineage& pair,int64_t end){
        if(end<pair.b.host100ns||pair.b.host100ns<pair.a.host100ns||pair.a.host100ns<=0)return;
        std::lock_guard lock(mutex_);
        if(!metrics_.latest.sameWindow(metrics_.latest.sessionId,pair.a.identity)||
           !metrics_.latest.sameWindow(metrics_.latest.sessionId,pair.b.identity))return;
        stage(pairBegin+unsigned(diagnostics::PairTiming::GeneratedFromA),double(end-pair.a.host100ns)/10000,end);
        stage(pairBegin+unsigned(diagnostics::PairTiming::GeneratedFromB),double(end-pair.b.host100ns)/10000,end);
    }
    void gpu(const std::array<diagnostics::GpuSample,gpuCount>& samples,int64_t now){
        std::lock_guard lock(mutex_);
        for(unsigned i=0;i<gpuCount;++i){const auto& s=samples[i];if(s.state==diagnostics::SampleState::Measured&&s.milliseconds&&s.end&&s.end!=lastGpuEnd_[i]){lastGpuEnd_[i]=s.end;stage(i,*s.milliseconds,now);}}
    }
    void gpuFrame(const diagnostics::GpuFrameTiming& frame,int64_t now){
        std::lock_guard lock(mutex_);
        if(frame.identity.settingsRevision!=metrics_.latest.frame.settingsRevision||
           frame.identity.epoch<firstTimingEpoch_||frame.identity.epoch>lastTimingEpoch_)return;
        // Every dequeued record is delivered once. Different frames may have
        // equal timestamp endpoints, especially empty diagnostic passes.
        for(unsigned i=0;i<gpuCount;++i){const auto& s=frame.gpu[i];if(s.state==diagnostics::SampleState::Measured&&s.milliseconds)stage(i,*s.milliseconds,now);}
        if(const auto total=diagnostics::enhancementProcessingMs(frame))stage(enhancementIndex,*total,now);
    }
    void ready(uint64_t batch,bool real,unsigned valid,unsigned invalid,int64_t now){
        std::lock_guard lock(mutex_);
        if(!batch||readyBatches_[batch%readyBatches_.size()]==batch)return;
        readyBatches_[batch%readyBatches_.size()]=batch;
        metrics_.counters.fgReadyValid+=valid;metrics_.counters.fgInvalid+=invalid;
        metrics_.lastReady100ns=now;
        if(real)++metrics_.counters.realReady;
        rates_->ready(real,valid,now);
    }
    void xessSubmitted(uint64_t count,uint64_t generated,int64_t now){
        std::lock_guard lock(mutex_);metrics_.counters.xessSdkPresented+=count;metrics_.counters.xessSdkGenerated+=generated;
        rates_->xess(count,now);
    }
    void latency(int64_t begin,int64_t end){
        if(begin<=0||end<begin)return;
        std::lock_guard lock(mutex_);latency_.push_back({end,double(end-begin)/10000});
        while(latency_.size()>8192||(!latency_.empty()&&latency_.front().time<=end-10000000))latency_.pop_front();
    }
    void presented(bool generated,uint64_t fence,int64_t now,int64_t paceAt=0){
        std::lock_guard lock(mutex_);
        if(generated)++metrics_.counters.generatedPresented;else ++metrics_.counters.realPresented;
        metrics_.latest.consumerFence=fence;rates_->presented(generated,now,paceAt);
        metrics_.lastPresent100ns=now;
    }
    diagnostics::FrameFlowMetrics snapshot(int64_t now)const{
        std::lock_guard lock(mutex_);auto m=metrics_;rates_->snapshot(m,now);
        if(now>=latencyRefresh_){
            std::vector<double> values;double sum=0;
            for(const auto& sample:latency_)if(sample.time>now-10000000){values.push_back(sample.ms);sum+=sample.ms;}
            latencyCount_=values.size();latencyMean_.reset();latencyP95_.reset();
            if(!values.empty()){latencyMean_=sum/values.size();const size_t p=(values.size()*95+99)/100-1;std::nth_element(values.begin(),values.begin()+p,values.end());latencyP95_=values[p];}
            latencyRefresh_=now+2500000;
        }
        if(now>=timingRefresh_){
            std::array<std::vector<double>,stageCount> values;
            for(size_t i=0;i<timingSize_;++i){const auto& s=timing_[(timingHead_+i)%timing_.size()];if(s.time>now-10000000)values[s.stage].push_back(s.ms);}
            for(unsigned i=0;i<stageCount;++i){auto& a=aggregates_[i];auto& v=values[i];a={};a.samples=v.size();if(v.empty())continue;double sum=0;for(auto ms:v)sum+=ms;a.mean=sum/v.size();const auto p=(v.size()*95+99)/100-1;std::nth_element(v.begin(),v.begin()+p,v.end());a.p95=v[p];}
            timingRefresh_=now+2500000;
        }
        for(unsigned i=0;i<gpuCount;++i)m.gpuTiming[i]=aggregates_[i];
        for(unsigned i=0;i<unsigned(diagnostics::CpuStage::Count);++i)m.cpuTiming[i]=aggregates_[gpuCount+i];
        for(unsigned i=0;i<unsigned(diagnostics::PairTiming::Count);++i)m.pairTiming[i]=aggregates_[pairBegin+i];
        m.enhancementProcessing=aggregates_[enhancementIndex];
        m.softwareLatencyMs=latencyMean_;m.softwareLatencyP95Ms=latencyP95_;m.latencySamples=latencyCount_;return m;
    }
};
}
