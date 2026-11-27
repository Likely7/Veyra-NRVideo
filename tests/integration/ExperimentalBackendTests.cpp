#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/XessPacing.h"
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;

namespace {

struct FlowSample {
    float x = 0.0f;
    float y = 0.0f;
    size_t count = 0;
};

float halfToFloat(uint16_t bits)
{
    const uint32_t sign = uint32_t(bits & 0x8000u) << 16;
    uint32_t exponent = (bits >> 10) & 0x1Fu;
    uint32_t mantissa = bits & 0x03FFu;
    uint32_t result = 0;
    if (exponent == 0) {
        if (mantissa != 0) {
            exponent = 127 - 14;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            result = sign | (exponent << 23) | ((mantissa & 0x03FFu) << 13);
        } else {
            result = sign;
        }
    } else if (exponent == 0x1Fu) {
        result = sign | 0x7F800000u | (mantissa << 13);
    } else {
        result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(result);
}

// Test-only readback. Normal playback, capture, and video export never call this.
bool readCanonicalFlow(gfx::D3D12DeviceContext& ctx, gfx::CommandSlotRing& ring,
                       ID3D12Resource* flow, FlowSample& sample)
{
    if (flow == nullptr || flow->GetDesc().Format != DXGI_FORMAT_R16G16_FLOAT) return false;
    const D3D12_RESOURCE_DESC desc = flow->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    ctx.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;

    Status status = Status::Ok;
    uint32_t slot = 0;
    ID3D12GraphicsCommandList* list = ring.acquireNext(slot, status);
    if (list == nullptr) return false;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {flow, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = flow;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;

    uint8_t* data = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&data)))) return false;
    std::vector<float> xs, ys;
    // Ignore the outer quarter. It contains intentional out-of-bounds rejection.
    for (uint32_t y = desc.Height / 4; y < desc.Height * 3 / 4; y += 4) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(data + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch);
        for (uint32_t x = uint32_t(desc.Width) / 4; x < uint32_t(desc.Width) * 3 / 4; x += 4) {
            const float vx = halfToFloat(row[x * 2]);
            const float vy = halfToFloat(row[x * 2 + 1]);
            if (std::isfinite(vx) && std::isfinite(vy)) {
                xs.push_back(vx);
                ys.push_back(vy);
            }
        }
    }
    D3D12_RANGE empty{0, 0};
    readback->Unmap(0, &empty);
    if (xs.size() < 64) return false;
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    sample = {xs[xs.size() / 2], ys[ys.size() / 2], xs.size()};
    return true;
}

bool checkDisplayMotion(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring){
    constexpr unsigned w=640,h=360;
    auto source=pipeline::makeTexture(ctx.device(),w,h,DXGI_FORMAT_R16G16_FLOAT,true);
    auto output=pipeline::makeTexture(ctx.device(),w,h,DXGI_FORMAT_R16G16_FLOAT,true);
    pipeline::ComputePass pass;std::vector<uint8_t> cs;
    if(!source||!output||!pipeline::loadShaderBytes("PresentMotion.dxil",cs)||!pass.create(ctx.device(),cs,3,1,1,16))return false;
    const auto cpu=pass.heap->GetCPUDescriptorHandleForHeapStart();
    const auto gpu=pass.heap->GetGPUDescriptorHandleForHeapStart();
    pipeline::makeSrv(ctx.device(),source.Get(),DXGI_FORMAT_R16G16_FLOAT,cpu);
    pipeline::makeUav(ctx.device(),output.Get(),DXGI_FORMAT_R16G16_FLOAT,{cpu.ptr+pass.increment});
    pipeline::makeUav(ctx.device(),source.Get(),DXGI_FORMAT_R16G16_FLOAT,{cpu.ptr+2*pass.increment});
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> clearHeap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;
    if(FAILED(ctx.device()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&clearHeap))))return false;
    pipeline::makeUav(ctx.device(),source.Get(),DXGI_FORMAT_R16G16_FLOAT,clearHeap->GetCPUDescriptorHandleForHeapStart());
    for(unsigned test=0;test<8;++test){
        Status st;uint32_t slot=0;auto* list=ring.acquireNext(slot,st);if(!list)return false;
        pipeline::StateTracker states;
        states.transition(list,source.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap* heaps[]={pass.heap.Get()};list->SetDescriptorHeaps(1,heaps);
        const float velocity[4]={-2,1,0,0};
        list->ClearUnorderedAccessViewFloat({gpu.ptr+2*pass.increment},clearHeap->GetCPUDescriptorHandleForHeapStart(),source.Get(),velocity,0,nullptr);
        states.transition(list,source.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        states.transition(list,output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // Identity; 2X zoom; view shifted 10% right; explicit history reset.
        const float zoom=test==1?2.0f:1.0f;
        const unsigned turns=test>=4&&test<=6?test-3:test==7?1:0;
        const float renderW=test==7?303.75f:960*zoom,renderH=540*zoom;
        const float map[16]={640,360,960,540,renderW,test==2?.6f:.5f,.5f,test==3?1.0f:0.0f,
            renderH,.5f,.5f,float(turns),960,540,renderW,renderH};
        pass.bind(list,map,gpu.ptr,gpu.ptr+pass.increment);list->Dispatch(w/8,h/8,1);
        states.transition(list,source.Get(),D3D12_RESOURCE_STATE_COMMON);
        states.transition(list,output.Get(),D3D12_RESOURCE_STATE_COMMON);
        if(!ring.submitAndSignal(slot)||!ring.drainQueue())return false;
        list=ring.acquireNext(slot,st);if(!list)return false;
        pipeline::StateTracker readStates;
        readStates.transition(list,output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!ring.submitAndSignal(slot))return false;
        FlowSample sample;if(!readCanonicalFlow(ctx,ring,output.Get(),sample))return false;
        const float expectedX=test==3?0:test==2?62:turns==1?-renderW/540.0f:turns==2?2:turns==3?960.0f/540.0f:-2*zoom;
        const float expectedY=test==3?0:turns==1?-1.125f:turns==2?-1:turns==3?1.125f:zoom;
        const bool ok=std::abs(sample.x-expectedX)<.08f&&std::abs(sample.y-expectedY)<.08f;
        std::cout<<"DISPLAY_MOTION case="<<test<<" actual="<<sample.x<<","<<sample.y<<" expected="<<expectedX<<","<<expectedY<<" pass="<<ok<<std::endl;
        list=ring.acquireNext(slot,st);if(!list)return false;
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON};
        list->ResourceBarrier(1,&b);
        if(!ring.submitAndSignal(slot)||!ring.drainQueue()||!ok)return false;
    }
    return true;
}

} // namespace

