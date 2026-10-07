// SPDX-License-Identifier: GPL-3.0-only
// Actual shared graph and shaders with GPU identity and marker providers. This checks
// composition, chaining and reset, and does NOT execute HIP/AMD neural inference.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/GraphDescription.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <d3d12sdklayers.h>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
namespace {
void checkSrHandoff(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,unsigned& checks,unsigned& failures){
    struct Case {const char* name;unsigned layers;bool temporal,flow,beforeSr,fsr;uint32_t width,height;double fps;float strength=1;};
    const Case cases[]={
        {"off-fsr-zero",0,false,false,false,true,128,72,23.976},
        {"pre-fsr-one",1,false,false,true,true,128,72,23.976},
        {"pre-fsr-two",2,false,false,true,true,128,72,23.976},
        {"pre-fsr-temporal",1,true,false,true,true,128,72,23.976},
        {"pre-fsr-flow-file",1,false,true,true,true,128,72,23.976},
        {"pre-fsr-flow-xsx-1080",1,false,true,true,true,1920,1080,60},
        {"post-fsr-one",1,false,false,false,true,128,72,23.976},
        {"pre-blit-one",1,false,false,true,false,128,72,60},
        {"pre-fsr-zero-strength",1,false,false,true,true,128,72,60,0},
        {"off-fsr-flow",0,false,true,false,true,128,72,60}
    };
    SetEnvironmentVariableA("VEYRA_TEST_LMXXF_MODE","marker");
    for(const auto& c:cases){
        AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_RGBA;frame->width=int(c.width);frame->height=int(c.height);
        if(av_frame_get_buffer(frame,32)<0){av_frame_free(&frame);++failures;continue;}
        for(int y=0;y<frame->height;++y)for(int x=0;x<frame->width;++x){auto* p=frame->data[0]+y*frame->linesize[0]+x*4;p[0]=32;p[1]=96;p[2]=160;p[3]=255;}
        pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=c.width;gd.sourceHeight=c.height;gd.workWidth=c.width*2;gd.workHeight=c.height*2;
        gd.rgbInput=true;gd.enableNr=c.layers>0;gd.enableSr=c.fsr;gd.enableFg=false;gd.nrBeforeSr=c.beforeSr;
        gd.videoSrQuality=c.fsr?engine::kVideoSrFsr:0;
        gd.nrRuntime=engine::NrRuntime::LmxxfAmd;gd.nrMotion=gd.fgMotion=engine::MotionSource::Zero;
        gd.srMotion=c.flow?engine::MotionSource::OpticalFlow:engine::MotionSource::Zero;
        gd.opticalFlowBackend=engine::OpticalFlowBackend::AmdFidelityFx;
        gd.runtimeAbsPath=std::filesystem::absolute("runtime/nvidia").wstring();
        gd.nrWidth=c.width;gd.nrHeight=c.height;
        gd.nrLayersModel.resize(c.layers);gd.nrLayersResidual.resize(c.layers);
        for(auto& r:gd.nrLayersResidual)r.total=c.strength;
        gd.nrLayersTemporal.assign(c.layers,c.temporal);gd.nrLayersSizePolicy.assign(c.layers,pipeline::NrSizePolicy::Native);
        gd.nrLayersExtent.assign(c.layers,{c.width,c.height});
        pipeline::EnhanceGraph graph(ctx,ring);
        bool executed=graph.initialize(gd)&&graph.createViews();
        executed=executed&&(!c.fsr||graph.fsrSrEnabled());
        const uint8_t marker[3]={188,99,71},original[3]={32,96,160};
        const auto* expected=c.layers&&c.strength>0?marker:original;
        unsigned worst=0,first=0,last=0;
        for(unsigned f=0;executed&&f<6;++f){
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;
            executed=graph.process(frame,double(f)*1000/c.fps,f==0||f==3,out,f+1)&&
                sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
            executed=executed&&image.width==gd.workWidth&&image.height==gd.workHeight;
            unsigned error=0;
            if(executed)for(unsigned y=1;y<=3;++y)for(unsigned x=1;x<=3;++x)for(unsigned ch=0;ch<3;++ch){
                const auto offset=(size_t(image.height*y/4)*image.width+image.width*x/4)*4+ch;
                error=std::max(error,unsigned(std::abs(int(image.pixels[offset])-int(expected[ch]))));
            }
            worst=std::max(worst,error);if(f==0)first=error;last=error;
        }
        const auto nr=graph.metrics().nrEvaluateCount,sr=graph.metrics().srEvaluateCount;
        const uint64_t expectedSr=c.fsr?(c.flow?4:6):0;
        const bool pass=executed&&worst<=3&&nr==uint64_t(c.layers)*6&&sr==expectedSr;
        ring.drainQueue();graph.shutdown();av_frame_free(&frame);++checks;failures+=!pass;
        std::cout<<"AMD_NR_SR_MARKER case="<<c.name<<" output="<<gd.workWidth<<'x'<<gd.workHeight
            <<" fps="<<c.fps<<" nr="<<nr<<" sr="<<sr<<" firstError="<<first<<" lastError="<<last
            <<" worstCodeDifference="<<worst<<" executed="<<executed<<" pass="<<pass<<'\n';
    }
    SetEnvironmentVariableA("VEYRA_TEST_LMXXF_MODE",nullptr);
}
}
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st=Status::Ok;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))return 2;
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_RGBA;frame->width=128;frame->height=72;
    if(av_frame_get_buffer(frame,32)<0)return 2;
    for(int y=0;y<frame->height;++y)for(int x=0;x<frame->width;++x){
        auto* p=frame->data[0]+y*frame->linesize[0]+x*4;
        p[0]=uint8_t(30+x);p[1]=uint8_t(40+y*2);p[2]=uint8_t(210-x);p[3]=255;
    }
    unsigned failures=0,checks=0;
    checkSrHandoff(ctx,ring,checks,failures);
    sink::RgbaImage reference;
    for(unsigned layers:{0u,1u,2u,4u})for(bool temporal:{false,true}){
        if(!layers&&temporal)continue;
        pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;
        gd.sourceWidth=gd.workWidth=128;gd.sourceHeight=gd.workHeight=72;
        gd.rgbInput=true;gd.enableNr=layers>0;gd.enableSr=gd.enableFg=false;
        gd.nrRuntime=engine::NrRuntime::LmxxfAmd;gd.nrMotion=engine::MotionSource::Zero;
        gd.srMotion=gd.fgMotion=engine::MotionSource::Zero;
        gd.runtimeAbsPath=std::filesystem::absolute("runtime/nvidia").wstring();
        gd.nrLayersModel.resize(layers);gd.nrLayersResidual.resize(layers);
        gd.nrLayersTemporal.assign(layers,temporal);
        gd.nrLayersSizePolicy.assign(layers,pipeline::NrSizePolicy::P480);
        gd.nrLayersExtent.assign(layers,{64,36});
        bool ok=graph.initialize(gd)&&graph.createViews();
        unsigned maximum=0;uint64_t rgbSum=0;
        for(unsigned f=0;ok&&f<8;++f){
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;
            ok=graph.process(frame,f*33.333,f==0||f==4,out,f+1)&&
                sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
            if(ok&&!layers&&f==0)reference=image;
            ok=ok&&image.width==reference.width&&image.height==reference.height&&image.pixels.size()==reference.pixels.size();
            if(ok)for(size_t i=0;i<image.pixels.size();++i)if(i%4!=3){
                rgbSum+=image.pixels[i];maximum=std::max(maximum,unsigned(std::abs(int(image.pixels[i])-int(reference.pixels[i]))));
            }
            ok=ok&&maximum<=1&&rgbSum>0;
        }
        ring.drainQueue();graph.shutdown();++checks;failures+=!ok;
        std::cout<<"AMD_GRAPH_IDENTITY layers="<<layers<<" temporal="<<temporal
            <<" maxCodeDifference="<<maximum<<" rgbSum="<<rgbSum<<" pass="<<ok<<'\n';
    }
    // Real export planner + full-resolution residual compositor. A small
    // identity fixture alone cannot detect a 4K output being truncated to the
    // neural model's internal 1080p budget.
    av_frame_free(&frame);frame=av_frame_alloc();
    frame->format=AV_PIX_FMT_RGBA;frame->width=3840;frame->height=2160;
    if(av_frame_get_buffer(frame,32)<0)return 2;
    for(int y=0;y<frame->height;++y)for(int x=0;x<frame->width;++x){
        auto* p=frame->data[0]+y*frame->linesize[0]+x*4;
        p[0]=uint8_t(24+(x/24)%192);p[1]=uint8_t(32+(y/16)%176);p[2]=uint8_t(220-(x/32+y/24)%192);p[3]=255;
    }
    reference={};
    for(unsigned layers:{0u,1u,2u}){
        engine::EnhancementSettings settings;settings.nr=layers>0;settings.sr=false;settings.multiplier=1;
        settings.nrRuntime=engine::NrRuntime::LmxxfAmd;settings.nrMotion=engine::MotionSource::Zero;
        settings.nrLayerCount=layers;
        for(unsigned n=0;n<layers;++n){settings.nrLayers[n].enabled=true;settings.nrLayers[n].runtime=engine::NrRuntime::LmxxfAmd;settings.nrLayers[n].sizePolicy=pipeline::NrSizePolicy::Native;}
        engine::StageRequest request;request.nr=settings.nr;request.width=3840;request.height=2160;
        request.exportJob=true;request.nvidiaAdapter=false;request.amdNr=true;
        pipeline::EnhanceGraphDesc gd;const auto plan=engine::describeStages(request,settings,gd);
        gd.rgbInput=true;gd.runtimeAbsPath=std::filesystem::absolute("runtime/nvidia").wstring();
        bool ok=plan.base==pipeline::Extent{3840,2160}&&gd.nrWidth<=1920&&gd.nrHeight<=1080;
        for(const auto extent:gd.nrLayersExtent)ok=ok&&extent.width<=1920&&extent.height<=1080;
        pipeline::EnhanceGraph graph(ctx,ring);ok=ok&&graph.initialize(gd)&&graph.createViews();
        unsigned maximum=0;uint64_t evaluated=0;
        for(unsigned f=0;ok&&f<3;++f){
            pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;
            ok=graph.process(frame,f*33.333,f==0||f==2,out,f+1)&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
            if(ok&&!layers&&f==0)reference=image;
            ok=ok&&image.width==3840&&image.height==2160&&image.pixels.size()==reference.pixels.size();
            if(ok)for(size_t p=0;p<image.pixels.size();++p)if(p%4!=3)maximum=std::max(maximum,unsigned(std::abs(int(image.pixels[p])-int(reference.pixels[p]))));
            ok=ok&&maximum<=1;
        }
        evaluated=graph.metrics().nrEvaluateCount;ok=ok&&evaluated==uint64_t(layers)*3;
        ring.drainQueue();graph.shutdown();++checks;failures+=!ok;
        std::cout<<"AMD_EXPORT_4K_IDENTITY layers="<<layers<<" output=3840x2160 internal="<<gd.nrWidth<<'x'<<gd.nrHeight
            <<" evaluated="<<evaluated<<" maxCodeDifference="<<maximum<<" pass="<<ok<<'\n';
    }
    uint32_t removed=0;if(!ctx.checkDeviceAlive(removed))++failures;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    if(SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)))){
        unsigned errors=0;
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){
            SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(SUCCEEDED(info->GetMessage(i,message,&bytes))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                if(errors<12)std::cout<<"D3D12_ERROR id="<<message->ID<<" "<<message->pDescription<<'\n';++errors;
            }
        }
        failures+=errors>0;std::cout<<"D3D12_DEBUG errors="<<errors<<'\n';
    }else{++failures;std::cout<<"D3D12_DEBUG unavailable\n";}
    av_frame_free(&frame);CoUninitialize();
    std::cout<<"Shared AMD composition GPU test: "<<checks<<" cases, "<<failures<<" failures. Not HIP inference.\n";
    return failures?1:0;
}
