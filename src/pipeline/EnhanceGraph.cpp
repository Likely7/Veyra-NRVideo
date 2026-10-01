#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/pipeline/HdrToneMap.h"
// EnhanceGraph implementation - the real GPU chain migrated from
// tools/player_probe main.cpp (R3.2). Ordering constraints preserved from the
// injected-layer era evidence: committed resources and NGX/NVOF objects are
// created BEFORE descriptor views; FG warm-up evaluate precedes views; static
// views are created last. Per-frame execution uses the shared command slot
// ring (NR evaluates on a fresh list - snippet constraint).
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/ColorLut.h"
#include "veyra/diagnostics/CpuStallTrace.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/DlssFgBackend.h"
#include "veyra/ngx/FgCompatibilitySession.h"
#include "veyra/ngx/AdaMfgUnlock.h"
#include "veyra/ngx/AmpereMfgUnlock.h"
#include "veyra/ngx/NvapiArchSpoof.h"
#include "veyra/gfx/FsrSrBackend.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/DlssSrBackend.h"
#include "veyra/ngx/VideoSrBackend.h"
#include "veyra/ngx/TrueHdrBackend.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/NvOfSession.h"
#include "veyra/guidance/AmdOpticalFlow.h"
#include "veyra/guidance/GpuDisOpticalFlow.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext_d3d12va.h>
#include <libswscale/swscale.h>
}

namespace veyra::pipeline {
using diagnostics::GpuStage;

namespace {

constexpr int64_t usPerSecond = 1000000;
float uintBits(uint32_t v) { return std::bit_cast<float>(v); }

// N1 packed capture ingress helpers. Codes match pipeline::packedInputCode:
// 1 BGR24, 2 RGB555, 3 RGB565, 4 UYVY, 5 YVYU.
unsigned packedIngressRowBytes(uint32_t code,unsigned width){
    return code==1?width*3:width*2;
}
unsigned packedIngressTexels(uint32_t code,unsigned width){
    return (packedIngressRowBytes(code,width)+3)/4;
}
AVPixelFormat packedIngressFormat(uint32_t code){
    switch(code){
    case 1:return AV_PIX_FMT_BGR24;
    case 2:return AV_PIX_FMT_RGB555LE;
    case 3:return AV_PIX_FMT_RGB565LE;
    case 4:return AV_PIX_FMT_UYVY422;
    case 5:return AV_PIX_FMT_YVYU422;
    default:break;
    }
    return AV_PIX_FMT_NONE;
}
// Coarse luma sample for scene analysis; mirrors the CPU luma weights.
uint8_t packedIngressLuma(const AVFrame& frame,uint32_t code,unsigned x,unsigned y){
    const auto* p=frame.data[0]+ptrdiff_t(y)*frame.linesize[0];
    switch(code){
    case 1:{const auto* q=p+x*3;return uint8_t((54*unsigned(q[2])+183*unsigned(q[1])+19*unsigned(q[0])+128)>>8);}
    case 2:{const unsigned w=unsigned(p[x*2])|(unsigned(p[x*2+1])<<8);
        const unsigned r=(w>>10)&31,g=(w>>5)&31,b=w&31;return uint8_t((54*(r<<3|r>>2)+183*(g<<3|g>>2)+19*(b<<3|b>>2)+128)>>8);}
    case 3:{const unsigned w=unsigned(p[x*2])|(unsigned(p[x*2+1])<<8);
        const unsigned r=(w>>11)&31,g=(w>>5)&63,b=w&31;return uint8_t((54*(r<<3|r>>2)+183*(g<<2|g>>4)+19*(b<<3|b>>2)+128)>>8);}
    case 4:return p[x*2+1]; // UYVY: Y0 follows U
    case 5:return p[x*2];   // YVYU: Y0 leads
    default:break;
    }
    return 0;
}

} // namespace

EnhanceGraph::EnhanceGraph(gfx::D3D12DeviceContext& context, gfx::CommandSlotRing& ring)
    : context_(context)
    , ring_(ring)
{
}

EnhanceGraph::~EnhanceGraph()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// initialize: exact ordering of the proven probe sequence.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Colour grade (plan v4): the tables are baked on the CPU, uploaded into small
// FP32 textures and read by the ingest shaders. Nothing here allocates, copies
// or dispatches when desc.color.enabled is false, so the "off" path costs zero.
// ---------------------------------------------------------------------------
namespace {
// Settings carry the LUT name as UTF-16; log it as UTF-8 (never narrow
// character by character, and never build a range from two temporaries).
std::string utf8Of(const std::wstring& text){
    if(text.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return "<invalid>";
    std::string out(std::size_t(size),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
ComPtr<ID3D12Resource> makeColorTable(ID3D12Device* device,uint32_t width){
    return makeTexture(device,width,1,DXGI_FORMAT_R32G32B32A32_FLOAT,false);
}
ComPtr<ID3D12Resource> makeColorLut3D(ID3D12Device* device,uint32_t size){
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    td.Width=size;td.Height=size;td.DepthOrArraySize=UINT16(size);td.MipLevels=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> r;
    if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r))))return {};
    return r;
}
}
bool EnhanceGraph::createColorResources(){
    if(colorCurveTex_&&colorHueTex_&&colorLumTex_&&colorLutTex_)return true;
    colorCurveTex_=makeColorTable(context_.device(),ColorGradeTables::kCurveEntries);
    colorHueTex_=makeColorTable(context_.device(),ColorGradeTables::kHueEntries);
    colorLumTex_=makeColorTable(context_.device(),ColorGradeTables::kLumEntries);
    // 1x1x1 placeholder keeps the descriptor valid while no LUT is loaded;
    // lutStrength stays 0 in that case, so it is never sampled.
    colorLutTex_=makeColorLut3D(context_.device(),1);
    colorLutSize_=0;
    upColorCurve_=makeUploadBuffer(context_.device(),sizeof(float)*4*ColorGradeTables::kCurveEntries);
    upColorHue_=makeUploadBuffer(context_.device(),sizeof(float)*4*ColorGradeTables::kHueEntries);
    upColorLum_=makeUploadBuffer(context_.device(),sizeof(float)*4*ColorGradeTables::kLumEntries);
    if(!colorCurveTex_||!colorHueTex_||!colorLumTex_||!colorLutTex_||!upColorCurve_||!upColorHue_||!upColorLum_){
        veyra::log::error("color-grade",std::format("colour resources unavailable tables={} lut={}",colorCurveTex_?1:0,colorLutTex_?1:0));
        return false;
    }
    if(FAILED(upColorCurve_->Map(0,nullptr,reinterpret_cast<void**>(&mappedColorCurve_)))||
       FAILED(upColorHue_->Map(0,nullptr,reinterpret_cast<void**>(&mappedColorHue_)))||
       FAILED(upColorLum_->Map(0,nullptr,reinterpret_cast<void**>(&mappedColorLum_))))return false;
    return true;
}
// Product default: dither exactly one LSB of whatever the graph is writing while
// the colour grade is active, and nothing at all when it is not - every ungraded
// output path therefore stays byte-identical to the pre-colour build.
float EnhanceGraph::outputDitherStep() const{
    if(desc_.outputDitherStep>=0.0f)return desc_.outputDitherStep;
    // A master switch that is on but neutral renders exactly like no grading at
    // all (contract), so it must not add noise either - only a real grade does.
    if(!colorActive_||colorTables_.identity)return 0.0f;
    if(hdr10Output())return 1.0f/1023.0f;
    if(desc_.hdrOutput)return 0.0f;   // FP16 target: nothing to quantise
    return 1.0f/255.0f;
}
void EnhanceGraph::refreshColorTables(){
    colorTables_=ColorGradeTables::bake(desc_.color);
    if(!colorActive_||!mappedColorCurve_||!mappedColorHue_||!mappedColorLum_)return;
    std::copy(colorTables_.curve.begin(),colorTables_.curve.end(),mappedColorCurve_);
    std::copy(colorTables_.hue.begin(),colorTables_.hue.end(),mappedColorHue_);
    std::copy(colorTables_.lum.begin(),colorTables_.lum.end(),mappedColorLum_);
    colorDirty_=true;
}
void EnhanceGraph::uploadColorTables(ID3D12GraphicsCommandList* list){
    if(!colorDirty_)return;
    auto copyTable=[&](ID3D12Resource* dst,ID3D12Resource* src,UINT width){
        tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION d{},s{};
        d.pResource=dst;d.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d.SubresourceIndex=0;
        s.pResource=src;s.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        s.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,width,1,1,width*16};
        list->CopyTextureRegion(&d,0,0,0,&s,nullptr);
        tracker_.transition(list,dst,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    copyTable(colorCurveTex_.Get(),upColorCurve_.Get(),ColorGradeTables::kCurveEntries);
    copyTable(colorHueTex_.Get(),upColorHue_.Get(),ColorGradeTables::kHueEntries);
    copyTable(colorLumTex_.Get(),upColorLum_.Get(),ColorGradeTables::kLumEntries);
    if(colorLutPending_&&!colorLutUpload_.empty()){
        const unsigned size=colorLutPending_;
        // D3D12 requires the copy source row pitch to be 256-byte aligned. A
        // tight size*16 pitch is invalid for every LUT size that is not a
        // multiple of 16 (2, 3, ... 17, 33, ... - most creative LUTs included),
        // and an invalid footprint lets the driver read/write past the staging
        // buffer. Measured symptom: a 2-cube upload crashed the next D3D12 call
        // with a fail-fast in the host process. Pad rows instead.
        constexpr unsigned kRowPitchAlignment=256;
        const unsigned rowPitch=(size*16u+kRowPitchAlignment-1)/kRowPitchAlignment*kRowPitchAlignment;
        const uint64_t bytes=uint64_t(rowPitch)*size*size;
        auto& staging=colorLutStaging_[colorLutStagingSlot_];
        colorLutStagingSlot_=(colorLutStagingSlot_+1)%colorLutStaging_.size();
        staging=makeUploadBuffer(context_.device(),bytes);
        void* mapped=nullptr;
        if(staging&&SUCCEEDED(staging->Map(0,nullptr,&mapped))&&mapped){
            for(unsigned z=0;z<size;++z)for(unsigned y=0;y<size;++y){
                auto* dst=reinterpret_cast<float*>(static_cast<uint8_t*>(mapped)+std::size_t(z)*rowPitch*size+std::size_t(y)*rowPitch);
                for(unsigned x=0;x<size;++x){
                    const std::size_t i=(std::size_t(z)*size+y)*size+x;
                    dst[x*4+0]=colorLutUpload_[i*3+0];
                    dst[x*4+1]=colorLutUpload_[i*3+1];
                    dst[x*4+2]=colorLutUpload_[i*3+2];
                    dst[x*4+3]=1.0f;
                }
            }
            staging->Unmap(0,nullptr);
            tracker_.transition(list,colorLutTex_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION d{},s{};
            d.pResource=colorLutTex_.Get();d.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d.SubresourceIndex=0;
            s.pResource=staging.Get();s.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            s.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,size,size,size,rowPitch};
            list->CopyTextureRegion(&d,0,0,0,&s,nullptr);
            tracker_.transition(list,colorLutTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }else{
            veyra::log::warn("color-grade","lut staging buffer unavailable; keeping the previous table");
        }
        colorLutUpload_.clear();
        colorLutPending_=0;
    }
    colorDirty_=false;
}
bool EnhanceGraph::setColorLut(const float* rgb,unsigned size){
    if(!colorActive_||size<2||size>64)return false;
    if(!createColorResources())return false;
    colorLutTex_=makeColorLut3D(context_.device(),size);
    if(!colorLutTex_)return false;
    colorLutSize_=size;
    colorLutUpload_.assign(rgb,rgb+std::size_t(size)*size*size*3);
    colorLutPending_=size;
    colorDirty_=true;
    // Callers must have drained the queue: the descriptor is re-staged in place.
    D3D12_SHADER_RESOURCE_VIEW_DESC lutSrv{};
    lutSrv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    lutSrv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;
    lutSrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lutSrv.Texture3D.MipLevels=1;
    for(auto* pass:{&yuvPass_,&rgbPass_})if(pass->heap)stager_.stageSrv(colorLutTex_.Get(),&lutSrv,pass->heap.Get(),11);
    return true;
}

bool EnhanceGraph::initialize(const EnhanceGraphDesc& desc)
{
    failedBackend_=engine::FailedBackend::Infrastructure;
    if(!desc.protection.validate().empty())return false;
    if(desc.compatibilityPreflight&&!desc.compatibilityPreflight(desc,context_)){
        failedBackend_=engine::FailedBackend::Fg;
        veyra::log::error("graph","FG compatibility preflight failed; see fg-probe log");return false;
    }
    if(desc.fgMotionProbe){
        const auto d=desc.fgMotionProbe->GetDesc();ComPtr<ID3D12Device> device;
        if(!desc.enableFg||desc.noFeatures||desc.noNgx||d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=desc.workWidth||d.Height!=desc.workHeight||d.Format!=DXGI_FORMAT_R16G16_FLOAT||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||FAILED(desc.fgMotionProbe->GetDevice(IID_PPV_ARGS(&device)))||device.Get()!=context_.device())return false;
    }
    if(desc.srMotionProbe){
        const auto d=desc.srMotionProbe->GetDesc();ComPtr<ID3D12Device> device;
        if(!desc.enableSr||desc.noFeatures||desc.noNgx||d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=desc.workWidth||d.Height!=desc.workHeight||d.Format!=DXGI_FORMAT_R16G16_FLOAT||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||FAILED(desc.srMotionProbe->GetDevice(IID_PPV_ARGS(&device)))||device.Get()!=context_.device()){
            veyra::log::error("graph","invalid diagnostic SR motion resource/feature/device contract");return false;
        }
        veyra::log::info("graph","diagnostic SR motion override active; no product entry point enables this");
    }
    desc_ = desc;tracker_={};prevValid_=false;fgHistorySkipped_=false;cadence_.reset();scene_.reset();previousLuma_.clear();
    if(desc.videoSrQuality>engine::kVideoSrFsr){veyra::log::error("graph",std::format("invalid video SR quality value={}",desc.videoSrQuality));return false;}
    srcW_ = desc.sourceWidth;
    srcH_ = desc.sourceHeight;
    workW_ = desc.workWidth;
    workH_ = desc.workHeight;
    nrW_=desc.nrWidth?desc.nrWidth:workW_; nrH_=desc.nrHeight?desc.nrHeight:workH_;
    if((desc.flowWidth==0)!=(desc.flowHeight==0)){veyra::log::error("resolution","partial flow extent is invalid");return false;}
    nvofW_ = desc.flowWidth?desc.flowWidth:srcW_;
    nvofH_ = desc.flowHeight?desc.flowHeight:srcH_;
    if(!Extent{srcW_,srcH_}.valid()||!Extent{workW_,workH_}.valid()){
        veyra::log::error("resolution","source/output exceeds D3D12 texture dimensions or is empty");return false;
    }
    if(!Extent{nrW_,nrH_}.valid()||nrW_>workW_||nrH_>workH_){veyra::log::error("resolution","invalid NR extent");return false;}
    if(!Extent{nvofW_,nvofH_}.valid()||nvofW_>srcW_||nvofH_>srcH_){veyra::log::error("resolution","invalid source-space flow extent");return false;}
    diagnostics::DiagnosticEvent initDiagnostic;initDiagnostic.stage="initialize";initDiagnostic.identity={epoch_+1,desc.settingsRevision,0};initDiagnostic.resolution.source={srcW_,srcH_};initDiagnostic.resolution.base=initDiagnostic.resolution.fg=initDiagnostic.resolution.output={workW_,workH_};initDiagnostic.resolution.nr={nrW_,nrH_};initDiagnostic.resolution.flow={nvofW_,nvofH_};initDiagnostic.flowApplied=std::to_string(unsigned(desc.flowQuality));initDiagnostic.runtimeHash="unverified-user-replaceable";Logger::diagnosticContext(initDiagnostic);
    veyra::log::info("resolution",std::format("source={}x{} base={}x{} nr={}x{} flow={}x{} fg={}x{} output={}x{}",srcW_,srcH_,workW_,workH_,nrW_,nrH_,nvofW_,nvofH_,workW_,workH_,workW_,workH_));
    veyra::log::info("pipeline-order",desc.nrBeforeSr?"NR -> residual(source) -> SR -> FG (experimental preview)":"SR -> NR -> residual -> FG");
    if(!desc.videoHdr.valid()||(desc.hdrOutput&&!desc.hdrInput&&!desc.convertVideoHdr())){veyra::log::error("hdr","HDR output requires native HDR input or explicit Video HDR conversion");return false;}
    srEnabled_ = desc.enableSr && (srcW_ != workW_ || srcH_ != workH_);
    nrEnabled_ = desc.enableNr && !desc.noFeatures && !desc.noNgx;
    fgEnabled_ = desc.enableFg && !desc.noNgx && !desc.stillImage && desc.frameGenerationBackend!=engine::FrameGenerationBackend::XeSS
        && desc.frameGenerationBackend!=engine::FrameGenerationBackend::Fsr;
    nvofStandalone_ = desc.enableNvofStandalone && !desc.noFeatures;
    if(desc.opticalFlowBackend==engine::OpticalFlowBackend::AmdFidelityFx&&desc.amdFlowHalfResolution){nvofW_=std::max(1u,nvofW_/2);nvofH_=std::max(1u,nvofH_/2);}
    uint64_t budget=0,usage=0;
    const uint64_t temporalBytes=desc_.nrTemporal?uint64_t(desc_.nrBeforeSr?srcW_:workW_)*(desc_.nrBeforeSr?srcH_:workH_)*40:0;
    const uint64_t estimate=uint64_t(workW_)*workH_*(fgEnabled_?100:76)+uint64_t(nrW_)*nrH_*32+uint64_t(srcW_)*srcH_*32+temporalBytes;
    if(context_.videoMemoryInfo(budget,usage)){
        veyra::log::info("memory",std::format("graph lower-bound={}MiB budget={}MiB usage={}MiB; SDK allocations additional",estimate>>20,budget>>20,usage>>20));
        if(usage>=budget||estimate>budget-usage){veyra::log::error("memory","insufficient adapter budget for requested graph textures");return false;}
    }
    Status st = Status::Ok;

    lumaPitch_ = (static_cast<size_t>(srcW_)*(desc_.wideYuvInput()?2:1) + 255) & ~size_t(255);
    chromaPitch_ = lumaPitch_;
    lumaSize_ = lumaPitch_ * srcH_;
    chromaSize_ = chromaPitch_ * ((srcH_ + 1) / 2);
    dPitch_ = (static_cast<size_t>(workW_) * 4 + 255) & ~size_t(255);
    const size_t dSize = dPitch_ * workH_;
    nv12Buf_.resize(lumaSize_ + chromaSize_);

    if(!gpuTimer_.initialize(context_.device(),context_.directQueue()))veyra::log::warn("gpu-timestamp","GPU timing unavailable");
    if (!createResources()) return false;
    if (!initZeroAndDepthTextures()) return false;
    if (!initNvof()) { failedBackend_=engine::FailedBackend::OpticalFlow; return false; }
    if (!initFsrSr()) return false;
    if(nrEnabled_&&GetEnvironmentVariableW(L"VEYRA_TEST_NR_INIT_FAILURE",nullptr,0)){
        failedBackend_=engine::FailedBackend::Nr;
        veyra::log::error("backend-recovery-test","test-only NR initialization rejection before SDK call; not a hardware failure");return false;
    }
    if (!initNgxFeatures()) return false;
    failedBackend_=engine::FailedBackend::Infrastructure;
    if (!createComputePasses()) return false;

    initialized_ = true;
    failedBackend_=engine::FailedBackend::None;
    veyra::log::info("graph", std::format("initialized src={}x{} work={}x{} sr={} nr={} fg={} nvof={}",
        srcW_, srcH_, workW_, workH_, srEnabled_ ? 1 : 0, nrEnabled_ ? 1 : 0,
        fgEnabled_ ? 1 : 0, (nvof_ && nvof_->initialized()) ? 1 : 0));
    return true;
}

bool EnhanceGraph::createResources()
{
    fgDisableInit_=makeUploadBuffer(context_.device(),4);
    if(!fgDisableInit_)return false;
    void* initial=nullptr;if(FAILED(fgDisableInit_->Map(0,nullptr,&initial)))return false;
    *static_cast<uint32_t*>(initial)=1;fgDisableInit_->Unmap(0,nullptr);
    for(unsigned i=0;i<kGeneratedPoolSlots;++i){
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=4;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr=context_.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&fgDisable_[i]));
        if(FAILED(hr)){veyra::log::error("fg-status",std::format("allocate UAV hr=0x{:X}",unsigned(hr)));return false;}
        hp.Type=D3D12_HEAP_TYPE_READBACK;bd.Flags=D3D12_RESOURCE_FLAG_NONE;
        hr=context_.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&fgDisableReadback_[i]));
        if(FAILED(hr)){veyra::log::error("fg-status",std::format("allocate readback hr=0x{:X}",unsigned(hr)));return false;}
    }
    // NV12 CPU upload keeps UPLOAD-heap BUFFERS (upload-heap textures are
    // size-limited) with persistent mapping, but the GPU ingestion is now a
    // PLACED_FOOTPRINT CopyTextureRegion instead of the retired RAW-buffer
    // SRV dispatch - the RAW SRV was the one descriptor kind that tripped the
    // injected layer (device removal / blocked NVOF; r33b evidence 2026-09-06).
    upLuma_[0] = makeUploadBuffer(context_.device(), lumaSize_);
    upLuma_[1] = makeUploadBuffer(context_.device(), lumaSize_);
    upChroma_[0] = makeUploadBuffer(context_.device(), chromaSize_);
    upChroma_[1] = makeUploadBuffer(context_.device(), chromaSize_);
    upDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroDepth_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    upZeroMotion_ = makeUploadBuffer(context_.device(), dPitch_ * workH_);
    lumaTex_ = makeTexture(context_.device(), srcW_, srcH_, desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM, true);
    chromaTex_ = makeTexture(context_.device(), (srcW_+1) / 2, (srcH_+1) / 2, desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM, true);
    if(desc_.rgbInput||desc_.yuy2Input||desc_.packedInput){
        if((desc_.yuy2Input||desc_.packedInput==4||desc_.packedInput==5)&&srcW_%2)return false;
        if(desc_.rgbInput&&(desc_.yuy2Input||desc_.packedInput))return false;
        const unsigned packedWidth=desc_.packedInput?packedIngressTexels(desc_.packedInput,srcW_):desc_.yuy2Input?srcW_/2:srcW_;
        rgbPitch_=(size_t(packedWidth)*4+255)&~size_t(255);
        rgbTex_=makeTexture(context_.device(),packedWidth,srcH_,DXGI_FORMAT_R8G8B8A8_UNORM,false);
        if(!rgbTex_)return false;
        for(unsigned i=0;i<2;++i){upRgb_[i]=makeUploadBuffer(context_.device(),rgbPitch_*srcH_);
            if(!upRgb_[i]||FAILED(upRgb_[i]->Map(0,nullptr,reinterpret_cast<void**>(&mappedRgb_[i]))))return false;
        }
    }
    srcRgba_ = makeTexture(context_.device(), srcW_, srcH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    if(desc_.convertVideoHdr()){
        videoHdrInput_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        videoHdrOutput_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        if(!videoHdrInput_||!videoHdrOutput_)return false;
    }
    if(srEnabled_&&desc_.videoSrQuality&&desc_.videoSrQuality!=engine::kVideoSrFsr){videoSrInput_=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R8G8B8A8_UNORM,true);videoSrOutput_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R8G8B8A8_UNORM,true);if(!videoSrInput_||!videoSrOutput_)return false;}
    // FSR upscaling needs a render-extent depth; Veyra has no source-resolution
    // depth source, so this is the same explicit constant far depth the
    // frame-generation path uses. It limits disocclusion quality and must not
    // be described as engine-native depth.
    if(srEnabled_&&desc_.videoSrQuality==engine::kVideoSrFsr){
        fsrSrDepth_=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R32_FLOAT,false);
        if(!fsrSrDepth_)return false;
        fsrSrDepthPitch_=size_t(srcW_)*sizeof(float);
        upFsrSrDepth_=makeUploadBuffer(context_.device(),fsrSrDepthPitch_*srcH_);
        if(!upFsrSrDepth_)return false;
    }
    const bool directBase=!srEnabled_&&srcW_==workW_&&srcH_==workH_;
    workRgba_=directBase?srcRgba_:makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    for(unsigned i=0;i<2;++i){sourceReferences_[i]=makeTexture(context_.device(),srcW_,srcH_,DXGI_FORMAT_R16G16B16A16_FLOAT,false);baseReferences_[i]=(directBase&&!desc_.color.enabled)?sourceReferences_[i]:makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,false);if(!sourceReferences_[i]||!baseReferences_[i])return false;}
    nrInput_=(desc_.nrBeforeSr&&nrW_==srcW_&&nrH_==srcH_)?srcRgba_:(nrW_==workW_&&nrH_==workH_)?workRgba_:makeTexture(context_.device(),nrW_,nrH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    residualRgba_=makeTexture(context_.device(),desc_.nrBeforeSr?srcW_:workW_,desc_.nrBeforeSr?srcH_:workH_,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    nrFlow_=makeTexture(context_.device(),nrW_,nrH_,DXGI_FORMAT_R16G16_FLOAT,true);
    baseFlow_=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16_FLOAT,true);
    if(presentSinkFg())for(auto& motion:presentMotion_){motion=makeTexture(context_.device(),workW_,workH_,DXGI_FORMAT_R16G16_FLOAT,false);if(!motion)return false;}
    if(!nrInput_||!residualRgba_||!nrFlow_||!baseFlow_)return false;
    // Colour grade tables (v4): only allocated when the stage is enabled.
    colorActive_=desc_.color.enabled;
    if(colorActive_){
        if(!createColorResources())return false;
        refreshColorTables();
        // Load the optional .cube the settings reference; a LUT that cannot be
        // resolved disables the lookup instead of sampling a placeholder.
        if(desc_.color.hasLut()){
            // Plan v5.2: the declared input space must match the content domain.
            // A display-referred (sRGB) LUT on HDR content, or a PQ LUT on SDR
            // content, is refused here instead of being applied silently with
            // values that mean something else than the LUT author assumed.
            const bool hdrContent=desc_.hdrInput;
            const int space=desc_.color.lutInputSpace;
            const bool mismatch=hdrContent?(space==engine::ColorSettings::kLutInputSrgb):(space==engine::ColorSettings::kLutInputPq);
            if(mismatch){
                colorLutNotice_=hdrContent?L"LUT disabled: HDR content cannot use the sRGB display-reference input space (switch to Cineon Log or PQ)"
                                        :L"LUT disabled: SDR content cannot use the PQ input space (switch to Cineon Log or sRGB display reference)";
                desc_.color.lutStrength=0.0f;
                refreshColorTables();
                veyra::log::warn("color-grade",std::format("lut input space rejected space={} hdrContent={} name bytes={} (grade continues without the lookup)",space,hdrContent?1:0,desc_.color.lutNameString().size()));
            }else{
                colorLutNotice_.clear();
                engine::ColorLutData lut;
                const engine::ColorLutStore store(runtime::localDataDirectory());
                if(store.resolve(desc_.color.lutNameString(),lut)&&lut.valid()&&setColorLut(lut.rgb.data(),unsigned(lut.size))){
                    // The name must be converted once: building the log argument
                    // from lutNameString().begin() and .end() created two
                    // different temporaries, so the range constructor computed a
                    // bogus distance and read past both buffers (crash in the LUT
                    // load path, reproduced by the GPU contract test).
                    veyra::log::info("color-grade",std::format("lut loaded name={} size={}",utf8Of(desc_.color.lutNameString()),lut.size));
                }else{
                    desc_.color.lutStrength=0.0f;
                    refreshColorTables();
                    veyra::log::warn("color-grade","referenced lut unavailable; colour grade continues without it");
                }
            }
        }
        veyra::log::info("color-grade",std::format("stage enabled tables={}/{}/{} lutInputSpace={} lut={}",
            ColorGradeTables::kCurveEntries,ColorGradeTables::kHueEntries,ColorGradeTables::kLumEntries,
            desc_.color.lutInputSpace,desc_.color.hasLut()?1:0));
    }
    proxyTex_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    neuralTex_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    finalRgba_ = makeTexture(context_.device(), nrW_, nrH_, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    videoFrame_[0] = makeTexture(context_.device(), workW_, workH_, outputFormat(), true);
    videoFrame_[1] = makeTexture(context_.device(), workW_, workH_, outputFormat(), true);
    confTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R8_UNORM, true);
    flowTex_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_R16G16_FLOAT, true);
    depthTex_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    // Valid placeholder descriptors keep disabled FG inexpensive. Enabling it
    // is a graph rebuild, so no in-flight descriptor is resized in place.
    for(auto& frame:genFrame_){frame=makeTexture(context_.device(),fgEnabled_?workW_:1,fgEnabled_?workH_:1,outputFormat(),true);if(!frame)return false;}
    nrZeroMotion_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R16G16_FLOAT, false);
    nrZeroDepth_ = makeTexture(context_.device(), workW_, workH_, DXGI_FORMAT_R32_FLOAT, false);
    rawW_ = (nvofW_ + nvofGrid_ - 1) / nvofGrid_;
    rawH_ = (nvofH_ + nvofGrid_ - 1) / nvofGrid_;
    nvofRawTex_ = makeTexture(context_.device(), rawW_, rawH_, DXGI_FORMAT_R16G16_SINT, false);
    nvofCostTex_ = makeTexture(context_.device(), rawW_, rawH_, DXGI_FORMAT_R8_UINT, false);
    nvofInA_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_B8G8R8A8_UNORM, true);
    nvofInB_ = makeTexture(context_.device(), nvofW_, nvofH_, DXGI_FORMAT_B8G8R8A8_UNORM, true);

    if (!upLuma_[0] || !upLuma_[1] || !upChroma_[0] || !upChroma_[1] ||
        !upDepth_ || !upZeroDepth_ || !upZeroMotion_ ||
        !lumaTex_ || !chromaTex_ || !srcRgba_ || !workRgba_ || !proxyTex_ ||
        !neuralTex_ || !finalRgba_ || !videoFrame_[0] || !videoFrame_[1] ||
        !flowTex_ || !depthTex_ || !genFrame_[0] || !genFrame_[1] ||
        !nrZeroMotion_ || !nrZeroDepth_ || !nvofRawTex_ || !nvofCostTex_ ||
        !nvofInA_ || !nvofInB_ || !confTex_) {
        veyra::log::error("graph", "resource allocation failed");
        return false;
    }

    // Persistent mapping of the NV12 upload ring (before any views).
    bool uploadsMapped = true;
    for (int i = 0; i < 2; ++i) {
        if (FAILED(upLuma_[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedLuma_[i]))) ||
            FAILED(upChroma_[i]->Map(0, nullptr, reinterpret_cast<void**>(&mappedChroma_[i])))) {
            uploadsMapped = false;
        }
    }
    if (!uploadsMapped) {
        veyra::log::error("graph", "NV12 upload persistent map failed");
        return false;
    }
    veyra::log::info("graph", "NV12 upload ring persistently mapped (2 buffer sets)");
    return true;
}

