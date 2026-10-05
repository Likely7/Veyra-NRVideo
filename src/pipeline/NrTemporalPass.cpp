#include "veyra/pipeline/NrTemporalPass.h"
#include <bit>
#include <cmath>
#include <algorithm>
namespace veyra::pipeline {
namespace {
// Route numbers must match shaders/NrTemporal.hlsl.
unsigned routeOf(engine::NrAntiFlicker tier){
    switch(tier){
    case engine::NrAntiFlicker::Static:       return 1;
    case engine::NrAntiFlicker::Flow:         return 2;
    case engine::NrAntiFlicker::FlowPlus:     return 3;
    case engine::NrAntiFlicker::LowFrequency: return 4;
    default:                                  return 0;
    }
}
}
bool NrTemporalPass::initialize(ID3D12Device* device,ID3D12Resource* base,ID3D12Resource* motion,ID3D12Resource* output,
                                engine::NrAntiFlicker tier){
    close();base_=base;motion_=motion;output_=output;tier_=tier;
    if(!base||!motion||!output)return false;
    auto desc=base->GetDesc();width_=unsigned(desc.Width);height_=desc.Height;
    // The low-frequency route filters a half-resolution observation, so it needs
    // its own pair of half-extent textures. Other routes never allocate them and
    // bind the guide history into those slots instead: the shader cannot read
    // them outside route 4, so an unused binding is safe and costs nothing.
    const bool lowFrequency=routeOf(tier)==4;
    if(lowFrequency){
        lowWidth_=(width_+1)/2;lowHeight_=(height_+1)/2;
        lowResidual_=makeTexture(device,lowWidth_,lowHeight_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        lowGuide_=makeTexture(device,lowWidth_,lowHeight_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        if(!lowResidual_||!lowGuide_)return false;
    }else{lowWidth_=width_;lowHeight_=height_;}
    raw_=makeTexture(device,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    for(unsigned i=0;i<2;++i){history_[i]=makeTexture(device,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);guide_[i]=makeTexture(device,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);if(!history_[i]||!guide_[i])return false;}
    std::vector<uint8_t> shader;
    // 7 SRVs (base, raw, history, guide, motion, low residual, low guide), 3 UAVs,
    // 36 constants: original 24, Route/LowSize/pad, optional correction block.
    if(!raw_||!pass_.loadShader("NrTemporal.dxil",shader)||!pass_.create(device,shader,20,7,3,36))return false;
    if(lowFrequency){
        std::vector<uint8_t> reduce;
        // 3 SRVs + 2 UAVs = 5 slots per phase, two phases.
        if(!reducePass_.loadShader("NrTemporalReduce.dxil",reduce)||!reducePass_.create(device,reduce,10,3,2,8))return false;
        DescriptorStager reduceStaging;if(!reduceStaging.initialize(device,10))return false;
        for(unsigned i=0;i<2;++i){
            // Input and Base are the same image in this graph; the reduce shader
            // only needs the difference Raw - Base plus the guide.
            ID3D12Resource* sources[]={base,base,raw_.Get()};
            for(unsigned j=0;j<3;++j)reduceStaging.stageSrv(sources[j],nullptr,reducePass_.heap.Get(),i*5+j);
            makeUav(device,lowResidual_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(reducePass_,i*5+3));
            makeUav(device,lowGuide_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(reducePass_,i*5+4));
        }
    }
    DescriptorStager staging;if(!staging.initialize(device,20))return false;
    for(unsigned i=0;i<2;++i){
        ID3D12Resource* sources[]={base,raw_.Get(),history_[1-i].Get(),guide_[1-i].Get(),motion,
                                   lowFrequency?lowResidual_.Get():guide_[1-i].Get(),
                                   lowFrequency?lowGuide_.Get():guide_[1-i].Get()};
        for(unsigned j=0;j<7;++j)staging.stageSrv(sources[j],nullptr,pass_.heap.Get(),i*10+j);
        makeUav(device,output,DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(pass_,i*10+7));
        makeUav(device,history_[i].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(pass_,i*10+8));
        makeUav(device,guide_[i].Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpuHandleOf(pass_,i*10+9));
    }
    return true;
}
void NrTemporalPass::run(ID3D12GraphicsCommandList* list,StateTracker& tracker,bool reset,bool haveMotion,double frameMs,
                         float total,const engine::ProtectionSettings& protection,
                         const engine::NrCorrectionSettings& correction,bool hdr){
    const auto i=index_;
    // An interval outside the window is not a normal step: the history chain was
    // built on a different cadence, so it must not be blended across. Judging it
    // here (rather than only at the call site) keeps the invariant with the pass
    // that owns `valid_`, so a caller that forgets to reset cannot silently
    // reuse a stale chain. The threshold matches the graph's own timeline rule.
    const bool intervalSane=std::isfinite(frameMs)&&frameMs>0&&frameMs<=250;
    if(!intervalSane&&valid_)valid_=false;
    const bool useHistory=valid_&&!reset&&haveMotion&&intervalSane;
    // 80 ms past-history EMA; no future frames or presentation holdback.
    const float stability=correction.enabled?(correction.automatic?1.f:correction.stability):1.f;
    const float weight=useHistory?float(std::exp(-frameMs/80.0))*stability:0.f;
    const unsigned route=routeOf(tier_);
    if(route==4){
        // The reduce pass writes the *other* phase's half-resolution pair, which
        // the main pass then reads as its current observation.
        tracker.transition(list,base_,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker.transition(list,raw_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        for(auto* r:{lowResidual_.Get(),lowGuide_.Get()})tracker.transition(list,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float rc[8]={std::bit_cast<float>(width_),std::bit_cast<float>(height_),
                           std::bit_cast<float>(lowWidth_),std::bit_cast<float>(lowHeight_),
                           0.f,protection.enabled?1.f:0.f,0.f,0.f};
        reducePass_.bind(list,rc,gpuHandleOf(reducePass_,0).ptr,gpuHandleOf(reducePass_,3).ptr);
        list->Dispatch((lowWidth_+7)/8,(lowHeight_+7)/8,1);
        for(auto* r:{lowResidual_.Get(),lowGuide_.Get()}){tracker.uavBarrier(list,r);tracker.transition(list,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    }
    tracker.transition(list,raw_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list,base_,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list,motion_,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list,history_[1-i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list,guide_[1-i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(auto* r:{output_,history_[i].Get(),guide_[i].Get()})tracker.transition(list,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    float c[36]={std::bit_cast<float>(width_),std::bit_cast<float>(height_),weight,total,protection.enabled?1.f:0.f,protection.featherPixels,0,0};
    for(unsigned n=0;n<4;++n){const auto r=engine::protectionConstants(protection.regions[n]);c[8+n*4]=r[0];c[9+n*4]=r[1];c[10+n*4]=r[2];c[11+n*4]=r[3];}
    // Route and the low-frequency extent follow the region block. The shader
    // reads LowSize only on route 4 and the pad is never read: they exist to
    // keep the region offsets identical to the pre-tier layout.
    c[24]=std::bit_cast<float>(route);
    c[25]=std::bit_cast<float>(lowWidth_);
    c[26]=std::bit_cast<float>(lowHeight_);
    c[27]=0.f;
    const auto controls=correction.constants(hdr);
    std::copy(controls.begin(),controls.end(),c+28);
    pass_.bind(list,c,gpuHandleOf(pass_,i*10).ptr,gpuHandleOf(pass_,i*10+7).ptr);
    list->Dispatch((width_+7)/8,(height_+7)/8,1);
    for(auto* r:{output_,history_[i].Get(),guide_[i].Get()}){tracker.uavBarrier(list,r);tracker.transition(list,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    valid_=true;index_=1-i;
}
void NrTemporalPass::close(){
    pass_={};reducePass_={};raw_.Reset();
    for(auto& r:history_)r.Reset();
    for(auto& r:guide_)r.Reset();
    lowResidual_.Reset();
    lowGuide_.Reset();
    base_=motion_=output_=nullptr;width_=height_=lowWidth_=lowHeight_=index_=0;
    tier_=engine::NrAntiFlicker::Flow;valid_=false;
}
}
