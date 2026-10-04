#pragma once
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/Log.h"
#include "veyra/ngx/FgCompatibilitySession.h"
#include <filesystem>
#include <format>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

namespace veyra::engine {
// One inactive single-NR graph. Only an effects-off graph may coexist, so
// there is never a second NR adapter/IAT owner or a second NGX Init.
class RecentGraphCache {
public:
    struct Key {
        EnhancementSettings settings;
        std::optional<ChainRuntimeOrder> order;
        unsigned sourceWidth=0,sourceHeight=0,workWidth=0,workHeight=0,nrWidth=0,nrHeight=0;
        bool hdrInput=false,hdrOutput=false,rgbInput=false;
        unsigned captureBitDepth=8,packedInput=0;bool yuy2Input=false;
        std::wstring runtime;
        uint64_t nrFileBytes=0;int64_t nrFileTimestamp=0;
        bool operator==(const Key&)const=default;
    };
    explicit RecentGraphCache(gfx::D3D12DeviceContext& context):context_(context){}
    ~RecentGraphCache(){evict("source-close");}
    static Key key(EnhancementSettings settings,std::optional<ChainRuntimeOrder> order,const pipeline::EnhanceGraphDesc& desc){
        settings.revision=0;
        Key result{std::move(settings),std::move(order),desc.sourceWidth,desc.sourceHeight,desc.workWidth,desc.workHeight,
            desc.nrWidth,desc.nrHeight,desc.hdrInput,desc.hdrOutput,desc.rgbInput,desc.captureBitDepth,desc.packedInput,desc.yuy2Input,desc.runtimeAbsPath};
        std::error_code error;const auto module=std::filesystem::path(desc.runtimeAbsPath)/L"nvngx_dlssnr.dll";
        result.nrFileBytes=std::filesystem::file_size(module,error);
        if(error)result.nrFileBytes=0;
        const auto time=std::filesystem::last_write_time(module,error);
        if(error)result.nrFileBytes=0;else result.nrFileTimestamp=time.time_since_epoch().count();
        return result;
    }
    static bool eligible(const pipeline::EnhanceGraphDesc& desc){
        return desc.enableNr&&!desc.enableSr&&!desc.enableFg&&!desc.hdrInput&&!desc.hdrOutput&&!desc.videoHdr.enabled&&!desc.color.enabled&&desc.additionalColorCount==0&&
            desc.nrHoldStrength==0&&!desc.noFeatures&&!desc.noNgx&&!desc.stillImage&&!desc.nrAutoPoolLayer&&
            desc.nrRuntime==NrRuntime::Original&&!desc.nrTemporal&&desc.nrLayersModel.size()<=1&&
            std::none_of(desc.nrLayersTemporal.begin(),desc.nrLayersTemporal.end(),[](bool value){return value;})&&
            !desc.runtimeNodeOrder&&(!desc.fixedExecutionPlan||(
                desc.fixedExecutionPlan->stepCount==1&&desc.fixedExecutionPlan->steps[0].type==EffectType::NrEnhance));
    }
    static bool effectsOff(const pipeline::EnhanceGraphDesc& desc){
        return !desc.enableNr&&!desc.enableSr&&!desc.enableFg&&!desc.videoHdr.enabled&&!desc.color.enabled&&desc.additionalColorCount==0;
    }
    static std::string label(const Key& key){
        return std::format("{}x{}:{}x{}:{}x{}:runtime{}:file{}:mtime{}",key.sourceWidth,key.sourceHeight,
            key.workWidth,key.workHeight,key.nrWidth,key.nrHeight,int(key.settings.nrRuntime),key.nrFileBytes,key.nrFileTimestamp);
    }
    bool retain(std::unique_ptr<pipeline::EnhanceGraph>& graph,Key key,uint64_t bytes){
        evict("replace");uint64_t budget=0,usage=0;
        if(!graph||graph->failedBackend()!=FailedBackend::None||!bytes||!key.nrFileBytes||
            ngx::FgCompatibilitySession::requested(context_.adapter().vendorId,context_.adapter().deviceId)||FAILED(context_.device()->GetDeviceRemovedReason())||
            !context_.videoMemoryInfo(budget,usage)||!fits(budget,usage,bytes)){
            log::info("feature-cache",std::format("event=miss reason=budget-or-session key={} bytes={} usage={} budget={}",label(key),bytes,usage,budget));return false;}
        graph_=std::move(graph);key_=std::move(key);bytes_=bytes;
        log::info("feature-cache",std::format("event=retain key={} bytes={} usage={} budget={}",label(*key_),bytes_,usage,budget));return true;
    }
    std::unique_ptr<pipeline::EnhanceGraph> take(const Key& key){
        if(!graph_)return {};
        if(!key_||!(*key_==key)){evict("key-changed");return {};}
        if(!checkBudget())return {};
        graph_->invalidatePausedResidualCache();
        log::info("feature-cache",std::format("event=hit key={} bytes={} reset-required=true",label(*key_),bytes_));
        key_.reset();bytes_=0;return std::move(graph_);
    }
    bool checkBudget(){
        if(!graph_)return true;uint64_t budget=0,usage=0;
        if(FAILED(context_.device()->GetDeviceRemovedReason())||!context_.videoMemoryInfo(budget,usage)||!fits(budget,usage,bytes_)){
            evict("budget-pressure-or-device");return false;}
        return true;
    }
    void evict(const char* reason){
        if(!graph_)return;
        log::info("feature-cache",std::format("event=evict reason={} bytes={}",reason,bytes_));
        graph_->shutdown();graph_.reset();key_.reset();bytes_=0;
    }
    uint64_t bytes()const{return bytes_;}
    bool hasCachedGraph()const{return bool(graph_);}
    static bool fits(uint64_t budget,uint64_t usage,uint64_t bytes){
        // Inactive graphs are expendable: preserve only while this process
        // uses less than one third of its dynamic WDDM budget, as well as the
        // required 1.5x spare footprint. Avoid reserving headroom near pressure.
        return bytes&&budget>usage&&usage<budget/3&&bytes<=UINT64_MAX/3&&(budget-usage)>bytes+bytes/2;
    }
private:
    gfx::D3D12DeviceContext& context_;
    std::unique_ptr<pipeline::EnhanceGraph> graph_;
    std::optional<Key> key_;
    uint64_t bytes_=0;
};
}