bool EnhanceGraph::initZeroAndDepthTextures()
{
    // Depth constants uploaded once (copies are safe before views exist).
    uint8_t* d = nullptr; uint8_t* zd = nullptr; uint8_t* zm = nullptr;
    uint8_t* fd = nullptr;
    upDepth_->Map(0, nullptr, reinterpret_cast<void**>(&d));
    upZeroDepth_->Map(0, nullptr, reinterpret_cast<void**>(&zd));
    upZeroMotion_->Map(0, nullptr, reinterpret_cast<void**>(&zm));
    if(upFsrSrDepth_!=nullptr)upFsrSrDepth_->Map(0,nullptr,reinterpret_cast<void**>(&fd));
    for (uint32_t y = 0; y < workH_; ++y) {
        float* dRow = reinterpret_cast<float*>(d + y * dPitch_);
        float* zdRow = reinterpret_cast<float*>(zd + y * dPitch_);
        uint16_t* zmRow = reinterpret_cast<uint16_t*>(zm + y * dPitch_);
        for (uint32_t x = 0; x < workW_; ++x) {
            dRow[x] = 0.9f;  // explicit constant far depth (video content)
            zdRow[x] = 0.5f; // NR zero-depth explicit fallback
            zmRow[x * 2] = 0; zmRow[x * 2 + 1] = 0;
        }
    }
    upDepth_->Unmap(0, nullptr);
    upZeroDepth_->Unmap(0, nullptr);
    upZeroMotion_->Unmap(0, nullptr);
    if(upFsrSrDepth_!=nullptr){
        for(uint32_t y=0;y<srcH_;++y){
            float* row=reinterpret_cast<float*>(fd+size_t(y)*fsrSrDepthPitch_);
            for(uint32_t x=0;x<srcW_;++x)row[x]=0.9f;
        }
        upFsrSrDepth_->Unmap(0,nullptr);
    }

    Status st = Status::Ok;
    ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
    if (list == nullptr) return false;
    auto uploadTex = [&](ID3D12Resource* tex, const ComPtr<ID3D12Resource>& up, DXGI_FORMAT fmt) {
        D3D12_RESOURCE_BARRIER b{};
        b.Transition.pResource = tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = up.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = fmt;
        src.PlacedFootprint.Footprint.Width = workW_;
        src.PlacedFootprint.Footprint.Height = workH_;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(dPitch_);
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER back{};
        back.Transition.pResource = tex;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &back);
    };
    uploadTex(depthTex_.Get(), upDepth_, DXGI_FORMAT_R32_FLOAT);
    uploadTex(nrZeroDepth_.Get(), upZeroDepth_, DXGI_FORMAT_R32_FLOAT);
    uploadTex(nrZeroMotion_.Get(), upZeroMotion_, DXGI_FORMAT_R16G16_FLOAT);
    if(fsrSrDepth_!=nullptr&&upFsrSrDepth_!=nullptr){
        D3D12_RESOURCE_BARRIER b{};
        b.Transition.pResource=fsrSrDepth_.Get();
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};
        dst.pResource=fsrSrDepth_.Get();
        dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upFsrSrDepth_.Get();
        src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R32_FLOAT;
        src.PlacedFootprint.Footprint.Width=srcW_;
        src.PlacedFootprint.Footprint.Height=srcH_;
        src.PlacedFootprint.Footprint.Depth=1;
        src.PlacedFootprint.Footprint.RowPitch=static_cast<UINT>(fsrSrDepthPitch_);
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1,&b);
    }
    list->Close();
    ID3D12CommandList* lists[] = { list };
    context_.directQueue()->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> initFence;
    context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initFence));
    context_.directQueue()->Signal(initFence.Get(), 1);
    if (nvofOutEvent_ == nullptr) {
        nvofOutEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    initFence->SetEventOnCompletion(1, nvofOutEvent_);
    WaitForSingleObject(nvofOutEvent_, 5000);
    veyra::log::info("graph", "depth/zero guidance textures initialized");
    return true;
}

bool EnhanceGraph::initNvof()
{
    if(desc_.stillImage){mvecSource_="single-image (no temporal motion)";return true;}
    if (desc_.noFeatures || !(nvofStandalone_ || presentSinkFg() || (fgEnabled_&&desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss) || (srEnabled_&&(desc_.videoSrQuality==0||desc_.videoSrQuality==engine::kVideoSrFsr)))) {
        veyra::log::info("graph", "VEYRA_NO_FEATURES: NVOF session skipped");
        return true;
    }
    if(desc_.opticalFlowBackend==engine::OpticalFlowBackend::GpuDis){
        gpuDis_=std::make_unique<guidance::GpuDisOpticalFlow>();
        if(!gpuDis_->initialize(context_.device(),nvofW_,nvofH_))return false;
        mvecSource_="gpu-dis FAST + bidirectional/photometric confidence";
        return true;
    }
    if(desc_.opticalFlowBackend==engine::OpticalFlowBackend::AmdFidelityFx){
        amdOf_=std::make_unique<guidance::AmdOpticalFlow>();
        if(!amdOf_->initialize(context_.device(),nvofW_,nvofH_))return false;
        nvofRawTex_=amdOf_->vectors();rawW_=amdOf_->width();rawH_=amdOf_->height();selectedGrid_=8;
        mvecSource_="amd-fidelityfx + photometric confidence";
        return true;
    }
    nvof_ = std::make_unique<ngx::NvOfSession>();
    if (nvofOutFence_ == nullptr) {
        if (FAILED(context_.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&nvofOutFence_)))) {
            veyra::log::error("graph", "NVOF out fence creation failed");
            return false;
        }
    }
    if (nvofOutEvent_ == nullptr) {
        nvofOutEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ngx::NvOfSession::Desc nd{};
    nd.width = nvofW_; nd.height = nvofH_;
    nd.inFence = context_.fence();
    nd.outFence = nvofOutFence_.Get();
    nd.gridSize = 4;nd.quality=uint32_t(desc_.flowQuality);
    // s6 contract: B8G8R8A8 inputs, raw R16G16_SINT + R8_UINT cost at grid
    // extent, passed explicitly; allocation failure = fail closed.
    Status st = Status::Ok;
    if (!nvof_->initialize(context_.device(), nvofInA_.Get(), nvofInB_.Get(),
            nvofRawTex_.Get(), nvofCostTex_.Get(), nd, st)) {
        veyra::log::error("graph", "NVOF init failed");
        return false;
    }
    selectedGrid_ = nvof_->caps().selectedGrid;
    return true;
}

