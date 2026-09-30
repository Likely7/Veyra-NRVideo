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
enum class NrSizePolicy { Realtime, Native, P480, P720, P900, P1440 };
constexpr bool validNrSizePolicy(NrSizePolicy policy){return policy>=NrSizePolicy::Realtime&&policy<=NrSizePolicy::P1440;}
constexpr unsigned nrHeightLimit(NrSizePolicy policy){
    switch(policy){case NrSizePolicy::Realtime:return 1080;case NrSizePolicy::P480:return 480;case NrSizePolicy::P720:return 720;case NrSizePolicy::P900:return 900;case NrSizePolicy::P1440:return 1440;default:return 0;}
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
struct ResolutionPlan {
    Extent source,base,nr,flow,fg,output;
    bool srApplied=false;
    uint64_t settingsRevision=0;
    static ResolutionPlan make(Extent source,bool sr,NrSizePolicy policy,bool exporting,uint64_t revision=0,SrTarget target=SrTarget::Uhd4K,bool nrBeforeSr=false) {
        if(!source.valid())throw std::invalid_argument("invalid SDR source extent");
        if(!validSrTarget(target))throw std::invalid_argument("invalid SR target");
        if(!validNrSizePolicy(policy))throw std::invalid_argument("invalid NR size policy");
        ResolutionPlan p; p.source=source;p.base=source;
        if(sr){const auto limit=srTargetExtent(target);const double scale=std::min(double(limit.width)/source.width,double(limit.height)/source.height);if(scale>1.0)p.base={std::max(2u,uint32_t(source.width*scale+1e-6)&~1u),std::max(2u,uint32_t(source.height*scale+1e-6)&~1u)};}
        p.srApplied=sr&&p.base!=source;p.nr=p.base;
        if(nrBeforeSr&&!exporting)p.nr=source;
        if(!exporting&&policy!=NrSizePolicy::Native) {
            const double limit=nrHeightLimit(policy);
            const double scale=std::min({1.0,(limit*16.0/9.0)/p.nr.width,limit/p.nr.height});
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
