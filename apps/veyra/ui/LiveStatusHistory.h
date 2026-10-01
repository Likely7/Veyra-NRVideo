#pragma once
#include "veyra/engine/EngineController.h"
#include <deque>
#include <optional>

namespace veyra::ui::live_status {
struct DashboardHistory {
    std::deque<std::optional<double>> points;
    uint64_t session=0,revision=0,epoch=0;
    unsigned low=0,good=0,inputLow=0,resetSamples=0;
    bool overloaded=false,inputLimited=false;
    void sample(const engine::PlayerSnapshot& s){
        if(session!=s.sessionId||revision!=s.applied.revision){
            points.clear();low=good=inputLow=resetSamples=0;epoch=0;
            overloaded=inputLimited=false;session=s.sessionId;revision=s.applied.revision;
        }
        const bool active=s.running&&!s.image&&s.transport==engine::TransportState::Playing&&!s.applying;
        points.push_back(active?s.metrics.flow.enhancementProcessing.mean:std::optional<double>{});
        if(points.size()>120)points.pop_front();
        const auto nextEpoch=s.metrics.flow.reset.epoch;
        if(active&&epoch&&nextEpoch>epoch+1)resetSamples=std::min(resetSamples+1,8u);
        else if(resetSamples) --resetSamples;
        epoch=nextEpoch;
        const bool measuredInput=s.capture&&s.captureReceived>30&&s.captureFps>0;
        const double source=measuredInput?std::min(s.nominalSourceFps,s.captureFps):s.nominalSourceFps;
        if(active&&measuredInput&&s.captureFps<s.nominalSourceFps*.95)inputLow=std::min(inputLow+1,8u);
        else inputLow=0;
        inputLimited=inputLow>=8;
        const bool xess=s.applied.multiplier>1&&engine::presentSinkFrameGeneration(s.applied.frameGenerationBackend);
        const double actual=xess?s.metrics.flow.xessSdkSubmitFps:s.metrics.flow.presentSubmitFps;
        const double target=source*(s.captureHalfRate?.5:1)*s.applied.multiplier;
        if(!active||!s.metrics.flow.rateWindowReady||target<=0){low=good=0;overloaded=false;return;}
        if(actual<target*.95){good=0;if(++low>=8)overloaded=true;}
        else{low=0;if(++good>=8)overloaded=false;}
    }
    const wchar_t* rateStatus(const engine::PlayerSnapshot& s)const{
        if(resetSamples>=3)return L"Input timeline anomaly";
        if(inputLimited)return L"Input frame rate too low";
        if(s.applied.multiplier>1&&(s.fgBudgetLimited||s.xessGenerationSuppressed))return L"Frame gen scheduling throttled";
        return overloaded?L"Output below target":L"Normal";
    }
};
}