bool EnhanceGraph::fsrSrEnabled() const
{
    return fsrSrBackend_ != nullptr && fsrSrBackend_->created();
}

void EnhanceGraph::applyAdaMfgUnlock()
{
    const auto& adapter = context_.adapter();
    // Hard architecture gate: 50 series keeps its native multi-frame path and is
    // never patched, and no other architecture is in scope either.
    // VEYRA_TEST_FORCE_ADA_UNLOCK runs the same patch on a non-Ada host so the
    // edit itself can be checked without 40-series hardware; never a product path.
    wchar_t forced[2]{};
    const bool forceOnAnyAdapter=GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_ADA_UNLOCK",forced,2)>0;
    if (!forceOnAnyAdapter&&!ngx::AdaMfgUnlock::adapterIsAda(adapter.vendorId, adapter.deviceId)) {
        return;
    }
    wchar_t disabled[2]{};
    if (GetEnvironmentVariableW(L"VEYRA_DISABLE_ADA_MFG_UNLOCK", disabled, 2) > 0) {
        veyra::log::warn("ada-mfg", "multi-frame unlock disabled by VEYRA_DISABLE_ADA_MFG_UNLOCK");
        return;
    }
    const auto modulePath = std::filesystem::path(desc_.runtimeAbsPath) / L"nvngx_dlssg.dll";
HMODULE module = fgCompatibility_ ? fgCompatibility_->provider() : nullptr;
    if (module == nullptr) {
        veyra::log::warn("ada-mfg", std::format("DLSS-G runtime could not be opened for the unlock (path={} win32={})",
                                                modulePath.string(), GetLastError()));
        return;
    }
    const auto scan = ngx::AdaMfgUnlock::scan(module);
    if (!scan.moduleValid || scan.archGateSites < ngx::AdaMfgUnlock::kMinArchGateSites ||
        scan.archGateSites > ngx::AdaMfgUnlock::kMaxArchGateSites ||
        scan.mfgGateSites != ngx::AdaMfgUnlock::kExpectedMfgGateSites || !scan.mfgGateValid ||
        scan.descriptorSlots == 0 ||
        scan.ptxBytes != ngx::AdaMfgUnlock::kExpectedPtxBytes ||
        scan.midpointCount != ngx::AdaMfgUnlock::kExpectedMidpoints || !scan.joinLabelUnique) {
        veyra::log::warn("ada-mfg", std::format("unlock refused: runtime build does not match the audited structure ({})",
                                                scan.detail));
        return;
    }
    const auto state = ngx::AdaMfgUnlock::apply(module, true);
    std::string detailStr;
    detailStr.reserve(state.detail.size());
    for (auto c : state.detail) detailStr.push_back(static_cast<char>(c));
    veyra::log::info("ada-mfg", std::format("adapter deviceId=0x{:04X} unlock applied={} gates={} mfgGate={} descriptors={} kernel={} ({})",
                                            adapter.deviceId, state.applied ? 1 : 0, state.archGateSites,
                                            state.mfgGatePatched ? 1 : 0, state.descriptorSlots,
                                            state.kernelPatched ? 1 : 0,
                                            detailStr));
}

// Must run before the NGX core initializes the DLSS-G provider: the provider
// resolves NvAPI_GPU_GetArchInfo once during its own initialization and caches
// the resulting architecture decision, so installing the spoof afterwards has
// no effect (measured 2026-09-17 on the local 5070: the cached entry was
// already populated and the capability verdict stayed unchanged).
void EnhanceGraph::prepareAmpereFgSpoof()
{
    if (!fgEnabled_ || desc_.frameGenerationBackend != engine::FrameGenerationBackend::Dlss) {
        return;
    }
    wchar_t disabled[2]{};
    if (GetEnvironmentVariableW(L"VEYRA_DISABLE_AMPERE_MFG_UNLOCK", disabled, 2) > 0) {
        return;
    }
    const auto& adapter = context_.adapter();
    wchar_t forced[2]{};
    const bool forceOnAnyAdapter = GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_AMPERE_UNLOCK", forced, 2) > 0;
    if (!forceOnAnyAdapter && !ngx::AmpereMfgUnlock::adapterIsAmpere(adapter.vendorId, adapter.deviceId)) {
        return;
    }

    const auto modulePath = std::filesystem::path(desc_.runtimeAbsPath) / L"nvngx_dlssg.dll";
HMODULE module = fgCompatibility_ ? fgCompatibility_->provider() : nullptr;
    if (module == nullptr) {
        veyra::log::warn("ampere-mfg", std::format("DLSS-G runtime could not be preloaded for the architecture spoof (path={} win32={})",
                                                   modulePath.string(), GetLastError()));
        return;
    }
    // Preserve real architecture for DL4RT's hardware-specific network choice.
    // Upstream changes capability policy independently of the NVAPI query.
    // Architecture substitution remains available only for diagnostic tests.
    uint32_t spoofArchitecture = 0;
    {
        wchar_t overrideText[16]{};
        if (GetEnvironmentVariableW(L"VEYRA_TEST_NVAPI_SPOOF_ARCH", overrideText, 16) > 0) {
            spoofArchitecture = static_cast<uint32_t>(std::wcstoul(overrideText, nullptr, 0));
        }
    }
    if (spoofArchitecture == 0) {
        veyra::log::info("ampere-mfg", "preserving real NVAPI architecture; applying scoped compatibility policy only");
        return;
    }
    ampereSpoofed_ = ngx::NvapiArchSpoof::install(module, spoofArchitecture);
    if (!ampereSpoofed_) {
        const auto state = ngx::NvapiArchSpoof::snapshot();
        std::string detailStr;
        detailStr.reserve(state.detail.size());
        for (auto c : state.detail) detailStr.push_back(static_cast<char>(c));
        veyra::log::warn("ampere-mfg", std::format("NVAPI architecture spoof unavailable ({}); the arch-gate retarget will be used instead",
                                                   detailStr));
    }
}

void EnhanceGraph::applyAmpereMfgUnlock()
{
    const auto& adapter = context_.adapter();
    // Hard architecture gate: only RTX 30 (Ampere GA10x) takes this path. Ada
    // keeps its own unlock and Blackwell keeps its native multi-frame path.
    // VEYRA_TEST_FORCE_AMPERE_UNLOCK exists so the patch itself can be checked
    // on a non-Ampere host; it is not enabled in a product session.
    wchar_t forced[2]{};
    const bool forceOnAnyAdapter=GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_AMPERE_UNLOCK",forced,2)>0;
    if (!forceOnAnyAdapter&&!ngx::AmpereMfgUnlock::adapterIsAmpere(adapter.vendorId, adapter.deviceId)) {
        return;
    }
    wchar_t disabled[2]{};
    if (GetEnvironmentVariableW(L"VEYRA_DISABLE_AMPERE_MFG_UNLOCK", disabled, 2) > 0) {
        veyra::log::warn("ampere-mfg", "RTX 30 sm_86 unlock disabled by VEYRA_DISABLE_AMPERE_MFG_UNLOCK");
        return;
    }
    const auto modulePath = std::filesystem::path(desc_.runtimeAbsPath) / L"nvngx_dlssg.dll";
HMODULE module = fgCompatibility_ ? fgCompatibility_->provider() : nullptr;
    if (module == nullptr) {
        veyra::log::warn("ampere-mfg", std::format("DLSS-G runtime could not be opened for the RTX 30 unlock (path={} win32={})",
                                                   modulePath.string(), GetLastError()));
        return;
    }
    const auto scan = ngx::AmpereMfgUnlock::scan(module);
    if (!scan.moduleValid || !scan.identityMatched ||
        scan.slotRuns != ngx::AmpereMfgUnlock::kExpectedSlotRuns ||
        scan.slotPointers != ngx::AmpereMfgUnlock::kExpectedSlotRuns *
                                 ngx::AmpereMfgUnlock::kExpectedSlotsPerRun ||
        scan.programFatbins != ngx::AmpereMfgUnlock::kExpectedProgramFatbins ||
        scan.networkFatbins != ngx::AmpereMfgUnlock::kExpectedRdataNetworkFatbins ||
        scan.auxFatbins != ngx::AmpereMfgUnlock::kExpectedAuxFatbins ||
        scan.leaSites != ngx::AmpereMfgUnlock::kExpectedLeaSites ||
        scan.archGateSites < ngx::AmpereMfgUnlock::kMinArchGateSites ||
        scan.archGateSites > ngx::AmpereMfgUnlock::kMaxArchGateSites ||
        !scan.temporalSlotUnique) {
        veyra::log::warn("ampere-mfg", std::format("unlock refused: runtime build does not match the audited structure ({})",
                                                   scan.detail));
        return;
    }
    // The spoof was installed before the provider was initialized (see
    // prepareAmpereFgSpoof). When it is active the provider's own 0x1b0 compare
    // must stay byte-identical so the reported architecture matches it; only
    // the kernel rewrite runs here. Without the spoof the old retarget is kept.
    const auto spoofState = ngx::NvapiArchSpoof::snapshot();
    const bool spoofed = ampereSpoofed_ && spoofState.installed;
    const auto state = ngx::AmpereMfgUnlock::apply(module, !spoofed, adapter.luid);
    std::string detailStr;
    detailStr.reserve(state.detail.size());
    for (auto c : state.detail) detailStr.push_back(static_cast<char>(c));
    veyra::log::info("ampere-mfg", std::format("adapter deviceId=0x{:04X} unlock applied={} spoofed={} reportedArch=0x{:X} runs={} slots={} inventoryFatbins={} inventoryLea={} gates={} ({})",
                                               adapter.deviceId, state.applied ? 1 : 0,
                                               spoofed ? 1 : 0, spoofState.reportedArchitecture, state.slotRuns,
                                               state.slotPointers,
                                               state.programFatbins + state.networkFatbins + state.auxFatbins,
                                               state.leaSites, state.archGateSites,
                                               detailStr));
}

bool EnhanceGraph::initFsrSr()
{
    if (!fsrSrRequested()) return true;
    if (desc_.noFeatures) {
        veyra::log::warn("fsr-sr", "feature-disabled configuration: AMD FSR upscaling skipped");
        return true;
    }
    if (desc_.hdrWorking()) {
        // The working color here is linear FP16 with Veyra's own HDR contract;
        // feeding that to the FSR HDR path without a verified transfer contract
        // would be a guess. Refuse the stage instead of shipping a wrong image.
        veyra::log::warn("fsr-sr", "HDR output is not supported by the AMD FSR upscaling stage yet; continuing without it");
        return true;
    }
    failedBackend_=engine::FailedBackend::Sr;
    fsrSrBackend_=std::make_unique<gfx::FsrSrBackend>();
    if(!fsrSrBackend_->initialize(context_.device(),srcW_,srcH_,workW_,workH_,false)){
        fsrSrBackend_.reset();
        // The engine checks fsrSrRequested() && !fsrSrEnabled() after the graph
        // is created and disables the stage with a user-visible warning, so a
        // missing local runtime never blocks basic playback.
        veyra::log::error("fsr-sr", "AMD FSR upscaling unavailable; the engine will disable the stage");
        failedBackend_=engine::FailedBackend::None;
        return true;
    }
    failedBackend_=engine::FailedBackend::None;
    veyra::log::info("fsr-sr", std::format("selected render={}x{} output={}x{} provider={}",srcW_,srcH_,workW_,workH_,
                                           fsrSrBackend_->providerVersion()));
    return true;
}