int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    if(wcscmp(argv[1],L"present-motion")==0){
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
        const bool initialized=ctx.initialize({},status)&&ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status);
        const bool ok=initialized&&checkDisplayMotion(ctx,ring);
        if(initialized)ring.drainQueue();ring.shutdown();ctx.shutdown();CoUninitialize();return ok?0:1;
    }
    const bool dis=std::wstring(argv[1]).find(L"dis")==0;
    const bool pan=wcscmp(argv[1],L"xess-pan2")==0||wcscmp(argv[1],L"xess-pan4")==0;
    const bool recovery=wcscmp(argv[1],L"xess-recovery2")==0||wcscmp(argv[1],L"xess-recovery3")==0||wcscmp(argv[1],L"xess-recovery4")==0||wcscmp(argv[1],L"xess-resize4")==0;
    const bool resizeRecovery=wcscmp(argv[1],L"xess-resize4")==0;
    const bool soak=wcscmp(argv[1],L"xess-soak4")==0;
    const bool yuy50=wcscmp(argv[1],L"xess-yuy50-2")==0||wcscmp(argv[1],L"xess-yuy50-4")==0;
    const bool xess=yuy50||soak||recovery||pan||wcscmp(argv[1],L"xess")==0||wcscmp(argv[1],L"dis-xess")==0;
    const bool amd=wcscmp(argv[1],L"amd")==0;
    const bool sr=std::wstring(argv[1]).find(L"sr")==0;
    if(!xess&&!amd&&!sr&&!dis&&wcscmp(argv[1],L"nvof1080")!=0)return 2;
    const bool full=std::wstring(argv[1]).find(L"1080")!=std::wstring::npos;
    const unsigned width=(sr||full)?1920:640,height=(sr||full)?1080:360,frames=soak?3600:yuy50?100:sr?3:48;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::create_directories(argv[2]);
    Logger::instance().openFile((std::filesystem::path(argv[2])/"engine.log").wstring());
    SetEnvironmentVariableW(L"VEYRA_VERBOSE_FRAME_LOGS",soak?nullptr:L"1");
    HWND window=CreateWindowExW(0,L"STATIC",L"Veyra experimental backend test",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 1;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;
    gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 1;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;ctx.device()->QueryInterface(IID_PPV_ARGS(&info));
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth=desc.workWidth=width;desc.sourceHeight=desc.workHeight=height;
    desc.rgbInput=!yuy50;desc.yuy2Input=yuy50;desc.enableNr=false;desc.enableFg=xess;desc.noNgx=true;
    desc.enableNvofStandalone=true;
    desc.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    if(soak||wcscmp(argv[1],L"xess-pan4")==0||wcscmp(argv[1],L"xess-yuy50-4")==0)desc.fgMultiplier=4;
    if(recovery)desc.fgMultiplier=(resizeRecovery||wcscmp(argv[1],L"xess-recovery4")==0)?4:wcscmp(argv[1],L"xess-recovery3")==0?3:2;
    desc.opticalFlowBackend=dis?engine::OpticalFlowBackend::GpuDis:amd?engine::OpticalFlowBackend::AmdFidelityFx:engine::OpticalFlowBackend::Nvidia;
    if(sr){
        desc.sourceWidth=width;desc.sourceHeight=height;
        desc.workWidth=std::wstring(argv[1]).find(L"8k")!=std::wstring::npos?7680:2560;
        desc.workHeight=desc.workWidth==7680?4320:1440;
        desc.nrWidth=1920;desc.nrHeight=1080;desc.noNgx=false;desc.enableSr=true;
        desc.enableNvofStandalone=false;
        desc.videoSrQuality=std::wstring(argv[1]).find(L"video")!=std::wstring::npos?1:0;
        desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    }
    bool ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
    if(xess)ok=ok&&presenter.xessActive();
    if(xess)ok=ok&&checkDisplayMotion(ctx,ring);
    if(xess&&desc.fgMultiplier>2&&ok&&ctx.adapter().vendorId!=0x8086){
        const auto again=gfx::XessPacing::install(GetModuleHandleW(L"libxess_fg.dll"),desc.fgMultiplier-1);
        ok=again.installed;
    }
    AVFrame* frame=av_frame_alloc();frame->format=yuy50?AV_PIX_FMT_YUYV422:AV_PIX_FMT_RGBA;frame->width=int(width);frame->height=int(height);frame->color_trc=AVCOL_TRC_IEC61966_2_1;
    ok=ok&&av_frame_get_buffer(frame,32)>=0;
    pipeline::EnhanceGraph::FrameOutputs out;
    FlowSample rightward{}, leftward{};
    engine::PreviewView view;
    uint64_t panGenerated=0;
    unsigned recovered=0;
    UINT64 previousMessages=0;
    if(pan)view.zoom=1.5f;
    const auto cadenceStart=std::chrono::steady_clock::now();
    for(unsigned i=0;ok&&i<frames;++i){
        if(soak||yuy50)std::this_thread::sleep_until(cadenceStart+std::chrono::microseconds(uint64_t(i)*1000000/(yuy50?50:30)));
        ok=presenter.beginSourceInput()&&ok;
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        const int phase = i < 24 ? int(i) : int(i - 24);
        const int velocity = i < 24 ? 2 : -2;
        for(int y=0;y<int(height);++y)for(int x=0;x<int(width);++x){
            const int px=(x-phase*velocity+int(width))%int(width);auto* p=frame->data[0]+y*frame->linesize[0]+x*(yuy50?2:4);
            // A spatially non-periodic pattern makes a two-pixel translation
            // identifiable. The previous striped pattern had repeating aliases.
            uint32_t hash=uint32_t(px)*0x9E3779B9u^uint32_t(y)*0x85EBCA6Bu;
            hash^=hash>>16;hash*=0x7FEB352Du;hash^=hash>>15;hash*=0x846CA68Bu;hash^=hash>>16;
            if(yuy50){p[0]=uint8_t(16+hash%220);p[1]=128;}
            else{p[0]=uint8_t(hash);p[1]=uint8_t(hash>>8);p[2]=uint8_t(hash>>16);p[3]=255;}
        }
        const bool forcedReset=i==0||i==24||(recovery&&(i==8||i==32||i==40));
        out={};ok=ok&&presenter.beginSourceProcessing()&&graph.process(frame,i*1000.0/(yuy50?50:30),forcedReset,out,i+1);
        if(ok)presenter.sourceProcessed(out.batch.identity);
        if(ok&&recovery&&i==18){
            // Repaint an older identity while the next source is prepared.
            // It must not consume that source's identity or add generated frames.
            const auto before=presenter.xessGeneratedCount();
            auto older=out.batch.identity;--older.sourceFrameId;
            ok=presenter.present(ctx,ring,graph,out.videoSlot,false,false,0,false,.5f,older,view);
            ok=ok&&presenter.xessGeneratedCount()==before;
        }
        const bool moving=pan&&i>=4&&i<44;
        if(moving)view.pan(i<24?3.0f:-3.0f,1.0f,960,540,float(width),float(height));
        const auto beforePresent=presenter.xessGeneratedCount();
        if(ok)ok=presenter.present(ctx,ring,graph,out.videoSlot,false,false,0,false,.5f,out.batch.identity,view);
        if(moving)panGenerated+=presenter.xessGeneratedCount()-beforePresent;
        if(recovery&&(i==9||i==33||i==41)){
            const auto delta=presenter.xessGeneratedCount()-beforePresent;
            std::cout<<"RECOVERY frame="<<i<<" generated="<<delta<<" expected="<<desc.fgMultiplier-1<<std::endl;
            ok=ok&&delta==desc.fgMultiplier-1;
            if(delta==desc.fgMultiplier-1)++recovered;
        }
        if(ok&&i==16){
            const auto before=presenter.xessGeneratedCount();
            ok=presenter.present(ctx,ring,graph,out.videoSlot,false,false,0,false,.5f,out.batch.identity,view);
            if(xess)ok=ok&&presenter.xessGeneratedCount()==before;
        }
        if(i==24&&(!recovery||resizeRecovery))SetWindowPos(window,nullptr,0,0,800,600,SWP_NOACTIVATE|SWP_NOZORDER);
        if((amd||dis)&&(i==23||i==47)){
            ok=ring.drainQueue()&&ok;
            FlowSample& sample=i==23?rightward:leftward;
            ok=ok&&readCanonicalFlow(ctx,ring,graph.flowResource(),sample);
        }
        if(!soak&&!yuy50)Sleep(34);
        if(recovery&&info){
            const auto messages=info->GetNumStoredMessagesAllowedByRetrievalFilter();
            if(messages!=previousMessages)std::cout<<"DEBUG frame="<<i<<" messages="<<messages<<std::endl;
            previousMessages=messages;
        }
    }
    ok=ring.drainQueue()&&ok;
    const auto generated=presenter.xessGeneratedCount();
    const auto dispatches=dis?graph.metrics().gpuDisExecuteCount:graph.metrics().amdOfExecuteCount;
    if(sr&&ok){
        sink::RgbaImage image;
        ok=graph.metrics().srEvaluateCount==frames&&sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
        uint64_t nonblack=0;for(size_t i=0;i+3<image.pixels.size();i+=4)nonblack+=image.pixels[i]>8||image.pixels[i+1]>8||image.pixels[i+2]>8;
        ok=ok&&image.width==desc.workWidth&&image.height==desc.workHeight&&nonblack>uint64_t(image.width)*image.height/2;
        std::cout<<"SR extent="<<image.width<<"x"<<image.height<<" evaluate="<<graph.metrics().srEvaluateCount<<" nonblack="<<nonblack<<" pass="<<ok<<std::endl;
    }
    if(xess)ok=ok&&generated>10;
    if(yuy50){
        const auto expectedMinimum=(frames-8)*(desc.fgMultiplier-1);
        ok=ok&&generated>=expectedMinimum;
        std::cout<<"YUY50 multiplier="<<desc.fgMultiplier<<" generated="<<generated<<" minimum="<<expectedMinimum<<" pass="<<ok<<std::endl;
    }
    if(recovery)ok=ok&&recovered==3;
    if(pan){
        ok=ok&&panGenerated>10;
        std::cout<<"PAN multiplier="<<desc.fgMultiplier<<" generatedWhileMoving="<<panGenerated<<" pass="<<ok<<std::endl;
    }
    if(amd||dis){
        // The source moves right then left. Canonical current->previous motion
        // must therefore point left then right with no material vertical drift.
        const bool directionOk=rightward.count>0&&leftward.count>0&&rightward.x<-0.5f&&leftward.x>0.5f&&
            std::abs(rightward.y)<0.5f&&std::abs(leftward.y)<0.5f;
        std::cout<<(dis?"GPU DIS flow rightward=(":"AMD flow rightward=(")<<rightward.x<<","<<rightward.y<<") leftward=("<<leftward.x<<","<<leftward.y<<") samples="<<rightward.count<<"/"<<leftward.count<<" directionPass="<<directionOk<<std::endl;
        ok=ok&&dispatches==(dis?46u:48u)&&directionOk;
    }
    out={};presenter.close();graph.shutdown();av_frame_free(&frame);
    uint64_t errors=0;
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(SUCCEEDED(info->GetMessage(i,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;log::error("debug",message->pDescription);}
    }
    ok=ok&&errors==0;
    std::cout<<"BACKEND "<<(dis?(xess?"gpu-dis-xess":"gpu-dis"):xess?"xess":amd?"amd-of":"sr")<<" generated="<<generated<<" amdDispatches="<<dispatches<<" debugErrors="<<errors<<" pass="<<ok<<" (SDK submissions, not scanout or visual quality)\n";
    info.Reset();ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return ok?0:1;
}
