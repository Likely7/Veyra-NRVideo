#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/sink/ImageExportSink.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// One real HWND, one process. Test reads back only to establish that actual
// generated/real presentations contain pixels; production never calls it.
int wmain(int argc, wchar_t** argv) {
    using namespace veyra;
    if(argc<2||FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    const bool amd=argc>2&&std::wstring_view(argv[2])==L"amd";
    std::filesystem::create_directories(argv[1]);
    HWND window=CreateWindowExW(0,L"STATIC",L"Veyra FG switch acceptance",WS_POPUP,
        40,40,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st=Status::Ok;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;device.requiredVendorId=amd?0x1002:0;
    bool ok=window&&ctx.initialize(device,st)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st);
    if(!ok){if(window)DestroyWindow(window);return 2;}
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;ctx.device()->QueryInterface(IID_PPV_ARGS(&info));
    ctx.directQueue()->SetName(L"Veyra FG acceptance producer queue");
    UINT64 diagnosed=0;
    const auto diagnose=[&](unsigned cycle,unsigned input,const char* stage){
        if(!info)return;
        const auto count=info->GetNumStoredMessagesAllowedByRetrievalFilter();
        for(;diagnosed<count;++diagnosed){
            SIZE_T size=0;info->GetMessage(diagnosed,nullptr,&size);std::vector<uint8_t> data(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
            if(SUCCEEDED(info->GetMessage(diagnosed,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)
                std::cout<<"GPU_DIAG cycle="<<cycle<<" input="<<input<<" stage="<<stage<<" "<<message->pDescription<<std::endl;
        }
    };
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    AVFrame* frame=av_frame_alloc();
    if(!frame){ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return 2;}
    frame->format=AV_PIX_FMT_RGBA;frame->width=640;frame->height=360;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    ok=av_frame_get_buffer(frame,32)>=0;
    struct Case{engine::FrameGenerationBackend backend;unsigned multiplier;};
    std::vector<Case> cases;
    for(unsigned repeat=0;repeat<3;++repeat){
        cases.push_back({engine::FrameGenerationBackend::Fsr,2});
        cases.push_back({engine::FrameGenerationBackend::XeSS,4});
        cases.push_back({engine::FrameGenerationBackend::Fsr,2});
        cases.push_back({engine::FrameGenerationBackend::XeSS,2});
        cases.push_back({engine::FrameGenerationBackend::Dlss,1});
        if(!amd)cases.push_back({engine::FrameGenerationBackend::Dlss,2});
    }
    uint64_t source=0;unsigned cycles=0,totalGenerated=0;
    for(const auto& c:cases){
        if(!ok)break;++cycles;
        pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=640;desc.sourceHeight=desc.workHeight=360;
        desc.rgbInput=true;desc.enableNr=desc.enableSr=false;desc.enableFg=c.multiplier>1;
        desc.fgMultiplier=std::max(2u,c.multiplier);desc.frameGenerationBackend=c.backend;
        desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();desc.settingsRevision=cycles;
        desc.opticalFlowBackend=amd?engine::OpticalFlowBackend::AmdFidelityFx:engine::OpticalFlowBackend::Nvidia;
        ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
        if(!ok){std::cout<<"SWITCH_OPEN_FAIL backend="<<engine::frameGenerationBackendName(c.backend)<<" multiplier="<<c.multiplier<<std::endl;break;}
        for(unsigned slot=0;slot<2;++slot){
            graph.videoFrameResource(slot)->SetName(L"Veyra acceptance graph video");
            graph.generatedFrameResource(slot)->SetName(L"Veyra acceptance graph generated");
        }
        if(graph.presentDepth())graph.presentDepth()->SetName(L"Veyra acceptance graph depth");
        diagnose(cycles,0,"open");
        unsigned generated=0,nonblack=0;
        for(unsigned i=0;ok&&i<40;++i){
            if(i==20)SetWindowPos(window,nullptr,40,40,800,450,SWP_NOZORDER|SWP_NOACTIVATE);
            if(i==30)SetWindowPos(window,nullptr,40,40,640,360,SWP_NOZORDER|SWP_NOACTIVATE);
            const unsigned left=60+i*4;
            for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){
                auto* p=frame->data[0]+size_t(y)*frame->linesize[0]+x*4;
                p[0]=uint8_t(y>130&&y<230&&x>=left&&x<left+80?230:32);
                p[1]=p[0];p[2]=p[0];p[3]=255;
            }
            ok=presenter.beginSourceInput()&&presenter.beginSourceProcessing();
            pipeline::EnhanceGraph::FrameOutputs out;
            ++source;
            ok=ok&&graph.process(frame,double(source-1)*1000/30,i==0||i==24,out,source);
            diagnose(cycles,i,"process");
            if(!ok)break;
            presenter.sourceProcessed(out.batch.identity);
            ok=ctx.waitForFenceValue(std::max(out.videoFenceValue,out.genFenceValue))&&graph.resolveGeneration(out);
            for(unsigned k=0;ok&&k<out.batch.count;++k){
                const auto& item=out.batch.frames[k];const bool gen=item.kind==pipeline::FrameKind::Generated;
                if(gen&&item.validity!=pipeline::GenerationValidity::Valid)continue;
                ok=presenter.present(ctx,ring,graph,item.lease->slot,gen,false,0,false,.5f,item.identity,{},item.pts100ns);
                diagnose(cycles,i,"present");
                if(gen)++generated;
                if(i==10||i==35){
                    sink::RgbaImage image;
                    // XeSS's proxy exposes buffers still used by its private
                    // presentation queue. An application-queue fence cannot
                    // authorize a test readback from that buffer. Validate its
                    // real input here and use the SDK status for generated counts;
                    // FSR/DLSS outputs are read from our owned presentation queue.
                    ok=ok&&(c.backend==engine::FrameGenerationBackend::XeSS
                        ?sink::readRgba8(ctx,ring,graph.videoFrameResource(item.lease->slot),image)
                        :presenter.readPresentedFrameForTest(ctx,ring,image));
                    diagnose(cycles,i,"readback");
                    if(ok){
                        uint64_t light=0;for(size_t p=0;p<image.pixels.size();p+=4)light+=image.pixels[p];
                        ok=light>uint64_t(image.width)*image.height*10;
                        if(ok)++nonblack;
                        if(gen&&i==10)ok=sink::saveImage((std::filesystem::path(argv[1])/(std::to_wstring(cycles)+L"-generated.png")).wstring(),image)&&ok;
                    }
                }
            }
            if(c.backend==engine::FrameGenerationBackend::XeSS)generated=unsigned(presenter.xessGeneratedCount());
            Sleep(12);
        }
        const unsigned minGenerated=c.multiplier>1?c.backend==engine::FrameGenerationBackend::XeSS?(c.multiplier-1)*25:25:0;
        ok=ok&&generated>=minGenerated&&nonblack>=2;
        totalGenerated+=generated;
        std::cout<<"SWITCH_CASE cycle="<<cycles<<" pid="<<GetCurrentProcessId()<<" hwnd="<<window
            <<" backend="<<engine::frameGenerationBackendName(c.backend)<<" multiplier="<<c.multiplier
            <<" generated="<<generated<<" nonblack="<<nonblack<<" pass="<<ok<<std::endl;
        ok=ring.drainQueue()&&ok;presenter.close();graph.shutdown();
    }
    if(ok){
        // Unsupported FSR 4 must fail before a swapchain is touched. The same
        // HWND must subsequently reopen, with no FSR 3.1 mislabeled as ML.
        pipeline::EnhanceGraphDesc ml;ml.sourceWidth=ml.workWidth=640;ml.sourceHeight=ml.workHeight=360;
        ml.rgbInput=ml.enableFg=true;ml.enableNr=ml.enableSr=false;ml.fgMultiplier=2;
        ml.frameGenerationBackend=engine::FrameGenerationBackend::Fsr4;ml.opticalFlowBackend=engine::OpticalFlowBackend::AmdFidelityFx;
        const bool accepted=graph.initialize(ml);
        if(accepted){std::cout<<"FSR4_AVAILABLE provider="<<graph.fsrProviderVersion()<<std::endl;graph.shutdown();}
        else{ok=graph.failedBackend()==engine::FailedBackend::Fg;graph.shutdown();
            ml.frameGenerationBackend=engine::FrameGenerationBackend::Fsr;
            ok=ok&&graph.initialize(ml)&&presenter.open(ctx,window,graph)&&graph.createViews();
            std::cout<<"FSR4_UNSUPPORTED_ROLLBACK pass="<<ok<<std::endl;
            presenter.close();graph.shutdown();}
    }
    unsigned errors=0;
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(SUCCEEDED(info->GetMessage(i,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
            ++errors;std::cout<<"GPU_ERROR "<<message->pDescription<<std::endl;}
    }
    ok=ok&&errors==0&&cycles==cases.size();
    av_frame_free(&frame);ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();
    std::cout<<"FSR_SWITCH pass="<<ok<<" cycles="<<cycles<<" generated="<<totalGenerated<<" debugErrors="<<errors<<std::endl;
    return ok?0:1;
}