bool EnhanceGraph::initNgxFeatures()
{
    failedBackend_=engine::FailedBackend::NgxCore;
    if(desc_.convertVideoHdr()&&(desc_.noNgx||desc_.noFeatures)){failedBackend_=engine::FailedBackend::VideoHdr;return false;}
    if (desc_.noNgx || desc_.noFeatures || (!nrEnabled_&&!srEnabled_&&!fgEnabled_&&!desc_.convertVideoHdr())) {
        veyra::log::info("graph", "NGX core/features skipped by disabled-feature configuration");
        return true;
    }
    // FSR upscaling is a FidelityFX effect: a session whose only feature is
    // FSR SR must not require (or fail on) the NGX core.
    const bool needNgxCore = nrEnabled_ || fgEnabled_ || desc_.convertVideoHdr() ||
        (srEnabled_ && desc_.videoSrQuality!=engine::kVideoSrFsr);
    if (!needNgxCore) {
        veyra::log::info("graph", "NGX core skipped: AMD FSR upscaling is the only enabled stage");
        failedBackend_=engine::FailedBackend::None;
        return true;
    }
    // Read the local NGX identity (same loader contract as the probes).
    std::ifstream ids(std::wstring(desc_.runtimeAbsPath) + L"\\..\\config\\ngx-local.json",
        std::ios::binary);
    std::string idText((std::istreambuf_iterator<char>(ids)), std::istreambuf_iterator<char>());
    std::string projectId, engineVersion;
    {
        auto scan = [&idText](const char* key) {
            const std::string needle = std::string("\"") + key + "\"";
            size_t p = idText.find(needle);
            if (p == std::string::npos) return std::string();
            p = idText.find('"', idText.find(':', p + needle.size()));
            if (p == std::string::npos) return std::string();
            return idText.substr(p + 1, idText.find('"', p + 1) - p - 1);
        };
        projectId = scan("ngxProjectId");
        engineVersion = scan("engineVersion");
    }
    if (projectId.empty()) {
        veyra::log::error("graph", "ngx-local.json missing");
        return false;
    }

    // Publish the programs before Init: the provider can register/cache CUDA
    // programs and capabilities during Init, before GetCapabilityParameters.
    if(!ngx::FgCompatibilitySession::processHealthy()){
        failedBackend_=engine::FailedBackend::NgxCore;
        log::error("graph","compatibility rollback was not verified; restart required before NGX initialization");
        return false;
    }
    if (fgEnabled_ && desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss &&
        ngx::FgCompatibilitySession::requested(context_.adapter().vendorId,context_.adapter().deviceId)) {
        failedBackend_=engine::FailedBackend::Fg;
        fgCompatibility_=std::make_unique<ngx::FgCompatibilitySession>();
        if(!fgCompatibility_->open(context_.device(),context_.adapter().vendorId,context_.adapter().deviceId,desc_.runtimeAbsPath))return false;
        prepareAmpereFgSpoof();
        applyAdaMfgUnlock();
        applyAmpereMfgUnlock();
        if(!fgCompatibility_->prepareDriver(desc_.runtimeAbsPath,projectId.c_str(),engineVersion.c_str()))return false;
        std::array<ID3D12Resource*,2> real={videoFrame_[0].Get(),videoFrame_[1].Get()};
        std::array<ID3D12Resource*,kGeneratedPoolSlots> generated{};
        for(size_t i=0;i<generated.size();++i)generated[i]=genFrame_[i].Get();
        if(!fgCompatibility_->bindResources(real,generated))return false;
    }

    failedBackend_=engine::FailedBackend::NgxCore;
    coreHost_ = std::make_unique<ngx::NgxCoreHost>();
    Status st = Status::Ok;
    if(fgCompatibility_&&!fgCompatibility_->beginInitialization())return false;
    const bool initialized=coreHost_->initialize(context_.device(), desc_.runtimeAbsPath.c_str(),
            projectId.c_str(), engineVersion.c_str(), st);
    const bool initRestored=!fgCompatibility_||fgCompatibility_->endInitialization(initialized);
    if (!initialized || !initRestored) {
        return false;
    }

    fgCapsAvailable_ = false;
    fgMultiFrameMax_ = 0;
    if (fgEnabled_ && desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss) {
    failedBackend_=engine::FailedBackend::Fg;
    fgBackend_ = std::make_unique<ngx::DlssFgBackend>();
    fgBackend_->setCompatibility(fgCompatibility_.get());
    ngx::DlssFgBackend::Capability fgCaps{};
    const bool fgAvailable = fgBackend_->queryCapability(*coreHost_, fgCaps, st);
    if (!fgAvailable) {
        veyra::log::error("graph", "FG capability startup failed; see native results and compatibility stage above");
        return false;
    }
        // Publish the capability before validating the request so a rejected
        // multiplier still teaches the caller what this GPU supports.
        fgCapsAvailable_ = fgCaps.available;
        fgMultiFrameMax_ = fgCaps.multiFrameCountMax;
        if (ngx::AdaMfgUnlock::applied() && fgMultiFrameMax_ <= 1) {
            veyra::log::warn("ada-mfg", std::format("provider patch installed, but startup maximum remains {}; inspect provider generation and capability query", fgMultiFrameMax_));
        }
        // Test-only capability override: lets the Ada (40-series) ceiling and the
        // capability-driven UI/recovery paths be exercised on a 50-series host
        // without touching the runtime. Never set by the product UI.
        {
            wchar_t overrideText[16]{};
            if(GetEnvironmentVariableW(L"VEYRA_TEST_FG_MULTIFRAME_MAX",overrideText,16)>0){
                const int forced=_wtoi(overrideText);
                if(forced>=0&&forced<=5){
                    fgMultiFrameMax_=forced;fgCaps.multiFrameCountMax=uint32_t(forced);
                    veyra::log::warn("capability",std::format("test-only FG MultiFrameCountMax override={}",forced));
                }
            }
        }
        veyra::log::info("graph", std::format("FG capability available={} multiFrameMax={}",
            fgCaps.available, fgCaps.multiFrameCountMax));
        // Test-only research path for the Ada (40-series) MFG unlock: with
        // VEYRA_TEST_FG_FORCE_MULTIPLIER=1 the reported capability no longer
        // rejects the request, so a 40-series host can show whether the runtime
        // generates real frames (or repeats/black) when the gate is bypassed at
        // the caller. Never reachable from the product UI.
        const bool forceAboveCapability=GetEnvironmentVariableW(L"VEYRA_TEST_FG_FORCE_MULTIPLIER",nullptr,0)>0;
        const bool aboveCapability=desc_.enableFg&&fgCaps.multiFrameCountMax<desc_.fgMultiplier-1;
        if(aboveCapability&&forceAboveCapability){
            veyra::log::warn("capability",std::format("test-only FG multiplier forced above capability request={} maxGeneratedFrames={}",desc_.fgMultiplier,fgCaps.multiFrameCountMax));
        }
        if(desc_.enableFg&&(desc_.fgMultiplier<2||desc_.fgMultiplier>6||(aboveCapability&&!forceAboveCapability))){veyra::log::error("graph",std::format("requested MFG multiplier unsupported request={} maxGeneratedFrames={}",desc_.fgMultiplier,fgCaps.multiFrameCountMax));return false;}
    }

    if(nrEnabled_){
    failedBackend_=engine::FailedBackend::Nr;
    if(desc_.nrRuntime!=engine::NrRuntime::Original&&desc_.nrRuntime!=engine::NrRuntime::Community&&desc_.nrRuntime!=engine::NrRuntime::Ampere)return false;
    auto nrDirectory=std::filesystem::path(desc_.runtimeAbsPath);if(desc_.nrRuntime==engine::NrRuntime::Community)nrDirectory/=L"nr-community";
    if(desc_.nrRuntime==engine::NrRuntime::Ampere)nrDirectory/=L"nr-ampere";
    veyra::log::info("nr-runtime",std::format("selected={} path={}",engine::nrRuntimeName(desc_.nrRuntime),nrDirectory.string()));
    nrAdapter_ = std::make_unique<ngx::DlssNrRuntimeAdapter>();
    if (!nrAdapter_->load(nrDirectory.wstring(), st) ||
        !nrAdapter_->installCallerCompatibility(st) ||
        (desc_.nrRuntime==engine::NrRuntime::Ampere && !nrAdapter_->installAmpereCompatibility(context_.device(), st)) ||
        !nrAdapter_->snippetInitExt(context_.device(), nrDirectory.c_str(), nrResult_, nrSeh_) ||
        nrResult_ != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
        veyra::log::error("graph", std::format("NR snippet init failed 0x{:X}", nrResult_));
        return false;
    }
    }
    failedBackend_=engine::FailedBackend::NgxCore;
    ngxParams_ = coreHost_->allocateParameters(st);
    if (ngxParams_ == nullptr) return false;

    srBackend_ = std::make_unique<ngx::DlssSrBackend>();
    if(desc_.convertVideoHdr()){
        auto* list=ring_.acquire(0,st);if(!list)return false;
        failedBackend_=engine::FailedBackend::VideoHdr;
        videoHdrBackend_=std::make_unique<ngx::TrueHdrBackend>();
        if(!videoHdrBackend_->create(*coreHost_,list,desc_.runtimeAbsPath))return false;
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;
    }

    // NR feature create (8.5 create parameter contract).
    if(nrEnabled_) {
        namespace p = ngx::dlssnr;
        ngx::ParameterBlock pb(ngxParams_);
        pb.setU32(p::kWidth, nrW_); pb.setU32(p::kHeight, nrH_);
        pb.setU32(p::kInputWidth, nrW_); pb.setU32(p::kInputHeight, nrH_);
        pb.setU32(p::kOutputWidth, nrW_); pb.setU32(p::kOutputHeight, nrH_);
        pb.setU32(p::kOutputDotWidth, nrW_); pb.setU32(p::kOutputDotHeight, nrH_);
        pb.setU32(p::kUpscaling, 0);
        pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
        pb.setVoid(p::kComputeScalingRatioCallback,
            reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        pb.setI32(p::kHintRenderPreset, 0);
        pb.setU32(p::kStdWidth, nrW_); pb.setU32(p::kStdHeight, nrH_);
        pb.setI32(p::kPerfQualityValue, 1);
        pb.setU32(p::kCreationNodeMask, 1); pb.setU32(p::kVisibilityNodeMask, 1);
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) { failedBackend_=engine::FailedBackend::Infrastructure; return false; }
        failedBackend_=engine::FailedBackend::Nr;
        if (!nrAdapter_->snippetCreateFeature(list, ngxParams_, &nrHandle_, nrResult_, nrSeh_) ||
            nrResult_ != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
            veyra::log::error("graph", std::format("NR create failed 0x{:X}", nrResult_));
            return false;
        }
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;
    }

    if (srEnabled_ && !desc_.noFeatures && desc_.videoSrQuality!=engine::kVideoSrFsr) {
        ngx::DlssSrBackend::CreateDesc sd{};
        sd.inputWidth = srcW_; sd.inputHeight = srcH_;
        sd.outputWidth = workW_; sd.outputHeight = workH_;
        sd.perfQuality = 1; sd.enableOutputSubrects = false;
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) { failedBackend_=engine::FailedBackend::Infrastructure; return false; }
        failedBackend_=engine::FailedBackend::Sr;
        if(desc_.videoSrQuality){videoSrBackend_=std::make_unique<ngx::VideoSrBackend>();if(!videoSrBackend_->create(*coreHost_,list))return false;veyra::log::info("video-sr",std::format("selected quality={} input={}x{} output={}x{}",desc_.videoSrQuality,srcW_,srcH_,workW_,workH_));}
        else if (!srBackend_->create(*coreHost_, list, ngxParams_, sd, st) || !srBackend_->created()) {
            veyra::log::error("graph", "SR create failed");
            return false;
        }
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;
    }

    // Still images have no temporal pair and must not depend on FG support.
    // FG create + warm-up evaluate BEFORE descriptor views (the runtime
    // allocates internals at the first evaluate and would fail after views
    // exist on this system).
    if (fgEnabled_ && desc_.frameGenerationBackend==engine::FrameGenerationBackend::Dlss) {
        ngx::DlssFgBackend::CreateDesc fd{};
        fd.width = workW_; fd.height = workH_;
        fd.renderWidth = workW_; fd.renderHeight = workH_;
        fd.backbufferFormat = outputFormat();
        ID3D12GraphicsCommandList* list = ring_.acquire(0, st);
        if (list == nullptr) { failedBackend_=engine::FailedBackend::Infrastructure; return false; }
        failedBackend_=engine::FailedBackend::Fg;
        if (!fgBackend_->create(*coreHost_, list, ngxParams_, fd, st) || !fgBackend_->created()) {
            veyra::log::error("graph", std::format("FG create failed 0x{:X}", fgBackend_->createResult()));
            return false;
        }
        failedBackend_=engine::FailedBackend::Infrastructure;
        if(!ring_.submitAndSignal(0)||!ring_.waitIdle())return false;

        ID3D12GraphicsCommandList* wlist = ring_.acquire(0, st);
        if (wlist == nullptr) return false;
        D3D12_RESOURCE_BARRIER b[4]{};
        for (int i = 0; i < 4; ++i) {
            b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        b[0].Transition.pResource = videoFrame_[0].Get();
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[1].Transition.pResource = nrZeroMotion_.Get();
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[2].Transition.pResource = depthTex_.Get();
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b[3].Transition.pResource = genFrame_[0].Get();
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        wlist->ResourceBarrier(4, b);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.backbuffer = videoFrame_[0].Get();
        fe.depth = depthTex_.Get();
        fe.mvecs = nrZeroMotion_.Get();
        fe.outputInterpolated = genFrame_[0].Get();
        fe.reset = true;fe.hdr=hdr10Output();
        fe.multiFrameCount=desc_.enableFg?desc_.fgMultiplier-1:1;fe.multiFrameIndex=1;
        fe.frameId = 0; // warm-up has its own identity; product IDs start at 1
        fe.mvecScaleX = 1.0f;
        fe.mvecScaleY = 1.0f;
        bool warmOk = true;
        for(uint32_t sub=1;sub<=fe.multiFrameCount;++sub){
            fe.multiFrameIndex=sub;
            if (!fgBackend_->evaluate(wlist,ngxParams_,fe,st)) { warmOk=false; break; }
            D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=fe.outputInterpolated;wlist->ResourceBarrier(1,&u);
        }
        if(!warmOk){failedBackend_=engine::FailedBackend::Fg;return false;}
        for (int i = 0; i < 4; ++i) b[i].Transition.StateBefore = b[i].Transition.StateAfter;
        b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        b[3].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        wlist->ResourceBarrier(4, b);
        if (!ring_.submitAndSignal(0) || !ring_.waitIdle()) return false;
        veyra::log::info("graph", std::format("FG warm-up evaluate done ok={} result=0x{:X}",
            warmOk ? 1 : 0, fgBackend_->createResult()));
    }
    return true;
}

bool EnhanceGraph::createComputePasses()
{
    std::vector<uint8_t> cs;
    if(!downsamplePass_.loadShader("NrDownsample.dxil",cs)||!downsamplePass_.create(context_.device(),cs,2,1,1))return false;
    if(!residualPass_.loadShader("NrResidualComposite.dxil",cs)||!residualPass_.create(context_.device(),cs,4,3,1,24))return false;
    if(!flowAdaptPass_.loadShader("FlowAdapt.dxil",cs)||!flowAdaptPass_.create(context_.device(),cs,3,1,1))return false;
    if (!yuvPass_.loadShader("YuvToLinearRgb.dxil", cs) || !yuvPass_.create(context_.device(), cs, 12, 2, 1, 12+kColorGradeConstantCount, 4)) return false;
    const char* rgbShader=desc_.packedInput?"PackedCaptureToLinear.dxil":desc_.yuy2Input?"Yuy2ToLinear.dxil":"RgbToLinear.dxil";
    if((desc_.rgbInput||desc_.yuy2Input||desc_.packedInput)&&(!rgbPass_.loadShader(rgbShader,cs)||!rgbPass_.create(context_.device(),cs,12,1,1,8+kColorGradeConstantCount,4)))return false;
    if (!encPass_.loadShader("ParityEncode.dxil", cs) || !encPass_.create(context_.device(), cs, 8, 1, 1)) return false;
    if (!decPass_.loadShader("ParityDecode.dxil", cs) || !decPass_.create(context_.device(), cs, 8)) return false;
    if (!blitPass_.loadShader("ScaleBlit.dxil", cs) || !blitPass_.create(context_.device(), cs, 21, 1, 1)) return false;
    // Nv12Upload stays: frame-time CopyTextureRegion is poisoned by the
    // injected layer (SEH in NGX evaluate, r33-final3 evidence); the compute
    // upload is the proven frame-path ingestion on this system.
    if (!uploadPass_.loadShader("Nv12Upload.dxil", cs) || !uploadPass_.create(context_.device(), cs, 4, 1, 2)) return false;
    if (!densifyPass_.loadShader("NvofDensify.dxil", cs) ||
        !densifyPass_.create(context_.device(), cs, 7, 5, 2)) return false;
    if(desc_.hdrWorking()&&videoSrInput_&&(!hdrVideoSrPass_.loadShader("HdrVideoSrRestore.dxil",cs)||!hdrVideoSrPass_.create(context_.device(),cs,7,3,1)))return false;
    if (!stager_.initialize(context_.device(), 80)) {
        veyra::log::error("graph", "descriptor stager init failed");
        return false;
    }
    return true;
}

bool EnhanceGraph::createViews()
{
    // Descriptor initialization is DEFAULT ON since the RAW-upload-SRV removal
    // (2026-09-06): texture SRV/UAV views keep the device alive and NVOF
    // executing (r33b-tu: 599/599 executes, real motion/confidence, 0
    // removals) and eliminate the uninitialized-slot dispatches behind GBV
    // id=938. VEYRA_VIEWS_OFF preserves the legacy behavior for A/B.
    const bool viewsOn = GetEnvironmentVariableW(L"VEYRA_VIEWS_OFF", nullptr, 0) == 0;
    const bool viewsTex = viewsOn;
    const bool viewsUav = viewsOn;
    if (!viewsOn) {
        veyra::log::warn("graph", "static views DISABLED (VEYRA_VIEWS_OFF; legacy behavior)");
        return true;
    }
    auto cpu = [this](const ComputePass& p, UINT slot) { return cpuHandleOf(p, slot); };
    auto gpu = [this](const ComputePass& p, UINT slot) { return gpuHandleOf(p, slot); };
    (void)gpu;
    // SRVs must go through the staging heap (CopyDescriptorsSimple into the
    // visible heap): direct CreateShaderResourceView writes into visible
    // heaps trigger the fabricated device-removed on this system (bare
    // stage 9 evidence; the safe path is proven 600/600).
    auto stagedSrv = [this](ID3D12Resource* resource, DXGI_FORMAT fmt,
                            ComputePass& pass, UINT slot) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = fmt;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        stager_.stageSrv(resource, &srv, pass.heap.Get(), slot);
    };

    stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 15);
    stagedSrv(desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,downsamplePass_,0);
    makeUav(context_.device(),nrInput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(downsamplePass_,1));
    stagedSrv(desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,0);
    stagedSrv(nrInput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,1);
    stagedSrv(finalRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,residualPass_,2);
    if(desc_.nrTemporal&&!nrTemporal_.initialize(context_.device(),desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get(),flowTex_.Get(),residualRgba_.Get()))return false;
    makeUav(context_.device(),desc_.nrTemporal?nrTemporal_.raw():residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(residualPass_,3));
    stagedSrv(flowTex_.Get(),DXGI_FORMAT_R16G16_FLOAT,flowAdaptPass_,0);
    makeUav(context_.device(),nrFlow_.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu(flowAdaptPass_,1));
    makeUav(context_.device(),baseFlow_.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu(flowAdaptPass_,2));
    if(desc_.hdrWorking()&&videoSrInput_){
        stagedSrv(srcRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,hdrVideoSrPass_,0);
        stagedSrv(videoSrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,1);
        stagedSrv(videoSrOutput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,2);
        makeUav(context_.device(),workRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(hdrVideoSrPass_,3));
        stagedSrv(residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,hdrVideoSrPass_,4);
        stagedSrv(videoSrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,5);
        stagedSrv(videoSrOutput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,hdrVideoSrPass_,6);
    }
    // Immutable per-resource views.
    if(desc_.rgbInput||desc_.yuy2Input||desc_.packedInput){
        stagedSrv(rgbTex_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,rgbPass_,0);
        makeUav(context_.device(),srcRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,cpu(rgbPass_,1));
    }
    if (viewsTex) stagedSrv(lumaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM, yuvPass_, 0);
    if (viewsTex) stagedSrv(chromaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM, yuvPass_, 1);
    if (viewsUav) makeUav(context_.device(), srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(yuvPass_, 2));
    if (viewsTex) stagedSrv(nrInput_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, encPass_, 0);
    if (viewsUav) makeUav(context_.device(), proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, cpu(encPass_, 1));
    if (viewsTex) stagedSrv(nrInput_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, decPass_, 0);
    if (viewsTex) stagedSrv(proxyTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 1);
    if (viewsTex) stagedSrv(neuralTex_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, decPass_, 2);
    if (viewsUav) makeUav(context_.device(), finalRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(decPass_, 3));
    if (viewsUav) makeUav(context_.device(), lumaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM, cpu(uploadPass_, 2));
    if (viewsUav) makeUav(context_.device(), chromaTex_.Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM, cpu(uploadPass_, 3));
    // Software NV12 ingestion uses plane copies; no raw-SRV dispatch.
    // Blit pass layout (all static; per-use offsets chosen at bind time):
    //  0: srcRgba SRV        1: workRgba UAV      (SR bypass / NR-off blit)
    //  2: finalRgba SRV      3/4: videoFrame UAV  (section 5)
    //  5: nvofInB SRV        6: nvofInA UAV       (NVOF A:=B)
    //  7/8: videoFrame SRV   9: nvofInB UAV       (NVOF B:=video)
    // 10: genTex SRV        11/12: videoFrame SRV (presents)
    // 13/14: genTex SRV (slot 2)
    if(videoSrInput_&&viewsUav)makeUav(context_.device(),videoSrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,cpu(blitPass_,16));
    if(videoSrOutput_&&viewsTex)stagedSrv(videoSrOutput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,blitPass_,17);
    if (viewsTex) stagedSrv(residualRgba_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,blitPass_,18);
    if(videoHdrInput_&&viewsUav)makeUav(context_.device(),videoHdrInput_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,cpu(blitPass_,19));
    if(videoHdrOutput_&&viewsTex)stagedSrv(videoHdrOutput_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,blitPass_,20);
    if (viewsTex) stagedSrv(srcRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 0);
    // Colour-grade tables: four SRVs at the extra table's fixed register base.
    if(colorActive_){
        D3D12_SHADER_RESOURCE_VIEW_DESC lutSrv{};
        lutSrv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        lutSrv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;
        lutSrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        lutSrv.Texture3D.MipLevels=1;
        for(auto* pass:{&yuvPass_,&rgbPass_}){
            if(!pass->heap)continue;
            stager_.stageSrv(colorCurveTex_.Get(),nullptr,pass->heap.Get(),8);
            stager_.stageSrv(colorHueTex_.Get(),nullptr,pass->heap.Get(),9);
            stager_.stageSrv(colorLumTex_.Get(),nullptr,pass->heap.Get(),10);
            stager_.stageSrv(colorLutTex_.Get(),&lutSrv,pass->heap.Get(),11);
        }
    }
    if (viewsUav) makeUav(context_.device(), workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, cpu(blitPass_, 1));
    if (viewsTex) stagedSrv(nrEnabled_&&!desc_.nrBeforeSr ? residualRgba_.Get() : workRgba_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, blitPass_, 2);
    if (viewsUav) makeUav(context_.device(), videoFrame_[0].Get(), outputFormat(), cpu(blitPass_, 3));
    if (viewsUav) makeUav(context_.device(), videoFrame_[1].Get(), outputFormat(), cpu(blitPass_, 4));
    if (viewsTex) stagedSrv(nvofInB_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, blitPass_, 5);
    if (viewsUav) makeUav(context_.device(), nvofInA_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpu(blitPass_, 6));
    if (viewsTex) stagedSrv(videoFrame_[0].Get(), outputFormat(), blitPass_, 7);
    if (viewsTex) stagedSrv(videoFrame_[1].Get(), outputFormat(), blitPass_, 8);
    if (viewsUav) makeUav(context_.device(), nvofInB_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, cpu(blitPass_, 9));
    if (viewsTex) stagedSrv(genFrame_[0].Get(), outputFormat(), blitPass_, 10);
    if (viewsTex) stagedSrv(genFrame_[1].Get(), outputFormat(), blitPass_, 13);
    if (viewsTex) stagedSrv(videoFrame_[0].Get(), outputFormat(), blitPass_, 11);
    if (viewsTex) stagedSrv(videoFrame_[1].Get(), outputFormat(), blitPass_, 12);

    // Densify pass views: 0=rawFlow SRV(int2) 1=cost SRV(uint)
    // 2=previous RGB,3=current RGB,4=AMD SCD;5=flow UAV,6=confidence UAV.
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC rf{};
        rf.Format = DXGI_FORMAT_R16G16_SINT;
        rf.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rf.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rf.Texture2D.MostDetailedMip = 0;
        rf.Texture2D.MipLevels = 1;
        rf.Texture2D.PlaneSlice = 0;
        rf.Texture2D.ResourceMinLODClamp = 0.0f;
        if (viewsTex) stager_.stageSrv(nvofRawTex_.Get(), &rf, densifyPass_.heap.Get(), 0);
        D3D12_SHADER_RESOURCE_VIEW_DESC rc{};
        rc.Format = DXGI_FORMAT_R8_UINT;
        rc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rc.Texture2D.MostDetailedMip = 0;
        rc.Texture2D.MipLevels = 1;
        rc.Texture2D.PlaneSlice = 0;
        rc.Texture2D.ResourceMinLODClamp = 0.0f;
        if (viewsTex) stager_.stageSrv(nvofCostTex_.Get(), &rc, densifyPass_.heap.Get(), 1);
        if(viewsTex){stagedSrv(nvofInA_.Get(),DXGI_FORMAT_B8G8R8A8_UNORM,densifyPass_,2);stagedSrv(nvofInB_.Get(),DXGI_FORMAT_B8G8R8A8_UNORM,densifyPass_,3);}
        if(viewsTex){rc.Format=DXGI_FORMAT_R32_UINT;stager_.stageSrv(amdOf_?amdOf_->sceneChanges():nullptr,&rc,densifyPass_.heap.Get(),4);}
        D3D12_UNORDERED_ACCESS_VIEW_DESC uf{};
        uf.Format = DXGI_FORMAT_R16G16_FLOAT;
        uf.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        context_.device()->CreateUnorderedAccessView(flowTex_.Get(), nullptr, &uf, cpu(densifyPass_, 5));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uc{};
        uc.Format = DXGI_FORMAT_R8_UNORM;
        uc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        context_.device()->CreateUnorderedAccessView(confTex_.Get(), nullptr, &uc, cpu(densifyPass_, 6));
    }
    return true;
}

