#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/ngx/FgCompatibilitySession.h"
#include <filesystem>
#include <iostream>
#include <string_view>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

int wmain(int argc,wchar_t** argv) {
    using namespace veyra;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;pipeline::EnhanceGraph graph(ctx,ring);
    Status status=Status::Ok;gfx::DeviceContextDesc device;
    bool ok=ctx.initialize(device,status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status);
    pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;
    desc.enableNr=desc.enableSr=false;desc.enableFg=true;desc.rgbInput=true;
    desc.runtimeAbsPath=(std::filesystem::path(VEYRA_PROJECT_ROOT)/"runtime_local/nvidia").wstring();
    const bool anr=argc>1&&std::wstring_view(argv[1])==L"anr";
    if(anr)desc.frameGenerationBackend=engine::FrameGenerationBackend::Anr;
    if(argc>2)desc.runtimeAbsPath=argv[2];
    const bool initFailure=argc>1&&std::wstring_view(argv[1])==L"init-failure";
    if(ok&&initFailure){
        desc.sourceHeight=desc.workHeight=360;
        SetEnvironmentVariableW(L"VEYRA_TEST_NGX_INIT_FAILURE",L"1");
        const bool rejected=!graph.initialize(desc);
        SetEnvironmentVariableW(L"VEYRA_TEST_NGX_INIT_FAILURE",nullptr);
        graph.shutdown();
        ok=rejected&&ngx::FgCompatibilitySession::processHealthy();
        std::cout<<"INIT failure-restores-session="<<ok<<'\n';
    }
    unsigned iteration=0;
    for(unsigned multiplier:{6u,2u,5u,3u,4u,6u}) {
        if(!ok)break;
        desc.fgMultiplier=multiplier;desc.sourceHeight=desc.workHeight=360+8*(iteration++%2);
        ok=graph.initialize(desc)&&graph.createViews();
        AVFrame* frame=av_frame_alloc();if(!frame){ok=false;break;}
        frame->format=AV_PIX_FMT_RGBA;frame->width=int(desc.workWidth);frame->height=int(desc.workHeight);frame->color_range=AVCOL_RANGE_JPEG;
        ok=ok&&av_frame_get_buffer(frame,32)>=0;
        unsigned generated=0;
        for(unsigned i=0;ok&&i<4;++i){
            for(unsigned y=0;y<desc.workHeight;++y)for(unsigned x=0;x<desc.workWidth;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                p[0]=p[1]=p[2]=uint8_t(24+(((x+i*8)/32+y/32)%2)*180);p[3]=255;
            }
            pipeline::EnhanceGraph::FrameOutputs output;
            const bool reset=i==0||i==2;
            ok=graph.process(frame,i*20.0,reset,output)&&ring.waitIdle()&&graph.resolveGeneration(output);
            unsigned count=0;
            for(unsigned j=0;ok&&j<output.batch.count;++j){
                const auto& f=output.batch.frames[j];
                if(f.kind==pipeline::FrameKind::Generated){++count;ok=f.validity==pipeline::GenerationValidity::Valid&&f.lease&&f.lease->slot<pipeline::kGeneratedPoolSlots;}
            }
            ok=ok&&count==(reset?0:multiplier-1);generated+=count;
        }
        av_frame_free(&frame);ok=ring.drainQueue()&&ok;
        if(argc>1&&!initFailure&&!anr)SetEnvironmentVariableW(L"VEYRA_TEST_PATCH_RESTORE_FAIL",L"1");
        graph.shutdown();
        if(argc>1&&!initFailure&&!anr){
            SetEnvironmentVariableW(L"VEYRA_TEST_PATCH_RESTORE_FAIL",nullptr);
            const bool rejected=!ngx::FgCompatibilitySession::processHealthy()&&!graph.initialize(desc);
            std::cout<<"ROLLBACK failure-retains-storage-and-rejects-reinit="<<rejected<<'\n';
            ok=ok&&rejected;break;
        }
        ok=ok&&ngx::FgCompatibilitySession::processHealthy();
        std::cout<<"LIFECYCLE multiplier="<<multiplier<<" generated="<<generated<<" reset-and-shutdown="<<ok<<'\n';
    }
    graph.shutdown();ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
}
