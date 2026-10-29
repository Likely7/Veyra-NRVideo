#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace veyra::pipeline {
struct Extent {
    uint32_t width=0,height=0;
    bool operator==(const Extent&) const = default;
    // D3D12 texture extent, not a product aspect-ratio or video-format policy.
    bool valid() const {return width>=1&&height>=1&&width<=16384&&height<=16384;}
};
// Keep saved fixed/Auto IDs 0..6 unchanged. A manual percentage is one size
// policy, encoded as 100+percent (125..200). This travels through existing
// per-layer presets/sessions/export POD without adding a second mutable value.
enum class NrSizePolicy { Realtime, Native, P480, P720, P900, P1440, Auto, Manual=200 };
inline constexpr unsigned kNrMinimumPercent=25,kNrMaximumPercent=100;
constexpr bool manualNrSizePolicy(NrSizePolicy policy){return int(policy)>=100+int(kNrMinimumPercent)&&int(policy)<=int(NrSizePolicy::Manual);}
constexpr NrSizePolicy nrSizePolicyForPercent(unsigned percent){
    return percent>=kNrMinimumPercent&&percent<=kNrMaximumPercent?NrSizePolicy(100+percent):NrSizePolicy(-1);
}
constexpr unsigned nrSizePercent(NrSizePolicy policy){return manualNrSizePolicy(policy)?unsigned(int(policy)-100):100;}
constexpr bool validNrSizePolicy(NrSizePolicy policy){return (policy>=NrSizePolicy::Realtime&&policy<=NrSizePolicy::Auto)||manualNrSizePolicy(policy);}
constexpr unsigned nrHeightLimit(NrSizePolicy policy){
    switch(policy){case NrSizePolicy::Realtime:case NrSizePolicy::Auto:return 1080;case NrSizePolicy::P480:return 480;case NrSizePolicy::P720:return 720;case NrSizePolicy::P900:return 900;case NrSizePolicy::P1440:return 1440;default:return 0;}
}
// Preserve the existing 0/1/2 preset IDs. New choices are appended.
enum class SrTarget : uint32_t { Qhd, Uhd4K, Uhd8K, Uhd5K, Uhd6K, Uhd7K };
constexpr bool validSrTarget(SrTarget target) { return target<=SrTarget::Uhd7K; }
constexpr Extent srTargetExtent(SrTarget target) {
    switch(target) {
    case SrTarget::Qhd:return {2560,1440};
    case SrTarget::Uhd4K:return {3840,2160};
    case SrTarget::Uhd8K:return {7680,4320};
    case SrTarget::Uhd5K:return {5120,2880};
    case SrTarget::Uhd6K:return {6144,3456};
    case SrTarget::Uhd7K:return {7168,4032};
    }
    return {};
}
// Three-step SR (user 2026-10-28): shrink the picture before SR, then fit the
// SR result into a smaller output. Off/Target keep the single-step SR. A fixed
// height fits a 16:9 box like NrSizePolicy; a manual value is 100+percent of
// the SR input (50..90), stored the way NR stores its percent.
enum class SrPrescale : uint32_t { Off, P720, P900, P1080, P1440 };
inline constexpr unsigned kSrPrescaleMinimumPercent=50,kSrPrescaleMaximumPercent=90;
constexpr bool manualSrPrescale(SrPrescale prescale){
    return uint32_t(prescale)>=100+kSrPrescaleMinimumPercent&&uint32_t(prescale)<=100+kSrPrescaleMaximumPercent;
}
constexpr SrPrescale srPrescaleForPercent(unsigned percent){
    return percent>=kSrPrescaleMinimumPercent&&percent<=kSrPrescaleMaximumPercent?SrPrescale(100+percent):SrPrescale(~0u);
}
constexpr unsigned srPrescalePercent(SrPrescale prescale){return manualSrPrescale(prescale)?uint32_t(prescale)-100:100;}
constexpr bool validSrPrescale(SrPrescale prescale){return prescale<=SrPrescale::P1440||manualSrPrescale(prescale);}
constexpr unsigned srPrescaleHeight(SrPrescale prescale){
    switch(prescale){case SrPrescale::P720:return 720;case SrPrescale::P900:return 900;case SrPrescale::P1080:return 1080;case SrPrescale::P1440:return 1440;default:return 0;}
}
// Target keeps the SR result; Source returns to the source extent.
enum class SrOutput : uint32_t { Target, Source, Qhd, Uhd4K, Uhd5K, Uhd6K, Uhd7K };
constexpr bool validSrOutput(SrOutput output){return output<=SrOutput::Uhd7K;}
constexpr Extent srOutputExtent(SrOutput output,Extent source){
    switch(output){
    case SrOutput::Source:return source;
    case SrOutput::Qhd:return srTargetExtent(SrTarget::Qhd);
    case SrOutput::Uhd4K:return srTargetExtent(SrTarget::Uhd4K);
    case SrOutput::Uhd5K:return srTargetExtent(SrTarget::Uhd5K);
    case SrOutput::Uhd6K:return srTargetExtent(SrTarget::Uhd6K);
    case SrOutput::Uhd7K:return srTargetExtent(SrTarget::Uhd7K);
    default:return {};
    }
}
// Sizes of one SR step: input after the prescale, the SR result, the step
// output. A prescale that leaves nothing for SR to enlarge, and an output that
// would not shrink an enlarged result, are not applied and are reported.
struct SrStageExtents {
    Extent input,evaluated,output;
    bool prescaleIneffective=false,outputIneffective=false;
    bool enlarges()const{return evaluated!=input;}
};
inline SrStageExtents srStageExtents(Extent source,SrTarget target,SrPrescale prescale=SrPrescale::Off,SrOutput output=SrOutput::Target){
    // Fit into the box, enlarging only: the single-step SR size rule.
    const auto enlarge=[](Extent from,Extent box){
        const double scale=std::min(double(box.width)/from.width,double(box.height)/from.height);
        return scale>1.0?Extent{std::max(2u,uint32_t(from.width*scale+1e-6)&~1u),std::max(2u,uint32_t(from.height*scale+1e-6)&~1u)}:from;
    };
    const auto shrink=[](Extent from,double scale){
        return Extent{std::max(2u,uint32_t(from.width*scale+1e-6)&~1u),std::max(2u,uint32_t(from.height*scale+1e-6)&~1u)};
    };
    SrStageExtents s;s.input=source;s.evaluated=enlarge(source,srTargetExtent(target));
    if(prescale!=SrPrescale::Off){
        const double height=srPrescaleHeight(prescale);
        const double scale=manualSrPrescale(prescale)?srPrescalePercent(prescale)/100.0:
            std::min({1.0,(height*16.0/9.0)/source.width,height/source.height});
        const auto input=shrink(source,scale);
        const auto evaluated=enlarge(input,srTargetExtent(target));
        if(scale<1.0&&input.width<source.width&&input.height<source.height&&evaluated!=input){s.input=input;s.evaluated=evaluated;}
        else s.prescaleIneffective=true;
    }
    s.output=s.evaluated;
    if(output!=SrOutput::Target){
        const auto box=srOutputExtent(output,source);
        const double scale=std::min(double(box.width)/s.evaluated.width,double(box.height)/s.evaluated.height);
        if(s.enlarges()&&scale<1.0)s.output=output==SrOutput::Source?source:shrink(s.evaluated,scale);
        else if(s.evaluated!=box)s.outputIneffective=true;
    }
    return s;
}
struct ResolutionPlan {
    Extent source,base,nr,flow,fg,output;
    // What SR reads and writes. Single-step SR: srInput==source, srOutput==base.
    Extent srInput,srOutput;
    Extent firstSrOutput;
    bool dualSrApplied=false;
    bool srApplied=false;
    bool srPrescaleIneffective=false,srOutputIneffective=false;
    uint64_t settingsRevision=0;
    static ResolutionPlan make(Extent source,bool sr,NrSizePolicy policy,bool exporting,uint64_t revision=0,SrTarget target=SrTarget::Uhd4K,bool nrBeforeSr=false,unsigned manualHeightBudget=0,
                               SrPrescale prescale=SrPrescale::Off,SrOutput output=SrOutput::Target,
                               bool dual=false,SrTarget secondTarget=SrTarget::Uhd4K) {
        if(!source.valid())throw std::invalid_argument("invalid SDR source extent");
        if(!validSrTarget(target))throw std::invalid_argument("invalid SR target");
        if(!validNrSizePolicy(policy))throw std::invalid_argument("invalid NR size policy");
        if(!validSrPrescale(prescale)||!validSrOutput(output))throw std::invalid_argument("invalid SR prescale or output");
        if(!validSrTarget(secondTarget))throw std::invalid_argument("invalid second SR target");
        ResolutionPlan p; p.source=source;p.base=p.srInput=p.srOutput=source;
        if(sr){
            const auto stage=srStageExtents(source,target,prescale,output);
            p.srInput=stage.input;p.srOutput=stage.evaluated;p.base=stage.output;
            p.srPrescaleIneffective=stage.prescaleIneffective;p.srOutputIneffective=stage.outputIneffective;
            if(dual){
                const auto first=srStageExtents(source,target);
                const auto second=srStageExtents(first.output,secondTarget,prescale,output);
                p.firstSrOutput=first.output;
                // A second pass needs a real reduction followed by enlargement.
                p.dualSrApplied=second.input!=first.output&&second.enlarges();
                if(p.dualSrApplied){p.srInput=second.input;p.srOutput=second.evaluated;p.base=second.output;}
                else{p.srInput=source;p.srOutput=p.base=first.output;}
                p.srPrescaleIneffective=!p.dualSrApplied;p.srOutputIneffective=second.outputIneffective;
            }
        }
        if(!p.firstSrOutput.valid())p.firstSrOutput=p.srOutput;
        p.srApplied=sr&&p.srOutput!=p.srInput;p.nr=p.base;
        if(nrBeforeSr&&!exporting)p.nr=source;
        if(!exporting&&policy!=NrSizePolicy::Native) {
            // A bounded backend supplies its existing maximum budget. Manual
            // 100% then means that available input; other backends use the
            // layer's actual input. Offline/native semantics stay unchanged.
            const bool manual=manualNrSizePolicy(policy);
            const double limit=manual?manualHeightBudget:nrHeightLimit(policy);
            const double fit=manual&&manualHeightBudget==0?1.0:
                std::min({1.0,(limit*16.0/9.0)/p.nr.width,limit/p.nr.height});
            const double scale=fit*(manual?double(nrSizePercent(policy))/100.0:1.0);
            if(scale<1.0)p.nr={std::max(1u,uint32_t(p.nr.width*scale)&~1u),std::max(1u,uint32_t(p.nr.height*scale)&~1u)};
        }
        p.flow=source;
        // NVOF reads source-space color. In the explicitly labelled realtime
        // path it must not silently remain at native 4K after NR was reduced.
        // Inputs smaller than the realtime NR extent stay at their native size.
        if(!exporting&&policy!=NrSizePolicy::Native&&
           (p.nr.width<p.source.width||p.nr.height<p.source.height))p.flow=p.nr;
        p.fg=p.output=p.base;p.settingsRevision=revision;return p;
    }
};
}