// ---------------------------------------------------------------------------
// process: the per-frame chain (verbatim from the probe lambda).
// ---------------------------------------------------------------------------
bool EnhanceGraph::process(const AVFrame* frame, double ptsMs, bool reset, FrameOutputs& out, uint64_t sourceFrameId, const ColorDescription* color, const HardwareSurfaceInput* hardwareSurface, bool retainReferences, const FgAdmission& admitFg, unsigned previewMultiplier)
{
    diagnostics::CpuStallTrace cpuTrace("graph-cpu-stall",sourceFrameId,30.0);
    failedBackend_=engine::FailedBackend::Infrastructure;
    out = FrameOutputs{};
    out.ptsMs = ptsMs;
    if(!sourceFrameId)sourceFrameId=realFrameIndex_+1;
    // The per-frame diagnostic context is only consumed when a warning or
    // error is logged; reuse one member event and refresh the cheap fields
    // (the string members are set once per graph, sweep 2026-09-22 A4).
    diagnostics::DiagnosticEvent& diagnostic=frameDiagnostic_;
    if(diagnostic.stage.empty()){diagnostic.stage="frame";diagnostic.runtimeHash="unverified-user-replaceable";}
    diagnostic.identity={epoch_+uint64_t(reset),desc_.settingsRevision,sourceFrameId};diagnostic.batch=realFrameIndex_+1;
    diagnostic.resolution.source={srcW_,srcH_};diagnostic.resolution.base={workW_,workH_};diagnostic.resolution.nr={nrW_,nrH_};diagnostic.resolution.flow={nvofW_,nvofH_};diagnostic.resolution.fg=diagnostic.resolution.output={workW_,workH_};
    if(diagnosticFlowPerf_!=actualFlowPerf()){diagnosticFlowPerf_=actualFlowPerf();diagnostic.flowApplied=std::to_string(diagnosticFlowPerf_);}
    if(diagnostic.fallbackReason!=mvecSource_)diagnostic.fallbackReason=mvecSource_;
    Logger::diagnosticContext(diagnostic);
    static const bool graphOff = GetEnvironmentVariableW(L"VEYRA_GRAPH_OFF", nullptr, 0) != 0;
    if (!frame || !initialized_ || !std::isfinite(ptsMs)) return false;
    // Reject before CPU plane access, swscale, or command-slot acquisition.
    // Source-side resize handling must rebuild the graph before submitting a
    // new extent; all upload sizes/strides still belong to this graph's extent.
    if (frame->width <= 0 || frame->height <= 0 || uint32_t(frame->width) != srcW_ || uint32_t(frame->height) != srcH_ || !av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format))) {
        veyra::log::error("graph", std::format("frame contract rejected: expected={}x{} actual={}x{} format={}", srcW_, srcH_, frame->width, frame->height, frame->format));
        return false;
    }
    const auto resolved=resolveFrameColor(*frame,color?*color:ColorDescription{});
    if(resolved.isHdrPath()&&!desc_.hdrOutput&&!toneMapPeakNits_){
        const auto peak=hdrToneMapPeak(resolved);toneMapPeakNits_=peak.nits;
        log::info("hdr-tone-map",std::format("method=BT2390-luminance sourcePeakNits={} peakSource={} targetPeakNits=203 blackNits=0 gamut=neutral-ray-soft-knee staticPerGraph=1",peak.nits,peak.source));
    }
    if(desc_.hdrInput&&!resolved.isHdrPath()){
        veyra::log::error("hdr","HDR source transfer changed to SDR; reopen/rebuild required");return false;
    }
    if(resolved.matrix==YuvMatrix::BT2020CL){veyra::log::error("graph","BT.2020 constant-luminance requires a dedicated conversion; refusing NCL substitution");return false;}
    if(resolved.isHdrPath()&&!resolved.scRgb&&(resolved.matrix!=YuvMatrix::BT2020NCL||resolved.primaries!=ColorPrimaries::BT2020)){
        veyra::log::error("hdr","Unsupported PQ/HLG colorimetry: requires signaled/fallback BT2020 NCL and BT2020 primaries");return false;
    }
    if(reset||!realFrameIndex_)veyra::log::info("color",std::format("range={} assumed={} matrix={} assumed={} transfer={} assumed={} display709={} preserveSdrCodes={} workingTransfer={}",int(resolved.range),resolved.rangeAssumed,int(resolved.matrix),resolved.matrixAssumed,int(resolved.transfer),resolved.transferAssumed,resolved.displayReferred709,resolved.preserveSdrCodeValues,workingTransferCode(resolved)));
    if(resolved.isHdrPath()&&!resolved.scRgb&&(reset||!realFrameIndex_)){
        veyra::log::info("hdr-route",std::format("input={} range={} matrix=BT2020-NCL primaries=BT2020 decode={} working={} output={} toneMap={} hdrReferenceWhiteNits=203 hlgReferencePeakNits=1000; SDR workingTransfer field is unused for PQ/HLG",
            resolved.transfer==TransferFunction::HLG?"HLG":"PQ",resolved.range==ColorRange::Full?"full":"limited",
            resolved.transfer==TransferFunction::HLG?"inverse-OETF+OOTF-gamma1.2":"ST2084-EOTF-absolute-nits",
            desc_.hdrWorking()?"linear-BT709-scRGB-1=80nits":"linear-BT709-SDR-relative",
            hdr10Output()?"PQ-BT2020-RGB10":desc_.hdrOutput?"scRGB-FP16":"sRGB-RGB8",
            desc_.hdrOutput?"none":"BT2390-luminance+neutral-ray-gamut-compression"));
    }
    const bool gpuRgb=desc_.rgbInput&&frame->format==AV_PIX_FMT_D3D11&&hardwareSurface&&hardwareSurface->present();
    if(resolved.scRgb&&(!gpuRgb||resolved.transfer!=TransferFunction::Linear||resolved.primaries!=ColorPrimaries::BT709||hardwareSurface->texture->GetDesc().Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
    if (resolved.isHdrPath()&&(!desc_.hdrInput||(!resolved.scRgb&&desc_.rgbInput)||desc_.yuy2Input||desc_.packedInput)) {
        veyra::log::error("graph", "HDR input requires an explicit YUV HDR contract; RGB/YUY2 HDR ingress is unsupported"); return false;
    }
    if(std::abs(ptsMs)>9e13){veyra::log::error("timeline","PTS outside representable range");return false;}
    if(prevValid_&&(std::llround(ptsMs*10000)-std::llround(prevPtsMs_*10000)<4||ptsMs-prevPtsMs_>1000)){
        out.detectedReset=ResetReason::PtsDiscontinuity;
        reset=true;veyra::log::info("timeline","non-monotonic or discontinuous PTS; atomic history reset");
    }
    if (reset) { prevValid_ = false; scene_.reset(); previousLuma_.clear();cadence_.reset(); }

    const uint32_t parity = static_cast<uint32_t>(realFrameIndex_ % 2);
    if(!realLeases_[parity].expired()||!generatedLeases_[parity].expired()){
        veyra::log::error("frame-pool",std::format("slot={} still leased; refusing overwrite, batch={}",parity,realFrameIndex_+1));return false;
    }
    for(unsigned i=parity;i<kGeneratedPoolSlots;i+=2)if(!generatedLeases_[i].expired()){veyra::log::error("frame-pool","generated subframe still leased; refusing overwrite");return false;}
    if (graphOff) {
        prevPtsMs_ = ptsMs;
        prevValid_ = true;
        ++realFrameIndex_;
        out.realFrameIndex = realFrameIndex_;
        out.videoSlot = parity;
        out.passthrough = true;
        return true;
    }
    uint32_t slot = 0;
    cpuTrace.mark("validate");
    if (uploadFences_[parity] && !context_.waitForFenceValue(uploadFences_[parity])) return false;
    // Presentation returns the shared output to COMMON before this parity
    // can be written again. Direct graph callers also need the GPU dependency.
    if(presentationFences_[parity]){
        const HRESULT hr=context_.directQueue()->Wait(presentationFences_[parity].Get(),presentationValues_[parity]);
        if(FAILED(hr)){veyra::log::error("graph",std::format("presentation handoff wait hr=0x{:X}",unsigned(hr)));return false;}
    }
    hardwareInputFrames_[parity].reset();
    if(frame->format==AV_PIX_FMT_D3D12||frame->format==AV_PIX_FMT_D3D11){
        auto* retained=av_frame_clone(frame);
        if(!retained)return false;
        hardwareInputFrames_[parity]=std::shared_ptr<AVFrame>(retained,[](AVFrame* value){av_frame_free(&value);});
    }
    Status st = Status::Ok;

    // 1. Source NV12: D3D12VA texture directly (GPU) or CPU upload.
    ID3D12Resource* nv12Texture = nullptr;
    ID3D12GraphicsCommandList* list = ring_.acquireNext(slot, st);
    if (list == nullptr) { veyra::log::error("graph", "ring acquire"); return false; }
    cpuTrace.mark("acquire");
    gpuTimer_.frame({epoch_,desc_.settingsRevision,realFrameIndex_+1},context_.fence());gpuTimer_.mark(list,GpuStage::Color);

    // Both CPU source layouts feed the same bounded scene/cadence history.
    auto analyzeLuma = [&](std::vector<uint8_t>& sample) {
        auto& hist=lumaHistogram_;std::fill(hist.begin(),hist.end(),0.0);double sad=0;
        for(auto v:sample)hist[v]+=1.0/sample.size();
        if(previousLuma_.size()==sample.size())for(size_t i=0;i<sample.size();++i)sad+=std::abs(int(sample[i])-int(previousLuma_[i]))/(255.0*sample.size());
        cadence_.observe(ptsMs,sad,previousLuma_.size()==sample.size());
        out.measuredContentRate=cadence_.confirmedRate(desc_.contentRate);
        if(cadence_.conflicts(desc_.contentRate)&&realFrameIndex_%60==0)veyra::log::warn("cadence","requested content-rate identification conflicts with observed motion; preserving source timestamps");
        out.contentDuplicate=desc_.contentRate!=engine::ContentRate::Transport&&previousLuma_.size()==sample.size()&&sad<0.0001;
        const auto analysis=scene_.analyze(realFrameIndex_,hist,sad,static_cast<uint64_t>(std::max(0.0,ptsMs)*1000));
        if(analysis.isSceneCut||analysis.isCadenceBreak){
            if(out.detectedReset==ResetReason::None)out.detectedReset=analysis.isSceneCut?ResetReason::SceneCut:ResetReason::CadenceBreak;
            reset=true;prevValid_=false;if(analysis.isSceneCut)++metrics_.sceneCutCount;
            veyra::log::info("scene",std::format("history boundary frame={} ptsMs={} cutCandidate={} cadenceBreak={} sad={} histogramDistance={}",
                realFrameIndex_,ptsMs,analysis.isSceneCut,analysis.isCadenceBreak,analysis.sadScore,analysis.histogramDistance));
        }
        std::swap(previousLuma_,sample);
    };

    if(gpuRgb){
        auto* texture=hardwareSurface->texture;const auto td=texture->GetDesc();
        if(td.Width!=srcW_||td.Height!=srcH_||(td.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&td.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
        if(hardwareSurface->waitFence&&FAILED(context_.directQueue()->Wait(hardwareSurface->waitFence,hardwareSurface->waitValue)))return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=td.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        stager_.stageSrv(texture,&srv,rgbPass_.heap.Get(),3+parity);
        tracker_.transition(list,texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }else if(desc_.rgbInput||desc_.yuy2Input||desc_.packedInput){
        const bool packed=desc_.packedInput!=0;
        if(packed?frame->format!=packedIngressFormat(desc_.packedInput):(desc_.yuy2Input?frame->format!=AV_PIX_FMT_YUYV422:(frame->format!=AV_PIX_FMT_RGBA&&frame->format!=AV_PIX_FMT_BGRA&&frame->format!=AV_PIX_FMT_RGB0&&frame->format!=AV_PIX_FMT_BGR0))){
            veyra::log::error("graph",std::format("direct capture input contract mismatch packing={} frameFormat={}",desc_.packedInput,int(frame->format)));return false;
        }
        const unsigned ingressRowBytes=packed?packedIngressRowBytes(desc_.packedInput,srcW_):srcW_*(desc_.yuy2Input?2:4);
        if(frame->width!=int(srcW_)||frame->height!=int(srcH_)||!frame->data[0]||std::abs(int64_t(frame->linesize[0]))<int64_t(ingressRowBytes))return false;
        for(uint32_t y=0;y<srcH_;++y){
            auto* dst=mappedRgb_[parity]+y*rgbPitch_;const auto* src=frame->data[0]+ptrdiff_t(y)*frame->linesize[0];
            if(packed||desc_.yuy2Input||frame->format==AV_PIX_FMT_RGBA)std::memcpy(dst,src,size_t(ingressRowBytes));
            else for(uint32_t x=0;x<srcW_;++x){
                const bool bgr=frame->format==AV_PIX_FMT_BGRA||frame->format==AV_PIX_FMT_BGR0;
                dst[x*4]=src[x*4+(bgr?2:0)];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4+(bgr?0:2)];
                dst[x*4+3]=(frame->format==AV_PIX_FMT_BGRA)?src[x*4+3]:255;
            }
        }
        if(!desc_.stillImage){
            auto& sample=lumaSample_;sample.clear();sample.reserve(64*36);
            const bool bgr=frame->format==AV_PIX_FMT_BGRA||frame->format==AV_PIX_FMT_BGR0;
            for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x){
                const unsigned sx=x*srcW_/64,sy=y*srcH_/36;
                if(packed)sample.push_back(packedIngressLuma(*frame,desc_.packedInput,sx,sy));
                else{const auto* p=frame->data[0]+ptrdiff_t(sy)*frame->linesize[0]+sx*(desc_.yuy2Input?2:4);
                    sample.push_back(desc_.yuy2Input?p[0]:uint8_t((54*unsigned(p[bgr?2:0])+183*unsigned(p[1])+19*unsigned(p[bgr?0:2])+128)>>8));}
            }
            analyzeLuma(sample);
        }
        tracker_.transition(list,rgbTex_.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=rgbTex_.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upRgb_[parity].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,packed?packedIngressTexels(desc_.packedInput,srcW_):desc_.yuy2Input?srcW_/2:srcW_,srcH_,1,UINT(rgbPitch_)};
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        tracker_.transition(list,rgbTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else if (frame->format == AV_PIX_FMT_D3D12 || frame->format == AV_PIX_FMT_D3D11) {
        // Two hardware sources feed the same import: D3D12VA hands the surface
        // over inside the AVFrame, D3D11VA through the packet (NT-handle shared
        // texture + D3D11 fence). Both become one NV12/P010 D3D12 texture plus
        // one GPU-side wait; the shader samples by texel index inside the
        // visible extent, so decoder allocation padding is never read.
        ID3D12Fence* waitFence = nullptr;
        uint64_t waitValue = 0;
        UINT nv12Slice = 0;
        if (frame->format == AV_PIX_FMT_D3D12) {
            auto* d3dFrame = reinterpret_cast<AVD3D12VAFrame*>(frame->data[0]);
            if (d3dFrame == nullptr || d3dFrame->texture == nullptr) {
                veyra::log::error("graph", "null d3d12va frame");
                return false;
            }
            nv12Texture = d3dFrame->texture;
            nv12Slice = static_cast<UINT>(d3dFrame->subresource_index);
            waitFence = d3dFrame->sync_ctx.fence;
            waitValue = d3dFrame->sync_ctx.fence_value;
        } else {
            if (hardwareSurface == nullptr || !hardwareSurface->present()) {
                veyra::log::error("graph", "d3d11va frame has no hardware surface view");
                return false;
            }
            nv12Texture = hardwareSurface->texture;
            nv12Slice = hardwareSurface->subresourceIndex;
            waitFence = hardwareSurface->waitFence;
            waitValue = hardwareSurface->waitValue;
        }
        if (waitFence != nullptr) {
            if (FAILED(context_.directQueue()->Wait(waitFence, waitValue))) {
                veyra::log::error("graph", "nv12 fence wait");
                return false;
            }
        }
        const bool isArray = nv12Texture->GetDesc().DepthOrArraySize > 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC lumaSrv{};
        lumaSrv.Format = nv12Texture->GetDesc().Format==DXGI_FORMAT_P010?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM;
        lumaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        lumaSrv.Texture2D.MostDetailedMip = 0;
        lumaSrv.Texture2D.MipLevels = 1;
        lumaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
        if (isArray) {
            lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            lumaSrv.Texture2DArray.MostDetailedMip = 0;
            lumaSrv.Texture2DArray.MipLevels = 1;
            lumaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
            lumaSrv.Texture2DArray.ArraySize = 1;
            lumaSrv.Texture2DArray.PlaneSlice = 0;
        } else {
            lumaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            lumaSrv.Texture2D.MostDetailedMip = 0;
            lumaSrv.Texture2D.MipLevels = 1;
            lumaSrv.Texture2D.PlaneSlice = 0;
        }
        // Slots 0/1 remain the CPU-upload views. Hardware views rotate with
        // hardwareInputFrames_/uploadFences_; never overwrite an in-flight SRV.
        stager_.stageSrv(nv12Texture, &lumaSrv, yuvPass_.heap.Get(), 3 + parity * 2);
        D3D12_SHADER_RESOURCE_VIEW_DESC chromaSrv{};
        chromaSrv.Format = nv12Texture->GetDesc().Format==DXGI_FORMAT_P010?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM;
        chromaSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        chromaSrv.Texture2D.MostDetailedMip = 0;
        chromaSrv.Texture2D.MipLevels = 1;
        chromaSrv.Texture2D.ResourceMinLODClamp = 0.0f;
        if (isArray) {
            chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            chromaSrv.Texture2DArray.MostDetailedMip = 0;
            chromaSrv.Texture2DArray.MipLevels = 1;
            chromaSrv.Texture2DArray.FirstArraySlice = nv12Slice;
            chromaSrv.Texture2DArray.ArraySize = 1;
            chromaSrv.Texture2DArray.PlaneSlice = 1;
        } else {
            chromaSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            chromaSrv.Texture2D.MostDetailedMip = 0;
            chromaSrv.Texture2D.MipLevels = 1;
            chromaSrv.Texture2D.PlaneSlice = 1;
        }
        stager_.stageSrv(nv12Texture, &chromaSrv, yuvPass_.heap.Get(), 4 + parity * 2);
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        const auto uploadFormat=desc_.captureBitDepth==16?AV_PIX_FMT_P016:desc_.wideYuvInput()?AV_PIX_FMT_P010:AV_PIX_FMT_NV12;
        const bool sameLayout=frame->format==uploadFormat;
        const unsigned sampleBytes=desc_.wideYuvInput()?2u:1u;
        const bool directUpload=sameLayout||(!desc_.wideYuvInput()&&(frame->format==AV_PIX_FMT_YUV420P||frame->format==AV_PIX_FMT_YUVJ420P));
        uint8_t* planes[4] = { directUpload?mappedLuma_[parity]:nv12Buf_.data(), directUpload?mappedChroma_[parity]:nv12Buf_.data() + lumaSize_ };
        const int strides[4] = { static_cast<int>(lumaPitch_), static_cast<int>(chromaPitch_) };
        if(sameLayout){
            // Native semiplanar capture already matches the upload textures.
            // Copy rows once, preserving every code value and signed stride.
            const size_t rowBytes[2]={size_t(srcW_)*sampleBytes,size_t((srcW_+1)/2)*2*sampleBytes};
            const unsigned rows[2]={srcH_,(srcH_+1)/2};
            for(unsigned plane=0;plane<2;++plane){
                const auto pitch=int64_t(frame->linesize[plane]);
                if(!frame->data[plane]||uint64_t(pitch<0?-pitch:pitch)<rowBytes[plane]){
                    veyra::log::error("color","Invalid native YUV plane or row stride");return false;
                }
            }
            for(unsigned plane=0;plane<2;++plane){
                // Capture frames are allocated with the upload pitch: copy the
                // whole plane in one streaming memcpy (all but the last row's
                // padding is a superset of rowBytes).
                if(frame->linesize[plane]==int(strides[plane])&&rows[plane]>1)
                    std::memcpy(planes[plane],frame->data[plane],size_t(strides[plane])*(rows[plane]-1)+rowBytes[plane]);
                else for(unsigned y=0;y<rows[plane];++y)
                    std::memcpy(planes[plane]+size_t(y)*strides[plane],frame->data[plane]+ptrdiff_t(y)*frame->linesize[plane],rowBytes[plane]);
            }
        }else{
        nv12Ctx_ = sws_getCachedContext(nv12Ctx_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format),
            frame->width, frame->height, uploadFormat, SWS_POINT,
            nullptr, nullptr, nullptr);
        if (nv12Ctx_ == nullptr) { veyra::log::error("graph", "sws"); return false; }
        const int colorSpace=resolved.matrix==YuvMatrix::BT2020NCL?SWS_CS_BT2020:resolved.matrix==YuvMatrix::BT601?SWS_CS_ITU601:SWS_CS_ITU709;
        const int* coefficients=sws_getCoefficients(colorSpace);
        const int full=resolved.range==ColorRange::Full?1:0;
        if(sws_setColorspaceDetails(nv12Ctx_,coefficients,full,coefficients,full,0,1<<16,1<<16)<0)return false;
        if(sws_scale(nv12Ctx_, frame->data, frame->linesize, 0, frame->height, planes, strides)!=frame->height){veyra::log::error("color","Software plane conversion failed");return false;}
        }
        // Analyze CPU luma, never write-combined upload memory. The high byte
        // of P010/P016 retains the existing 8-bit scene/cadence proxy exactly.
        const auto* sampleLuma=directUpload?frame->data[0]:planes[0];
        const auto samplePitch=directUpload?ptrdiff_t(frame->linesize[0]):ptrdiff_t(lumaPitch_);
        auto& sample=lumaSample_;sample.clear();sample.reserve(64*36);
        for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x)
            sample.push_back(sampleLuma[ptrdiff_t(y*srcH_/36)*samplePitch+(x*srcW_/64)*sampleBytes+(sampleBytes-1)]);
        analyzeLuma(sample);
        if(!directUpload){for (uint32_t y = 0; y < srcH_; ++y)
            std::memcpy(mappedLuma_[parity] + y * lumaPitch_, planes[0] + y * lumaPitch_, srcW_*(desc_.wideYuvInput()?2:1));
        for (uint32_t y = 0; y < (srcH_+1) / 2; ++y)
            std::memcpy(mappedChroma_[parity] + y * chromaPitch_, planes[1] + y * chromaPitch_, ((srcW_+1)/2)*(desc_.wideYuvInput()?4:2));}
        auto copyPlane = [&](ID3D12Resource* texture, ID3D12Resource* upload,
                             DXGI_FORMAT format, UINT width, UINT height, UINT pitch) {
            tracker_.transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
            dst.pResource = texture;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource = upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint = {format, width, height, 1, pitch};
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        };
        copyPlane(lumaTex_.Get(), upLuma_[parity].Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,
                  srcW_, srcH_, static_cast<UINT>(lumaPitch_));
        copyPlane(chromaTex_.Get(), upChroma_[parity].Get(), desc_.wideYuvInput()?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,
                  (srcW_+1)/2, (srcH_+1)/2, static_cast<UINT>(chromaPitch_));
        tracker_.transition(list, lumaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, chromaTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("upload");
    cpuTrace.mark("upload");

    // 2. YUV -> RGBA16F.
    if(colorActive_&&colorDirty_)uploadColorTables(list);
    auto convertInput=[&](bool grade){
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(desc_.rgbInput||desc_.yuy2Input||desc_.packedInput){
        float c[8+kColorGradeConstantCount]={uintBits(srcW_),uintBits(srcH_),uintBits(workingTransferCode(resolved)),uintBits(resolved.range==ColorRange::Limited?1u:0u),
            resolved.range==ColorRange::Full?0.0f:1.0f,resolved.matrix==YuvMatrix::BT2020NCL?2.0f:resolved.matrix==YuvMatrix::BT601?0.0f:1.0f,resolved.primaries==ColorPrimaries::BT2020?1.0f:0.0f,uintBits(desc_.packedInput)};
        if(grade)packColorGradeConstants(colorTables_,c+8);
        if(resolved.scRgb){c[4]=desc_.hdrWorking()?1.f:2.f;c[5]=toneMapPeakNits_;}
        rgbPass_.bind(list,c,gpuHandleOf(rgbPass_,gpuRgb?3+parity:0).ptr,gpuHandleOf(rgbPass_,1).ptr,gpuHandleOf(rgbPass_,8).ptr);
        list->Dispatch((srcW_+15)/16,(srcH_+15)/16,1);
    }else{
        float constants[12+kColorGradeConstantCount] = { resolved.range==ColorRange::Full?0.0f:1.0f,
            resolved.matrix==YuvMatrix::BT2020NCL?2.0f:resolved.matrix==YuvMatrix::BT601?0.0f:1.0f,
            resolved.transfer==TransferFunction::HLG?5.0f:resolved.transfer==TransferFunction::PQ?4.0f:float(workingTransferCode(resolved)), (nv12Texture?(nv12Texture->GetDesc().Format==DXGI_FORMAT_P010?1.0f:0.0f):(desc_.captureBitDepth==16?2.0f:desc_.wideYuvInput()?1.0f:0.0f)),
            uintBits(srcW_), uintBits(srcH_), uintBits((desc_.hdrWorking()?1u:0u)|(resolved.primaries==ColorPrimaries::BT2020?2u:0u)),
            uintBits(resolved.reconstructChroma?std::max(1u,unsigned(resolved.chromaLocation)):0u),toneMapPeakNits_,203.0f,0,0 };
        if(grade)packColorGradeConstants(colorTables_,constants+12);
        yuvPass_.bind(list, constants, gpuHandleOf(yuvPass_, nv12Texture ? 3 + parity * 2 : 0).ptr, gpuHandleOf(yuvPass_, 2).ptr,gpuHandleOf(yuvPass_,8).ptr);
        list->Dispatch((srcW_ + 15) / 16, (srcH_ + 15) / 16, 1);
    }
    tracker_.uavBarrier(list, srcRgba_.Get());
    };
    // Grading is fused into input conversion. Retain a genuinely ungraded
    // reference only when comparison requests it, then run the normal input.
    if(colorActive_&&retainReferences){
        convertInput(false);
        tracker_.transition(list,srcRgba_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        tracker_.transition(list,sourceReferences_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(sourceReferences_[parity].Get(),srcRgba_.Get());
        tracker_.transition(list,sourceReferences_[parity].Get(),D3D12_RESOURCE_STATE_COMMON);
    }
    convertInput(colorActive_);
    tracker_.transition(list, srcRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(gpuRgb)tracker_.transition(list,hardwareSurface->texture,D3D12_RESOURCE_STATE_COMMON);
    if (nv12Texture != nullptr) {
        tracker_.transition(list, nv12Texture, D3D12_RESOURCE_STATE_COMMON);
    }
    gpuTimer_.mark(list,GpuStage::Color,true);
    if (desc_.stageMark) desc_.stageMark("yuv");
    cpuTrace.mark("color");

    auto runVideoSr=[&]()->bool{
        gpuTimer_.mark(list,GpuStage::Sr);
        tracker_.transition(list,videoSrInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float enc[8]={uintBits(srcW_),uintBits(srcH_),uintBits(srcW_),uintBits(srcH_),desc_.hdrWorking()?3.0f:1.0f,0,0,0};
        blitPass_.bind(list,enc,gpuHandleOf(blitPass_,desc_.nrBeforeSr?18:0).ptr,gpuHandleOf(blitPass_,16).ptr);list->Dispatch((srcW_+15)/16,(srcH_+15)/16,1);tracker_.uavBarrier(list,videoSrInput_.Get());
        tracker_.transition(list,videoSrInput_.Get(),D3D12_RESOURCE_STATE_COMMON);tracker_.transition(list,videoSrOutput_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(!videoSrBackend_->evaluate(list,videoSrInput_.Get(),videoSrOutput_.Get(),desc_.videoSrQuality)){failedBackend_=engine::FailedBackend::Sr;return false;}
        tracker_.uavBarrier(list,videoSrOutput_.Get());tracker_.transition(list,videoSrOutput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float dec[8]={uintBits(workW_),uintBits(workH_),uintBits(workW_),uintBits(workH_),-1,0,0,0};
        if(desc_.hdrWorking()){
            tracker_.transition(list,videoSrInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            hdrVideoSrPass_.bind(list,dec,gpuHandleOf(hdrVideoSrPass_,desc_.nrBeforeSr?4:0).ptr,gpuHandleOf(hdrVideoSrPass_,3).ptr);
        }else blitPass_.bind(list,dec,gpuHandleOf(blitPass_,17).ptr,gpuHandleOf(blitPass_,1).ptr);list->Dispatch((workW_+15)/16,(workH_+15)/16,1);tracker_.uavBarrier(list,workRgba_.Get());tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ++metrics_.srEvaluateCount;gpuTimer_.mark(list,GpuStage::Sr,true);
        return true;
    };
    // Guidance from original source-space color before SR/NR. Queue waits are GPU-side.
    bool haveFlow = false;
    const bool runMotion = nvofStandalone_ || presentSinkFg() || fgEnabled_ || (srEnabled_ && (desc_.videoSrQuality==0||desc_.videoSrQuality==engine::kVideoSrFsr));
    if (runMotion && (gpuDis_ || amdOf_ || (nvof_ && nvof_->initialized()))) {
        const float dims[8] = {uintBits(nvofW_),uintBits(nvofH_),uintBits(nvofW_),uintBits(nvofH_),0,0,0,0};
        if (prevValid_) {
            tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            blitPass_.bind(list,dims,gpuHandleOf(blitPass_,5).ptr,gpuHandleOf(blitPass_,6).ptr);
            list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
            tracker_.uavBarrier(list,nvofInA_.Get());
            tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_COMMON);
        }
        tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float encodedDims[8] = {uintBits(srcW_),uintBits(srcH_),uintBits(nvofW_),uintBits(nvofH_),desc_.hdrWorking()?3.0f:1.0f,0,0,0};
        blitPass_.bind(list,encodedDims,gpuHandleOf(blitPass_,15).ptr,gpuHandleOf(blitPass_,9).ptr);
        list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
        tracker_.uavBarrier(list,nvofInB_.Get());
        tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(amdOf_){
            gpuTimer_.mark(list,GpuStage::Flow);
            if(!amdOf_->dispatch(list,nvofInB_.Get(),!prevValid_))return false;
            ++metrics_.amdOfExecuteCount;haveFlow=prevValid_;
            gpuTimer_.mark(list,GpuStage::Flow,true);
        }
        if(gpuDis_ && prevValid_){
            gpuTimer_.mark(list,GpuStage::Flow);
            tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            // The next ring submission signals this value; the parity upload
            // fence above retires all uses before these descriptors are reused.
            if(!gpuDis_->dispatch(list,context_.directQueue(),context_.fence(),ring_.lastSignaledValue()+1,parity,
                nvofInA_.Get(),nvofInB_.Get(),flowTex_.Get(),confTex_.Get(),previousSource_,sourceFrameId))return false;
            tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ++metrics_.gpuDisExecuteCount;haveFlow=true;
            gpuTimer_.mark(list,GpuStage::Flow,true);
        }
        if (prevValid_ && !gpuDis_) {
            if(!amdOf_){
            gpuTimer_.mark(list,GpuStage::Flow);
            if(!ring_.submitAndSignal(slot))return false;
            cpuTrace.mark("flowPrepare");
            haveFlow=nvof_->execute(ring_.lastSignaledValue(),st);
            cpuTrace.mark("nvofExecute");
            if(!haveFlow) { ++metrics_.nvofFrameFailures; mvecSource_="zero-motion-fallback (NVOF execute failed)"; }
            else {
                ++metrics_.nvofExecuteCount;
                auto waitForFlow=[&](){
                    const auto value=nvof_->nextOutValue()-1;
                    const HRESULT hr=context_.directQueue()->Wait(nvofOutFence_.Get(),value);
                    if(FAILED(hr))veyra::log::error("graph",std::format("NVOF output queue wait value={} hr=0x{:X}",value,unsigned(hr)));
                    return SUCCEEDED(hr);
                };
                if(!waitForFlow())return false;
            }
            list=ring_.acquireNext(slot,st);if(!list)return false;
            gpuTimer_.mark(list,GpuStage::Flow,true);
            }
            if(haveFlow) {
                if(amdOf_)tracker_.transition(list,amdOf_->sceneChanges(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofRawTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofCostTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                const float dc[8]={uintBits(rawW_),uintBits(rawH_),uintBits(nvofW_),uintBits(nvofH_),
                    uintBits(selectedGrid_),uintBits(0),uintBits(32),uintBits((desc_.validateMotion?3u:0u)|(amdOf_?4u:0u)|(amdOf_&&!amdOf_->warmedUp()?8u:0u))};
                densifyPass_.bind(list,dc,gpuHandleOf(densifyPass_,0).ptr,gpuHandleOf(densifyPass_,5).ptr);
                list->Dispatch((nvofW_+15)/16,(nvofH_+15)/16,1);
                tracker_.uavBarrier(list,flowTex_.Get());tracker_.uavBarrier(list,confTex_.Get());
                tracker_.transition(list,nvofInA_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofInB_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofRawTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
                if(amdOf_)tracker_.transition(list,amdOf_->sceneChanges(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,nvofCostTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
                tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                tracker_.transition(list,confTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
            }
        }
    }

    // 3. SR into workRgba (or 1:1 blit bypass).
    cpuTrace.mark("flowFinish");
    if(haveFlow){
        auto adapt=[&](ID3D12Resource* output,uint32_t w,uint32_t h,uint32_t descriptor){
            tracker_.transition(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float c[8]={uintBits(nvofW_),uintBits(nvofH_),uintBits(w),uintBits(h),0,0,0,0};
            flowAdaptPass_.bind(list,c,gpuHandleOf(flowAdaptPass_,0).ptr,gpuHandleOf(flowAdaptPass_,descriptor).ptr);
            list->Dispatch((w+15)/16,(h+15)/16,1);tracker_.uavBarrier(list,output);tracker_.transition(list,output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };
        adapt(nrFlow_.Get(),nrW_,nrH_,1);adapt(baseFlow_.Get(),workW_,workH_,2);
    }
    if(reset){++metrics_.resetCount;++epoch_;}
    out.historyReset=reset;
    out.batch.batchId=realFrameIndex_+1;out.batch.identity={epoch_,desc_.settingsRevision,sourceFrameId};
    out.batch.a100ns=static_cast<int64_t>(std::llround(prevPtsMs_*10000));
    out.batch.b100ns=static_cast<int64_t>(std::llround(ptsMs*10000));gpuTimer_.identity(out.batch.identity);
    auto runSr=[&]()->bool{
    if(srEnabled_&&fsrSrEnabled()&&haveFlow){
        // AMD FSR upscaling: FidelityFX effect, render-extent color + guidance
        // motion (previous = current + motion, same convention as the frame
        // generator) straight into the working texture. A frame without valid
        // motion falls through to the plain blit below instead of guessing.
        gpuTimer_.mark(list,GpuStage::Sr);
        const double deltaMs = (prevPtsMs_>=0.0&&ptsMs>prevPtsMs_)?(ptsMs-prevPtsMs_):16.6;
        tracker_.transition(list,srcRgba_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list,flowTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list,fsrSrDepth_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(!fsrSrBackend_->evaluate(list,srcRgba_.Get(),flowTex_.Get(),fsrSrDepth_.Get(),workRgba_.Get(),
                                    srcW_,srcH_,workW_,workH_,reset,float(deltaMs))){
            failedBackend_=engine::FailedBackend::Sr;
            return false;
        }
        ++metrics_.srEvaluateCount;gpuTimer_.mark(list,GpuStage::Sr,true);
        tracker_.uavBarrier(list,workRgba_.Get());
        tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else if(srEnabled_&&videoSrBackend_){
        if(!runVideoSr())return false;
    } else if (srEnabled_ && srBackend_ && srBackend_->created()) {
        gpuTimer_.mark(list,GpuStage::Sr);
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssSrBackend::EvalDesc ed{};
        ed.color = desc_.nrBeforeSr?residualRgba_.Get():srcRgba_.Get();
        ed.output = workRgba_.Get();
        ed.depth = nrZeroDepth_.Get();
        ed.motionVectors = haveFlow?baseFlow_.Get():nrZeroMotion_.Get();
        if(desc_.srMotionProbe)ed.motionVectors=desc_.srMotionProbe;
        ed.reset = reset;
        if (!srBackend_->evaluate(list, ngxParams_, ed, st)) {
            failedBackend_=engine::FailedBackend::Sr;
            veyra::log::error("graph", "sr evaluate failed");
            return false;
        }
        ++metrics_.srEvaluateCount;gpuTimer_.mark(list,GpuStage::Sr,true);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else if(workRgba_.Get()!=srcRgba_.Get()) {
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            uintBits(srcW_), uintBits(srcH_),
            uintBits(workW_), uintBits(workH_), 0, 0, 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, 0).ptr, gpuHandleOf(blitPass_, 1).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, workRgba_.Get());
        tracker_.transition(list, workRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (desc_.stageMark) desc_.stageMark("sr");
    cpuTrace.mark("sr");
    return true;};
    if(!desc_.nrBeforeSr&&!runSr())return false;
    if(desc_.nrBeforeSr&&retainReferences){
        tracker_.transition(list,workRgba_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float c[8]={uintBits(srcW_),uintBits(srcH_),uintBits(workW_),uintBits(workH_),0,0,0,0};
        blitPass_.bind(list,c,gpuHandleOf(blitPass_,0).ptr,gpuHandleOf(blitPass_,1).ptr);list->Dispatch((workW_+15)/16,(workH_+15)/16,1);tracker_.uavBarrier(list,workRgba_.Get());
    }
    if(retainReferences){
        // Comparison references belong to this real-frame lease. Avoid the two
        // full-frame copies when comparison is disabled.
        auto retain=[&](ID3D12Resource* src,ID3D12Resource* dst){tracker_.transition(list,src,D3D12_RESOURCE_STATE_COPY_SOURCE);tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(dst,src);tracker_.transition(list,dst,D3D12_RESOURCE_STATE_COMMON);tracker_.transition(list,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
        if(!colorActive_)retain(srcRgba_.Get(),sourceReferences_[parity].Get());
        if(baseReferences_[parity].Get()!=sourceReferences_[parity].Get())retain(workRgba_.Get(),baseReferences_[parity].Get());
    }


    // 4a. Parity encode.
    if (nrEnabled_ && nrHandle_ != nullptr) {
        if(nrInput_.Get()!=(desc_.nrBeforeSr?srcRgba_.Get():workRgba_.Get())){
        tracker_.transition(list,nrInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float down[8]={uintBits(desc_.nrBeforeSr?srcW_:workW_),uintBits(desc_.nrBeforeSr?srcH_:workH_),uintBits(nrW_),uintBits(nrH_),0,0,0,0};
        downsamplePass_.bind(list,down,gpuHandleOf(downsamplePass_,0).ptr,gpuHandleOf(downsamplePass_,1).ptr);
        list->Dispatch((nrW_+15)/16,(nrH_+15)/16,1);tracker_.uavBarrier(list,nrInput_.Get());tracker_.transition(list,nrInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = { 1.0f, 1.0f, 1.0f, desc_.hdrWorking()?1.0f:0.0f,
            uintBits(nrW_), uintBits(nrH_), 0.0f, 0.0f };
        encPass_.bind(list, constants, gpuHandleOf(encPass_, 0).ptr, gpuHandleOf(encPass_, 1).ptr);
        list->Dispatch((nrW_ + 15) / 16, (nrH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, proxyTex_.Get());
        tracker_.transition(list, proxyTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (desc_.stageMark) desc_.stageMark("encode");
        cpuTrace.mark("nrPrepare");

        // 4b. NR evaluate on a FRESH list (snippet constraint).
        if (!ring_.submitAndSignal(slot)) { veyra::log::error("graph", "submit before nr"); return false; }
        ID3D12GraphicsCommandList* nlist = ring_.acquireNext(slot, st);
        if (nlist == nullptr) { veyra::log::error("graph", "acquire nr list"); return false; }
        cpuTrace.mark("nrAcquire");
        {
            namespace p = ngx::dlssnr;
            ngx::ParameterBlock pb(ngxParams_);
            pb.setD3D12Resource(p::kColor, proxyTex_.Get());
            pb.setD3D12Resource(p::kOutput, neuralTex_.Get());
            pb.setD3D12Resource(p::kMVec, haveFlow ? nrFlow_.Get() : nrZeroMotion_.Get());
            pb.setD3D12Resource(p::kDepth, nrZeroDepth_.Get());
            pb.setU32(p::kColorSubrectWidth, nrW_); pb.setU32(p::kColorSubrectHeight, nrH_);
            pb.setU32(p::kOutputSubrectWidth, nrW_); pb.setU32(p::kOutputSubrectHeight, nrH_);
            pb.setU32(p::kMVecSubrectWidth, nrW_); pb.setU32(p::kMVecSubrectHeight, nrH_);
            pb.setU32(p::kDepthSubrectWidth, nrW_); pb.setU32(p::kDepthSubrectHeight, nrH_);
            pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
            pb.setI32(p::kDepthInverted, 1);
            pb.setI32(p::kIndicatorInvertX, 0);
            pb.setI32(p::kIndicatorInvertY, 0);
            pb.setI32(p::kEnabled, 1);
            pb.setI32(p::kReset, reset ? 1 : 0);
            pb.setI32(p::kStyle, desc_.model.style);
            pb.setF32(p::kIntensity, desc_.model.intensity);
            pb.setF32(p::kLocalToneStrength, desc_.model.tone);
            pb.setF32(p::kLocalStructureStrength, desc_.model.structure);
            pb.setF32(p::kSkinStructureStrength, desc_.model.skin);
            pb.setI32(p::kUseAutoMask, desc_.model.autoMask);
            pb.setI32(p::kUICorrection, desc_.model.uiCorrection);
            if(desc_.nrParameterProbe)desc_.nrParameterProbe(ngxParams_,proxyTex_.Get(),neuralTex_.Get(),nrW_,nrH_);
            static bool injectedFailureConsumed=false;
            if(!injectedFailureConsumed&&desc_.model.style==2&&GetEnvironmentVariableW(L"VEYRA_TEST_REJECT_NR_STYLE2",nullptr,0)){
                injectedFailureConsumed=true;veyra::log::error("settings-test","injected NR execution rejection before NGX; rollback exercise, not a hardware failure");return false;
            }
            if(GetEnvironmentVariableW(L"VEYRA_TEST_NR_RUNTIME_FAILURE",nullptr,0)){
                failedBackend_=engine::FailedBackend::Nr;
                veyra::log::error("backend-recovery-test","test-only NR runtime rejection before Evaluate; exercises partially recorded command cleanup, not a hardware failure");return false;
            }
            gpuTimer_.mark(nlist,GpuStage::Nr);
            uint64_t er = 0; uint32_t es = 0;
            cpuTrace.mark("nrParameters");
            if (!nrAdapter_->snippetEvaluateFeature(nlist, nrHandle_, ngxParams_, er, es) ||
                er != static_cast<uint64_t>(NVSDK_NGX_Result_Success)) {
                diagnostic.stage="NR Evaluate";diagnostic.ngx=er;diagnostic.seh=es;Logger::diagnosticContext(diagnostic);veyra::log::error("graph", std::format("NR evaluate failed 0x{:X} seh={}", er, es));
                failedBackend_=engine::FailedBackend::Nr;
                return false;
            }
            cpuTrace.mark("nrEvaluate");
            gpuTimer_.mark(nlist,GpuStage::Nr,true);
            ++metrics_.nrEvaluateCount;
            if(haveFlow)++metrics_.nrMotionFrames;
            tracker_.uavBarrier(nlist, neuralTex_.Get());
            tracker_.transition(nlist, neuralTex_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            wchar_t profileFlag[2]{};
            if(GetEnvironmentVariableW(L"VEYRA_PROFILE_SPLIT",profileFlag,2)&&profileFlag[0]==L'1'){
                ring_.tag(slot,"nr-only");
                if(!ring_.submitAndSignal(slot))return false;
                nlist=ring_.acquireNext(slot,st);if(!nlist)return false;
                ring_.tag(slot,"decode-output-fg");
            }
            // 4c. Parity decode.
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const float constants[8] = { 1.0f, 1.0f, 1.0f, desc_.hdrWorking()?1.0f:0.0f,
                uintBits(nrW_), uintBits(nrH_), 0.0f, 0.0f };
            decPass_.bind(nlist, constants, gpuHandleOf(decPass_, 0).ptr, gpuHandleOf(decPass_, 3).ptr);
            nlist->Dispatch((nrW_ + 15) / 16, (nrH_ + 15) / 16, 1);
            tracker_.uavBarrier(nlist, finalRgba_.Get());
            tracker_.transition(nlist, finalRgba_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        list = nlist; // continue recording on the NR list
    }
    if (desc_.stageMark) desc_.stageMark("nr");
    cpuTrace.mark("nr");

    if(nrEnabled_&&nrHandle_){
        gpuTimer_.mark(list,GpuStage::Residual);
        auto* raw=desc_.nrTemporal?nrTemporal_.raw():residualRgba_.Get();
        tracker_.transition(list,raw,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const auto& r=desc_.residual;float c[24]={r.total,r.darken,r.brighten,r.color,r.luminance,desc_.protection.enabled?1.0f:0.0f,desc_.protection.featherPixels,desc_.hdrWorking()?1.0f:0.0f};
        for(size_t i=0;i<4;++i){const auto q=desc_.protection.regions[i];c[8+i*4]=q.left;c[9+i*4]=q.top;c[10+i*4]=q.right;c[11+i*4]=q.bottom;}
        residualPass_.bind(list,c,gpuHandleOf(residualPass_,0).ptr,gpuHandleOf(residualPass_,3).ptr);
        list->Dispatch(((desc_.nrBeforeSr?srcW_:workW_)+15)/16,((desc_.nrBeforeSr?srcH_:workH_)+15)/16,1);tracker_.uavBarrier(list,raw);tracker_.transition(list,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(desc_.nrTemporal)nrTemporal_.run(list,tracker_,reset,haveFlow,ptsMs-prevPtsMs_,r.total,desc_.protection);
        gpuTimer_.mark(list,GpuStage::Residual,true);
    }

    if(desc_.nrBeforeSr&&!runSr())return false;
    if(videoHdrBackend_){
        gpuTimer_.mark(list,GpuStage::VideoHdr);
        tracker_.transition(list,videoHdrInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float encode[8]={uintBits(workW_),uintBits(workH_),uintBits(workW_),uintBits(workH_),1,0,0,0};
        blitPass_.bind(list,encode,gpuHandleOf(blitPass_,2).ptr,gpuHandleOf(blitPass_,19).ptr);
        list->Dispatch((workW_+15)/16,(workH_+15)/16,1);tracker_.uavBarrier(list,videoHdrInput_.Get());
        tracker_.transition(list,videoHdrInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list,videoHdrOutput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(!videoHdrBackend_->evaluate(list,videoHdrInput_.Get(),videoHdrOutput_.Get(),desc_.videoHdr)){
            failedBackend_=engine::FailedBackend::VideoHdr;return false;
        }
        tracker_.uavBarrier(list,videoHdrOutput_.Get());
        tracker_.transition(list,videoHdrOutput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpuTimer_.mark(list,GpuStage::VideoHdr,true);
    }
    // 5. Working frame -> SDR RGBA8 videoFrame[parity] + NVOF chain.
    {
        const UINT srcSlot = videoHdrBackend_?20:2;
        const UINT uavSlot = 3 + parity;
        tracker_.transition(list, (nrEnabled_ && nrHandle_ != nullptr && !desc_.nrBeforeSr) ? residualRgba_.Get() : workRgba_.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float constants[8] = {
            uintBits(workW_), uintBits(workH_),
            uintBits(workW_), uintBits(workH_), hdr10Output()?2.0f:desc_.hdrOutput?0.0f:1.0f, outputDitherStep(), 0, 0 };
        blitPass_.bind(list, constants, gpuHandleOf(blitPass_, srcSlot).ptr, gpuHandleOf(blitPass_, uavSlot).ptr);
        list->Dispatch((workW_ + 15) / 16, (workH_ + 15) / 16, 1);
        tracker_.uavBarrier(list, videoFrame_[parity].Get());
        tracker_.transition(list, videoFrame_[parity].Get(), D3D12_RESOURCE_STATE_COMMON);

    }
    if(presentSinkFg()){
        auto* input=haveFlow&&!reset?baseFlow_.Get():nrZeroMotion_.Get();
        tracker_.transition(list,input,D3D12_RESOURCE_STATE_COPY_SOURCE);
        tracker_.transition(list,presentMotion_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(presentMotion_[parity].Get(),input);
        tracker_.transition(list,input,D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(list,presentMotion_[parity].Get(),D3D12_RESOURCE_STATE_COMMON);
        presentMotionValid_[parity]=haveFlow&&!reset;
        motionPreviousSource_[parity]=previousSource_;
    }
    previousSource_=sourceFrameId;
    const bool fgBackendAvailable=fgEnabled_&&(fgBackend_&&fgBackend_->created());
    const unsigned fgMultiplier=previewMultiplier&&fgBackendAvailable?std::clamp(previewMultiplier,2u,desc_.fgMultiplier):desc_.fgMultiplier;
    out.fgCandidates=fgBackendAvailable?fgMultiplier-1:0;
    bool resetFg=reset||!prevValid_||fgHistorySkipped_;
    const auto decision=fgBackendAvailable?(admitFg?admitFg(out.batch,resetFg):FgDecision::Evaluate):FgDecision::Skip;
    const bool runFg=decision!=FgDecision::Skip;
    // An overloaded preview pair may afford one reset evaluation now. Seed
    // on this real input so the following pair does not lose another group.
    const bool reseedRejected=decision==FgDecision::Seed;
    out.fgBudgetSeed=reseedRejected;
    if(reseedRejected)resetFg=true;
    // Reduced groups only make sense with valid history; on a reset the
    // seed path already runs a single evaluation.
    const bool reduced=decision==FgDecision::Reduced&&!resetFg;
    out.fgReduced=reduced;
    // A reset has no usable A/B pair. Seed one complete 2X evaluation group;
    // all later subframes would repeat reset work and cannot be presented.
    const uint32_t fgCalls=(resetFg||reduced)?1:fgMultiplier-1;
    const unsigned outputMultiplier=reduced?2u:fgMultiplier;
    if(runFg&&resetFg){
        if(reseedRejected)out.fgSkippedBeforeEval=out.fgCandidates-fgCalls;
        else out.fgSkippedForReset=out.fgCandidates-fgCalls;
    }
    if(reduced)out.fgSkippedBeforeEval=out.fgCandidates-fgCalls;
    out.fgRecovery=runFg&&(fgHistorySkipped_||reseedRejected);
    if(fgBackendAvailable&&!runFg){out.fgSkippedBeforeEval=out.fgCandidates;fgHistorySkipped_=true;}
    if(!runFg)gpuTimer_.resolve(list);
    if (!ring_.submitAndSignal(slot)) return false;
    if(!runFg)gpuTimer_.submitted(ring_.lastSignaledValue());
    out.videoFenceValue = ring_.lastSignaledValue();
    out.videoFenceObject = context_.fence();

    // FG reads enhanced SDR color, not the pre-NR guidance input.
    if (runFg && fgBackend_ && fgBackend_->created()) {
      for(uint32_t sub=1;sub<=fgCalls;++sub){
        const uint32_t generatedSlot=parity+(sub-1)*2;
        auto* flist=ring_.acquireNext(slot,st); if(!flist)return false;
        auto* motion=desc_.fgMotionProbe?desc_.fgMotionProbe:(haveFlow?baseFlow_.Get():nrZeroMotion_.Get());
        tracker_.transition(flist,videoFrame_[parity].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!desc_.fgMotionProbe)tracker_.transition(flist,motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,depthTex_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        tracker_.transition(flist,genFrame_[generatedSlot].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // One status resource for the whole input-frame pair, just like the
        // unchanged input parameters. Later MFG calls may retain its value.
        if(sub==1){
            tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_COPY_DEST);
            flist->CopyBufferRegion(fgDisable_[parity].Get(),0,fgDisableInit_.Get(),0,4);
        }
        tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ngx::DlssFgBackend::EvalDesc fe{};
        fe.hdr=hdr10Output();fe.backbuffer=videoFrame_[parity].Get(); fe.depth=depthTex_.Get(); fe.mvecs=motion;
        fe.outputInterpolated=genFrame_[generatedSlot].Get(); fe.reset=resetFg;
        fe.outputDisableInterpolation=fgDisable_[parity].Get();
        fe.multiFrameCount=fgCalls;fe.multiFrameIndex=sub;
        fe.frameId=realFrameIndex_+1;
        // FG consumer adapter: current->previous pixel flow -> normalized reverse flow.
        fe.mvecScaleX=(haveFlow||desc_.fgMotionProbe)?-1.0f/static_cast<float>(workW_):1.0f;
        fe.mvecScaleY=(haveFlow||desc_.fgMotionProbe)?-1.0f/static_cast<float>(workH_):1.0f;
        if(sub==1)gpuTimer_.mark(flist,GpuStage::FgBatch);
        const auto timingStage=static_cast<GpuStage>(unsigned(GpuStage::Fg1)+sub-1);gpuTimer_.mark(flist,timingStage);
        cpuTrace.mark("fgPrepare");
        if(!fgBackend_->evaluate(flist,ngxParams_,fe,st)){failedBackend_=engine::FailedBackend::Fg;return false;}
        cpuTrace.mark("fgEvaluate");
        ++out.fgEvaluated;
        gpuTimer_.mark(flist,timingStage,true);if(sub==fgCalls)gpuTimer_.mark(flist,GpuStage::FgBatch,true);
        tracker_.uavBarrier(flist,genFrame_[generatedSlot].Get());
        tracker_.uavBarrier(flist,fgDisable_[parity].Get());
        tracker_.transition(flist,fgDisable_[parity].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        flist->CopyBufferRegion(fgDisableReadback_[generatedSlot].Get(),0,fgDisable_[parity].Get(),0,4);
        tracker_.transition(flist,videoFrame_[parity].Get(),D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist,genFrame_[generatedSlot].Get(),D3D12_RESOURCE_STATE_COMMON);
        tracker_.transition(flist,depthTex_.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(sub==fgCalls)gpuTimer_.resolve(flist);
        if(!ring_.submitAndSignal(slot))return false;
        if(sub==fgCalls)gpuTimer_.submitted(ring_.lastSignaledValue());
        out.hasGenerated=!resetFg;
        out.genSlot=parity; out.genFenceValue=ring_.lastSignaledValue();
        out.genFenceObject=context_.fence();
        out.generatedPtsMs=(prevPtsMs_+ptsMs)*0.5;
        if(out.hasGenerated){
            ++metrics_.fgSubmittedCandidates;
            BatchFrame f;f.identity=out.batch.identity;f.kind=FrameKind::Generated;f.validity=GenerationValidity::Pending;f.subframe=sub;
            f.pts100ns=FrameBatch::interpolate(out.batch.a100ns,out.batch.b100ns,sub,outputMultiplier);
            f.lease=std::make_shared<FrameLease>();f.lease->texture=genFrame_[generatedSlot];f.lease->slot=generatedSlot;f.lease->readyFence=out.genFenceValue;f.lease->readyFenceObject=out.genFenceObject;generatedLeases_[generatedSlot]=f.lease;out.batch.append(std::move(f));
        }
      }
    }
    failedBackend_=engine::FailedBackend::None;
    if(runFg)fgHistorySkipped_=false;
    // uploadFences_ doubles as the DLSS presentation-queue handoff fence for
    // the real frame (presentationReadyFence): it must cover the FG lists that
    // transition videoFrame_[parity] back to COMMON. Recording it before FG
    // (sweep A2, tried 2026-09-22) raised cross-queue barrier errors.
    uploadFences_[parity]=ring_.lastSignaledValue();

    prevPtsMs_ = ptsMs;
    prevValid_ = true;
    ++realFrameIndex_;
    out.realFrameIndex = realFrameIndex_;
    out.videoSlot = parity;
    BatchFrame real;real.identity=out.batch.identity;real.pts100ns=out.batch.b100ns;real.lease=std::make_shared<FrameLease>();real.lease->texture=videoFrame_[parity];real.lease->sourceReference=sourceReferences_[parity];real.lease->baseReference=baseReferences_[parity];real.lease->slot=parity;real.lease->readyFence=out.videoFenceValue;real.lease->readyFenceObject=out.videoFenceObject;real.lease->referencesValid=retainReferences;realLeases_[parity]=real.lease;out.batch.append(std::move(real));
    if(veyra::log::verboseFrameLogs())veyra::log::info("frame-batch",std::format("batch={} epoch={} revision={} source={} a={} b={} count={} realSlot={} realFence={} genSlot={} genFence={}",out.batch.batchId,epoch_,out.batch.identity.settingsRevision,out.batch.identity.sourceFrameId,out.batch.a100ns,out.batch.b100ns,out.batch.count,parity,out.videoFenceValue,out.genSlot,out.genFenceValue));
    return true;
}

ID3D12Fence* EnhanceGraph::presentationReadyFenceObject(unsigned slot,bool generated)const
{
    if(generated&&slot<kGeneratedPoolSlots){if(auto lease=generatedLeases_[slot].lock())return lease->readyFenceObject.Get();}
    return context_.fence();
}

bool EnhanceGraph::resolveFrame(FrameOutputs& out,uint32_t index)
{
    if(index>=out.batch.count)return false;
    auto& frame=out.batch.frames[index];
    if(!frame.lease||!frame.lease->ready())return false;
    if(frame.kind!=FrameKind::Generated||frame.validity!=GenerationValidity::Pending)return true;
    // Readback heaps may stay mapped; the 4-byte status is read directly.
    auto& mapped=fgDisableMapped_[frame.lease->slot];
    if(!mapped){D3D12_RANGE range{0,4};void* data=nullptr;const HRESULT hr=fgDisableReadback_[frame.lease->slot]->Map(0,&range,&data);
        if(FAILED(hr)){frame.validity=GenerationValidity::Failed;veyra::log::error("fg-status",std::format("Map hr=0x{:X}",unsigned(hr)));return true;}mapped=static_cast<const volatile uint8_t*>(data);}
    const bool disabled=*mapped!=0;
    const bool rejected=disabled||out.contentDuplicate;
    frame.validity=rejected?GenerationValidity::Disabled:GenerationValidity::Valid;
    if(rejected)++metrics_.fgDisabledFrames;else ++metrics_.fgGeneratedFrames;
    if(veyra::log::verboseFrameLogs())veyra::log::info("fg-status",std::format("batch={} frame={} epoch={} revision={} subframe={} fence={} disable={} valid={}",out.batch.batchId,out.realFrameIndex,out.batch.identity.epoch,out.batch.identity.settingsRevision,frame.subframe,frame.lease->readyFence,disabled,!rejected));
    return true;
}

bool EnhanceGraph::resolveGeneration(FrameOutputs& out)
{
    bool complete=true,anyValid=false;
    for(uint32_t i=0;i<out.batch.count;++i){
        if(!resolveFrame(out,i))complete=false;
        const auto& frame=out.batch.frames[i];
        anyValid|=frame.kind==FrameKind::Generated&&frame.validity==GenerationValidity::Valid;
    }
    // Warmup evaluates may have no exposed generated leases, but their status
    // and resources must finish before full-batch completion is reported.
    if(!out.gpuComplete())complete=false;
    if(complete)out.hasGenerated=anyValid;
    return complete;
}

bool EnhanceGraph::applySettings(const engine::EnhancementSettings& s){
    if(s.nrTemporal!=desc_.nrTemporal)return false;
    nrTemporal_.reset(); // settings/protection changes must not revive old corrections
    // A non-NVIDIA adapter keeps the user's requested NR setting in the UI
    // but has this NGX-only stage explicitly disabled in the graph. Do not
    // reject unrelated live settings in that degraded, playable state.
    if(s.nr&&desc_.enableNr&&!nrHandle_)return false;
    if(s.opticalFlowBackend!=desc_.opticalFlowBackend||s.amdFlowHalfResolution!=desc_.amdFlowHalfResolution)return false;
    // DLSS allocates multiplier-specific feature/output resources.
    // XeSS has a fixed 2X proxy swapchain contract. Settings callers must
    // rebuild instead of accepting a change that cannot take effect in place.
    if(s.nrRuntime!=desc_.nrRuntime||std::max(2u,s.multiplier)!=desc_.fgMultiplier||s.frameGenerationBackend!=desc_.frameGenerationBackend||s.videoSrQuality!=desc_.videoSrQuality||!s.validate().empty()||(s.multiplier>1&&!engine::presentSinkFrameGeneration(s.frameGenerationBackend)&&(!fgCapsAvailable_||s.multiplier-1>uint32_t(fgMultiFrameMax_))))return false;
    // The colour master switch changes the graph shape (tables + shader branch)
    // and must rebuild; every other colour field is a live uniform update.
    if(s.color.enabled!=desc_.color.enabled)return false;
    if(s.videoHdr.enabled!=desc_.videoHdr.enabled)return false;
    desc_.videoHdr=s.videoHdr;
    // Switching the referenced .cube re-stages a descriptor, which needs the
    // queue drained: treat it as a rebuild (parameters stay live).
    if(s.color.lutNameString()!=desc_.color.lutNameString())return false;
    if(!(s.color==desc_.color)){desc_.color=s.color;refreshColorTables();}
    desc_.contentRate=s.content;desc_.model=s.model;desc_.residual=s.residual;desc_.protection=s.protection;desc_.settingsRevision=s.revision;
    desc_.fgMultiplier=std::max(2u,s.multiplier);desc_.enableNvofStandalone=s.nr&&!desc_.stillImage;nvofStandalone_=desc_.enableNvofStandalone;
    setNrEnabled(s.nr);setFgEnabled(s.multiplier>1&&!engine::presentSinkFrameGeneration(s.frameGenerationBackend));
    veyra::log::info("settings",std::format("requested revision={} intensity={} tone={} structure={} skin={} style={} autoMask={} UI={} residual={}/{}/{}/{}/{} multiplier={}",s.revision,s.model.intensity,s.model.tone,s.model.structure,s.model.skin,s.model.style,s.model.autoMask,s.model.uiCorrection,s.residual.total,s.residual.darken,s.residual.brighten,s.residual.color,s.residual.luminance,s.multiplier));
    return true;
}
void EnhanceGraph::setNrEnabled(bool on)
{
    nrEnabled_ = on && nrHandle_ != nullptr;
    // Refresh the section-5 blit SRV exactly as the probe did at runtime
    // (direct write into the visible heap - proven safe for this refresh).
    makeSrv(context_.device(),
        nrEnabled_&&!desc_.nrBeforeSr ? residualRgba_.Get() : workRgba_.Get(),
        DXGI_FORMAT_R16G16B16A16_FLOAT, cpuHandleOf(blitPass_, 2));
}

ID3D12Fence* EnhanceGraph::contextFence()const{return context_.fence();}
uint32_t EnhanceGraph::actualFlowPerf()const{return nvof_?nvof_->caps().actualPerfLevel:0;}
bool EnhanceGraph::fgCreated() const
{
    return fgBackend_ && fgBackend_->created();
}

uint64_t EnhanceGraph::lastNvofSignal() const
{
    return (nvof_ && nvof_->initialized()) ? (nvof_->nextOutValue() - 1) : 0;
}

bool EnhanceGraph::nvofSessionInitialized() const
{
    return nvof_ && nvof_->initialized();
}

uint64_t EnhanceGraph::nvofNextOutValue() const
{
    return (nvof_ && nvof_->initialized()) ? nvof_->nextOutValue() : 0;
}

ID3D12Resource* EnhanceGraph::videoFrameResource(uint32_t slot) const
{
    return slot < 2 ? videoFrame_[slot].Get() : nullptr;
}

ID3D12Resource* EnhanceGraph::generatedFrameResource(uint32_t slot) const
{
    return slot < kGeneratedPoolSlots ? genFrame_[slot].Get() : nullptr;
}

// ---------------------------------------------------------------------------
// shutdown: s10 ownership-order teardown of everything the graph owns.
// The NVOF out-fence drain must ALREADY have happened (caller), while the
// ring and fences were alive.
// ---------------------------------------------------------------------------
void EnhanceGraph::shutdown()
{
    if (!initialized_ && !nrAdapter_ && !nvof_ && !srcRgba_) return;
    for(unsigned i=0;i<2;++i)if(presentationFences_[i]){
        const HRESULT hr=context_.directQueue()->Wait(presentationFences_[i].Get(),presentationValues_[i]);
        if(FAILED(hr))veyra::log::error("graph",std::format("presentation teardown wait hr=0x{:X}",unsigned(hr)));
    }
    (void)ring_.drainQueue();(void)ring_.discardRecording();
    presentationFences_={};presentationValues_={};
    for(auto& input:hardwareInputFrames_)input.reset();
    Status st = Status::Ok;
    if (nv12Ctx_ != nullptr) { sws_freeContext(nv12Ctx_); nv12Ctx_ = nullptr; }

    if (nrHandle_ != nullptr) {
        uint64_t rr = 0; uint32_t rs = 0;
        (void)nrAdapter_->snippetReleaseFeature(nrHandle_, rr, rs);
        nrHandle_ = nullptr;
    }
    gpuDis_.reset();
    amdOf_.reset();
    for(auto& motion:presentMotion_)motion.Reset();
    if(fsrSrBackend_){fsrSrBackend_->release();fsrSrBackend_.reset();}
    fsrSrDepth_.Reset();
    upFsrSrDepth_.Reset();
    if (fgBackend_) fgBackend_->release();
    if(videoSrBackend_){videoSrBackend_->release();videoSrBackend_.reset();}
    videoHdrBackend_.reset();videoHdrInput_.Reset();videoHdrOutput_.Reset();
    if (srBackend_) srBackend_->release();

    // NVOF teardown in THREE phases (ownership rule proven by t10-L0-r2):
    // a. unregisterAll() while the textures are still alive;
    // b. release the four registered textures (DLL still loaded);
    // c. shutdown() = nvOFDestroy + FreeLibrary.
    if (nvof_ && nvof_->initialized()) {
        veyra::Status us = veyra::Status::Ok;
        if (!nvof_->unregisterAll(us)) {
            veyra::log::error("graph", std::format("nvof unregisterAll failed status={}", static_cast<int>(us)));
        }
    }
    nvofCostTex_.Reset();
    nvofRawTex_.Reset();
    nvofInB_.Reset();
    nvofInA_.Reset();
    if (nvof_) nvof_->shutdown();

    if (nvofOutEvent_ != nullptr) { CloseHandle(nvofOutEvent_); nvofOutEvent_ = nullptr; }
    if (coreHost_ && coreHost_->initialized() && ngxParams_ != nullptr) {
        coreHost_->destroyParameters(ngxParams_);
        ngxParams_ = nullptr;
    }
    if (nrAdapter_) {
        nrAdapter_->restoreCallerCompatibility();
        nrAdapter_->unload();
    }
    if (coreHost_) coreHost_->shutdown();
    // Restore the DLSS-G runtime image only after the NGX core released the
    // feature (the unlock is process memory only; the file on disk is untouched).
    fgCompatibility_.reset();
    ampereSpoofed_=false;

    // Staged explicit release (scope-end destructors then have nothing left).
    decPass_ = ComputePass{};
    rgbPass_={};rgbTex_.Reset();
    for(unsigned i=0;i<2;++i){if(upRgb_[i]&&mappedRgb_[i])upRgb_[i]->Unmap(0,nullptr);mappedRgb_[i]=nullptr;upRgb_[i].Reset();}
    downsamplePass_={};residualPass_={};flowAdaptPass_={};
    nrTemporal_.close();nrInput_.Reset();residualRgba_.Reset();nrFlow_.Reset();baseFlow_.Reset();
    for(unsigned i=0;i<kGeneratedPoolSlots;++i){if(fgDisableMapped_[i]&&fgDisableReadback_[i]){D3D12_RANGE written{0,0};fgDisableReadback_[i]->Unmap(0,&written);}fgDisableMapped_[i]=nullptr;fgDisable_[i].Reset();fgDisableReadback_[i].Reset();generatedLeases_[i].reset();genFrame_[i].Reset();}for(auto& lease:realLeases_)lease.reset();fgDisableInit_.Reset();
    encPass_ = ComputePass{};
    blitPass_ = ComputePass{};hdrVideoSrPass_={};
    yuvPass_ = ComputePass{};
    toneMapPeakNits_=0;
    densifyPass_ = ComputePass{};
    confTex_.Reset();
    flowTex_.Reset();
    depthTex_.Reset();
    nrZeroMotion_.Reset();
    nrZeroDepth_.Reset();
    genFrame_[0].Reset();
    genFrame_[1].Reset();
    videoFrame_[0].Reset();
    videoFrame_[1].Reset();
    finalRgba_.Reset();
    neuralTex_.Reset();
    proxyTex_.Reset();
    for(auto& r:sourceReferences_)r.Reset();for(auto& r:baseReferences_)r.Reset();workRgba_.Reset();
    srcRgba_.Reset();videoSrInput_.Reset();videoSrOutput_.Reset();
    chromaTex_.Reset();
    lumaTex_.Reset();
    upZeroMotion_.Reset();
    upZeroDepth_.Reset();
    upDepth_.Reset();
    upChroma_[0].Reset();
    upChroma_[1].Reset();
    upLuma_[0].Reset();
    upLuma_[1].Reset();
    nvofOutFence_.Reset();
    nvof_.reset();
    fgBackend_.reset();
    srBackend_.reset();
    nrAdapter_.reset();
    coreHost_.reset();
    gpuTimer_.close();initialized_ = false;
    veyra::log::info("graph", "shutdown complete (ordered)");
    (void)st;
}

} // namespace veyra::pipeline
