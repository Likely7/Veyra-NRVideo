#pragma once
#include "veyra/engine/EngineController.h"
#include <deque>
#include <optional>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace veyra::ui::live_status {
struct RateContext {
    double displayHz=0;
    bool comparison=false;
    double nowSeconds=-1; // explicit monotonic time for deterministic contract tests
};
struct OutputRateAssessment {
    double transportFps=0,contentFps=0,requestedFps=0,targetFps=0,actualFps=0;
    double skippedFps=0;
    bool deduplicating=false,halfRate=false,displayLimited=false,outputLimited=false;
    bool comparison=false,speedAdjusted=false;
    bool policyLimited()const{return deduplicating||halfRate||displayLimited||outputLimited||comparison||speedAdjusted;}
};
inline OutputRateAssessment assessOutputRate(const engine::PlayerSnapshot& s,const RateContext& context,double skippedFps=0){
    OutputRateAssessment result;
    const bool measuredInput=s.capture&&s.captureReceived>30&&s.captureFps>0;
    result.transportFps=measuredInput?std::min(s.nominalSourceFps,s.captureFps):s.nominalSourceFps;
    result.contentFps=result.transportFps;
    result.halfRate=s.captureHalfRate;
    if(result.halfRate)result.contentFps*=.5;
    if(s.capture&&s.applied.content!=engine::ContentRate::Transport&&skippedFps>0){
        result.skippedFps=std::clamp(skippedFps,0.0,std::max(0.0,result.transportFps-1.0));
        double unique=std::max(1.0,result.transportFps-result.skippedFps);
        // Counter-window quantisation must not make regular 29.97/30 fps
        // content oscillate across the warning threshold. The learned rate is
        // only a nearby hint after actual intentional skips were observed.
        if(s.contentFps>0&&std::abs(unique-s.contentFps)<=std::max(1.0,result.transportFps*.04))unique=s.contentFps;
        result.contentFps=std::min(result.contentFps,unique);
        result.deduplicating=result.skippedFps>=.5;
    }
    if(!s.capture&&s.playbackRate>0&&std::isfinite(s.playbackRate)){
        result.contentFps*=s.playbackRate;result.speedAdjusted=std::abs(s.playbackRate-1.0)>.001;
    }
    result.comparison=context.comparison&&s.applied.multiplier>1;
    result.requestedFps=result.contentFps*(result.comparison?1u:s.applied.multiplier);
    result.targetFps=result.requestedFps;
    const auto& effective=s.presentationEffective;
    if(effective.outputRate==engine::OutputRateMode::Custom&&effective.customFps>0&&std::isfinite(effective.customFps)){
        result.outputLimited=effective.customFps<result.targetFps*.99;
        result.targetFps=std::min(result.targetFps,effective.customFps);
    }else if(effective.outputRate==engine::OutputRateMode::FollowDisplay&&context.displayHz>0){
        result.outputLimited=context.displayHz<result.targetFps*.99;
        result.targetFps=std::min(result.targetFps,context.displayHz);
    }
    if(engine::presentationVsync(effective)&&context.displayHz>0&&std::isfinite(context.displayHz)){
        result.displayLimited=context.displayHz<result.targetFps*.99;
        result.targetFps=std::min(result.targetFps,context.displayHz);
    }
    const bool provider=s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
    result.actualFps=provider?s.metrics.flow.xessSdkSubmitFps:s.metrics.flow.presentSubmitFps;
    return result;
}
struct DashboardHistory {
    std::deque<std::optional<double>> points;
    uint64_t session=0,revision=0,epoch=0;
    unsigned low=0,good=0,inputLow=0,resetSamples=0;
    bool overloaded=false,inputLimited=false;
    OutputRateAssessment output;
    struct SkipSample{double seconds;uint64_t count;};
    std::deque<SkipSample> skipped;
    uint64_t presentationRevision=0;
    double previousHz=0;
    bool previousComparison=false;
    void sample(const engine::PlayerSnapshot& s,RateContext context={}){
        if(session!=s.sessionId||revision!=s.applied.revision){
            points.clear();low=good=inputLow=resetSamples=0;epoch=0;
            skipped.clear();overloaded=inputLimited=false;session=s.sessionId;revision=s.applied.revision;
        }
        if(presentationRevision!=s.presentationRevision||std::abs(previousHz-context.displayHz)>.5||previousComparison!=context.comparison){
            low=good=0;overloaded=false;presentationRevision=s.presentationRevision;previousHz=context.displayHz;previousComparison=context.comparison;
        }
        const bool active=s.running&&!s.image&&s.transport==engine::TransportState::Playing&&!s.applying;
        points.push_back(active?s.metrics.flow.enhancementProcessing.mean:std::optional<double>{});
        if(points.size()>120)points.pop_front();
        const auto nextEpoch=s.metrics.flow.reset.epoch;
        if(active&&epoch&&nextEpoch>epoch+1)resetSamples=std::min(resetSamples+1,8u);
        else if(resetSamples) --resetSamples;
        epoch=nextEpoch;
        const bool measuredInput=s.capture&&s.captureReceived>30&&s.captureFps>0;
        if(active&&measuredInput&&s.captureFps<s.nominalSourceFps*.95)inputLow=std::min(inputLow+1,8u);
        else inputLow=0;
        inputLimited=inputLow>=8;
        double skippedFps=0;
        if(active&&s.capture&&s.applied.content!=engine::ContentRate::Transport){
            const double now=context.nowSeconds>=0?context.nowSeconds:std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if(!skipped.empty()&&(now<skipped.back().seconds||s.captureRateSkipped<skipped.back().count))skipped.clear();
            if(skipped.empty()||now>skipped.back().seconds)skipped.push_back({now,s.captureRateSkipped});
            while(skipped.size()>1&&skipped[1].seconds<=now-1.0)skipped.pop_front();
            while(skipped.size()>32)skipped.pop_front();
            const double span=now-skipped.front().seconds;
            if(span>=.5)skippedFps=double(s.captureRateSkipped-skipped.front().count)/span;
        }else skipped.clear();
        output=assessOutputRate(s,context,skippedFps);
        const double actual=output.actualFps,target=output.targetFps;
        if(!active||!s.metrics.flow.rateWindowReady||target<=0){low=good=0;overloaded=false;return;}
        if(actual<target*.95){good=0;if(++low>=8)overloaded=true;}
        else{low=0;if(++good>=8)overloaded=false;}
    }
    bool fgScheduleLimited(const engine::PlayerSnapshot& s)const{
        return s.applied.multiplier>1&&!output.comparison&&(s.fgBudgetLimited||s.xessGenerationSuppressed)&&
            (!output.policyLimited()||overloaded);
    }
    const wchar_t* rateStatus(const engine::PlayerSnapshot& s)const{
        if(resetSamples>=3)return L"输入时间线异常";
        if(inputLimited)return L"输入帧率不足";
        if(fgScheduleLimited(s))return L"补帧调度降档";
        return overloaded?L"输出未达标":L"正常";
    }
};
}
