#pragma once
// Test-only Feature-18 fixture. Uses product adapter/parameter contracts.
// All readbacks and waits belong to these experiments, never the player.
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/DlssNrParameters.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace nrperf {
using namespace veyra;
namespace p = ngx::dlssnr;
using pipeline::ComPtr;
inline double nowMs() { return std::chrono::duration<double,std::milli>(
    std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline bool hrOK(HRESULT hr,const char* where) {
    if(SUCCEEDED(hr))return true;
    std::printf("HRESULT where=%s value=0x%08X\n",where,unsigned(hr));return false;
}
inline bool ngxOK(bool safe,uint64_t result,uint32_t seh,const char* where) {
    std::printf("NGX where=%s result=0x%llX seh=0x%X safe=%d\n",where,
        static_cast<unsigned long long>(result),seh,safe);
    return safe&&result==uint64_t(NVSDK_NGX_Result_Success)&&seh==0;
}
struct Session {
    gfx::D3D12DeviceContext device;
    gfx::CommandSlotRing ring;
    ngx::NgxCoreHost core;
    ngx::DlssNrRuntimeAdapter adapter;
    std::wstring root;
    bool init(const std::wstring& directory) {
        root=directory;Status s=Status::Ok;gfx::DeviceContextDesc d;d.enableDebugLayer=true;
        if(!device.initialize(d,s)||!ring.initialize(device.device(),device.directQueue(),
            device.fence(),device.fenceEvent(),4,s))return false;
        auto coreDirectory=std::filesystem::path(root);
        for(unsigned n=0;n<3&&!std::filesystem::exists(coreDirectory/L"../config/ngx-local.json");++n)
            coreDirectory=coreDirectory.parent_path();
        std::ifstream input(coreDirectory/L"../config/ngx-local.json");
        const std::string text((std::istreambuf_iterator<char>(input)),{});
        auto field=[&](const char* name){const auto a=text.find(std::string("\"")+name+'"');
            if(a==std::string::npos)return std::string{};const auto b=text.find('"',a+std::strlen(name)+2);
            const auto c=text.find('"',b+1);return b==std::string::npos||c==std::string::npos?std::string{}:text.substr(b+1,c-b-1);};
        const auto id=field("ngxProjectId"),ver=field("engineVersion");
        if(id.empty()||ver.empty()||!core.initialize(device.device(),coreDirectory.wstring(),id.c_str(),ver.c_str(),s)||
            !adapter.load(root,s)||!adapter.installCallerCompatibility(s))return false;
        uint64_t r=0;uint32_t e=0;const bool safe=adapter.snippetInitExt(device.device(),root,r,e);
        return ngxOK(safe,r,e,"InitExt");
    }
    ~Session(){if(ring.initialized())ring.drainQueue();adapter.unload();core.shutdown();ring.shutdown();device.shutdown();}
};

struct Feature {
    Session& session;
    NVSDK_NGX_Parameter* params=nullptr;
    NVSDK_NGX_Handle* handle=nullptr;
    ComPtr<ID3D12Resource> color,output,motion,depth;
    pipeline::StateTracker states;
    uint32_t textureWidth=0,textureHeight=0;
    explicit Feature(Session& s):session(s) {Status status;params=s.core.allocateParameters(status);}
    ~Feature(){release();if(params)session.core.destroyParameters(params);}
    bool release() {if(!handle)return true;uint64_t r=0;uint32_t e=0;
        const bool safe=session.adapter.snippetReleaseFeature(handle,r,e);handle=nullptr;
        return ngxOK(safe,r,e,"Release");}
    void dimensions(uint32_t w,uint32_t h) {
        ngx::ParameterBlock b(params);
        for(auto key:{p::kWidth,p::kInputWidth,p::kOutputWidth,p::kOutputDotWidth,p::kStdWidth})b.setU32(key,w);
        for(auto key:{p::kHeight,p::kInputHeight,p::kOutputHeight,p::kOutputDotHeight,p::kStdHeight})b.setU32(key,h);
    }
    bool create(gfx::CommandSlotRing& ring,uint32_t w,uint32_t h) {
        if(!params)return false;dimensions(w,h);ngx::ParameterBlock b(params);
        b.setU32(p::kUpscaling,0);b.setF32(p::kScale,1);b.setF32(p::kScalingRatio,1);
        b.setVoid(p::kComputeScalingRatioCallback,reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
        b.setI32(p::kHintRenderPreset,0);b.setI32(p::kPerfQualityValue,1);
        b.setU32(p::kCreationNodeMask,1);b.setU32(p::kVisibilityNodeMask,1);
        Status s;uint32_t slot;auto* list=ring.acquireNext(slot,s);if(!list)return false;
        uint64_t r=0;uint32_t e=0;const auto start=nowMs();
        const bool safe=session.adapter.snippetCreateFeature(list,params,&handle,r,e);
        const bool ok=ngxOK(safe,r,e,"Create")&&handle;
        const bool submitted=ok?(ring.submitAndSignal(slot)&&ring.waitIdle()):ring.discardRecording();
        std::printf("CREATE width=%u height=%u cpuWithWaitMs=%.6f pass=%d\n",w,h,nowMs()-start,ok&&submitted);
        return ok&&submitted;
    }
    bool upload(gfx::CommandSlotRing& ring,ID3D12Resource* target,const std::vector<uint8_t>& pixels) {
        const auto desc=target->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows=0;UINT64 rowBytes=0,total=0;
        session.device.device()->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowBytes,&total);
        if(pixels.size()!=rowBytes*rows)return false;
        auto buffer=pipeline::makeUploadBuffer(session.device.device(),total);if(!buffer)return false;
        void* data=nullptr;D3D12_RANGE empty{};if(!hrOK(buffer->Map(0,&empty,&data),"Upload.Map"))return false;
        for(UINT y=0;y<rows;++y)std::memcpy(static_cast<uint8_t*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,
            pixels.data()+size_t(y)*rowBytes,size_t(rowBytes));buffer->Unmap(0,nullptr);
        Status s;uint32_t slot;auto* list=ring.acquireNext(slot,s);if(!list)return false;
        states.transition(list,target,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=target;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource=buffer.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;
        list->CopyTextureRegion(&a,0,0,0,&b,nullptr);
        states.transition(list,target,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return ring.submitAndSignal(slot)&&ring.waitIdle();
    }
    bool textures(gfx::CommandSlotRing& ring,uint32_t tw,uint32_t th,uint32_t activeW,uint32_t activeH) {
        color=pipeline::makeTexture(session.device.device(),tw,th,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        output=pipeline::makeTexture(session.device.device(),tw,th,DXGI_FORMAT_R8G8B8A8_UNORM,true);
        motion=pipeline::makeTexture(session.device.device(),tw,th,DXGI_FORMAT_R16G16_FLOAT,true);
        depth=pipeline::makeTexture(session.device.device(),tw,th,DXGI_FORMAT_R32_FLOAT,true);
        if(!color||!output||!motion||!depth)return false;
        textureWidth=tw;textureHeight=th;states={};std::vector<uint8_t> image(size_t(tw)*th*4,0),sentinel(image.size());
        for(uint32_t y=0;y<th;++y)for(uint32_t x=0;x<tw;++x){const auto i=(size_t(y)*tw+x)*4;
            image[i]=uint8_t(40+(x%activeW)*150/activeW);image[i+1]=uint8_t(50+(y%activeH)*140/activeH);
            image[i+2]=uint8_t((((x/31)^(y/23))&1)?190:60);image[i+3]=255;
            sentinel[i]=17;sentinel[i+1]=23;sentinel[i+2]=29;sentinel[i+3]=255;}
        const std::vector<uint8_t> zero(image.size(),0);
        return upload(ring,color.Get(),image)&&upload(ring,output.Get(),sentinel)&&
            upload(ring,motion.Get(),zero)&&upload(ring,depth.Get(),zero);
    }
    bool evaluate(gfx::CommandSlotRing& ring,uint32_t w,uint32_t h,bool reset,bool changeDimensions=false) {
        if(changeDimensions)dimensions(w,h);ngx::ParameterBlock b(params);
        b.setD3D12Resource(p::kColor,color.Get());b.setD3D12Resource(p::kOutput,output.Get());
        b.setD3D12Resource(p::kMVec,motion.Get());b.setD3D12Resource(p::kDepth,depth.Get());
        for(auto key:{p::kColorSubrectWidth,p::kOutputSubrectWidth,p::kMVecSubrectWidth,p::kDepthSubrectWidth})b.setU32(key,w);
        for(auto key:{p::kColorSubrectHeight,p::kOutputSubrectHeight,p::kMVecSubrectHeight,p::kDepthSubrectHeight})b.setU32(key,h);
        for(auto key:{p::kColorSubrectBaseX,p::kOutputSubrectBaseX,p::kMVecSubrectBaseX,p::kDepthSubrectBaseX,
            p::kColorSubrectBaseY,p::kOutputSubrectBaseY,p::kMVecSubrectBaseY,p::kDepthSubrectBaseY})b.setU32(key,0);
        b.setF32(p::kMVecScaleX,1);b.setF32(p::kMVecScaleY,1);b.setI32(p::kDepthInverted,1);
        b.setI32(p::kIndicatorInvertX,0);b.setI32(p::kIndicatorInvertY,0);b.setI32(p::kEnabled,1);
        b.setI32(p::kReset,reset?1:0);b.setI32(p::kStyle,0);b.setF32(p::kIntensity,1);
        b.setF32(p::kLocalToneStrength,1);b.setF32(p::kLocalStructureStrength,1);
        b.setF32(p::kSkinStructureStrength,-1);b.setI32(p::kUseAutoMask,0);b.setI32(p::kUICorrection,0);
        Status s;uint32_t slot;auto* list=ring.acquireNext(slot,s);if(!list)return false;
        states.transition(list,color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list,output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uint64_t r=0;uint32_t e=0;const bool safe=session.adapter.snippetEvaluateFeature(list,handle,params,r,e);
        const bool ok=ngxOK(safe,r,e,"Evaluate");
        if(!ok){ring.discardRecording();states.set(output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);return false;}
        states.uavBarrier(list,output.Get());states.transition(list,output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return ring.submitAndSignal(slot)&&ring.waitIdle()&&ok;
    }
    std::vector<uint8_t> read(gfx::CommandSlotRing& ring) {
        const auto desc=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows=0;UINT64 rowBytes=0,total=0;session.device.device()->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowBytes,&total);
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};
        bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;
        bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> buffer;if(!hrOK(session.device.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)),"Readback.Create"))return {};
        Status s;uint32_t slot;auto* list=ring.acquireNext(slot,s);if(!list)return {};
        states.transition(list,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=buffer.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp;
        b.pResource=output.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&a,0,0,0,&b,nullptr);
        states.transition(list,output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!ring.submitAndSignal(slot)||!ring.waitIdle())return {};
        void* data=nullptr;D3D12_RANGE range{0,SIZE_T(total)};if(!hrOK(buffer->Map(0,&range,&data),"Readback.Map"))return {};
        std::vector<uint8_t> result(size_t(rowBytes)*rows);
        for(UINT y=0;y<rows;++y)std::memcpy(result.data()+size_t(y)*rowBytes,
            static_cast<uint8_t*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(rowBytes));
        D3D12_RANGE empty{};buffer->Unmap(0,&empty);return result;
    }
};
}
