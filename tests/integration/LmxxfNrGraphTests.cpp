// SPDX-License-Identifier: GPL-3.0-only
// Actual shared graph and shaders with a GPU identity provider. This checks
// composition, chaining and reset, and does NOT execute HIP/AMD neural inference.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/GraphDescription.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
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
    av_frame_free(&frame);CoUninitialize();
    std::cout<<"Shared AMD composition GPU test: "<<checks<<" cases, "<<failures<<" failures. Not HIP inference.\n";
    return failures?1:0;
}
