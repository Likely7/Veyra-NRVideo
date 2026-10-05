#include "veyra/engine/VideoPresenter.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <iostream>
#include <fstream>

// Own window/backbuffer only, matching the QML video's WM_PAINT contract.
// Uses the real graph/presenter and checks full pixels after each invalidation.
namespace {
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_PAINT){ValidateRect(window,nullptr);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra;if(argc!=3)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Logger::instance().openFile((out/L"engine.log").wstring());
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=procedure;wc.lpszClassName=L"VeyraPausedPresentOwnedFixture";
    if(!RegisterClassW(&wc))return 2;
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Veyra paused-present test",WS_POPUP,50,50,640,384,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 2;ShowWindow(window,SW_SHOWNOACTIVATE);pump();
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;gfx::DeviceContextDesc device;device.enableDebugLayer=true;
    if(!ctx.initialize(device,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,status))return 2;
    source::MediaFileSource media;source::SourceOpenDesc open;open.path=argv[1];open.preferHardwareDecode=false;if(!media.open(open))return 2;
    pipeline::FramePacket packet;const AVFrame* input=nullptr;if(media.read(packet,&input)!=source::SourceReadStatus::Frame)return 2;
    engine::EffectChain chain;chain.nodeCount=1;chain.nodes[0].type=engine::EffectType::NrEnhance;chain.nodes[0].enabled=true;
    chain.nodes[0].nr.sizePolicy=pipeline::NrSizePolicy::Native;engine::EnhancementSettings settings;engine::fromChain(chain,settings);
    engine::StageRequest request;request.width=media.info().width;request.height=media.info().height;request.nr=true;
    pipeline::EnhanceGraphDesc desc;engine::describeStages(request,settings,desc);desc.enableNvofStandalone=true;desc.enableFg=false;desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    if(!graph.initialize(desc)||!presenter.open(ctx,window,graph)||!graph.createViews())return 2;
    pipeline::EnhanceGraph::FrameOutputs output;bool pass=graph.process(input,0,true,output,1,&packet.colorInfo,nullptr,true)&&ring.waitIdle();
    engine::PreviewView view{};int comparison=0;float split=.5f;
    auto present=[&]{return presenter.present(ctx,ring,graph,output.videoSlot,false,true,comparison,false,split,output.batch.identity,view,-1,true);};
    auto snapshot=[&](const wchar_t* name){sink::RgbaImage image;if(!presenter.readPresentedFrameForTest(ctx,ring,image)){pass=false;return image;}
        std::ofstream f(out/(std::wstring(name)+L".rgba"),std::ios::binary);f.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));return image;};
    pass=pass&&present();auto first=snapshot(L"initial");const auto initialCount=presenter.submittedCount();
    for(unsigned i=0;i<300&&pass;++i)pass=present();auto idle=snapshot(L"idle");
    const bool disabled=GetEnvironmentVariableW(L"VEYRA_TEST_DISABLE_PAUSED_PRESENT_REUSE",nullptr,0)>0;
    pass=pass&&first.pixels==idle.pixels&&presenter.submittedCount()==initialCount+(disabled?300:0);
    std::cout<<"IDLE pixelsEqual="<<(first.pixels==idle.pixels)<<" presents="<<presenter.submittedCount()<<" disabled="<<disabled<<std::endl;
    view.zoom=1.2f;auto count=presenter.submittedCount();pass=pass&&present()&&presenter.submittedCount()==count+1;auto zoom=snapshot(L"zoom");pass=pass&&zoom.pixels!=idle.pixels;
    comparison=2;count=presenter.submittedCount();pass=pass&&present()&&presenter.submittedCount()==count+1;auto compare=snapshot(L"comparison");pass=pass&&compare.pixels!=zoom.pixels;
    split=.25f;count=presenter.submittedCount();pass=pass&&present()&&presenter.submittedCount()==count+1;auto moved=snapshot(L"split");pass=pass&&moved.pixels!=compare.pixels;
    // Match the debounced resize deadline without touching any other window.
    Sleep(120);SetWindowPos(window,nullptr,50,50,768,432,SWP_NOACTIVATE|SWP_NOZORDER);pump();count=presenter.submittedCount();
    pass=pass&&present()&&presenter.submittedCount()==count+1;auto resized=snapshot(L"resized");pass=pass&&resized.width==768&&resized.height==432;
    settings.residual.total=0;settings.nrLayers[0].residual.total=0;pass=pass&&graph.applySettings(settings);
    pass=pass&&graph.process(input,0,true,output,1,&packet.colorInfo,nullptr,true)&&ring.waitIdle();count=presenter.submittedCount();
    pass=pass&&present()&&presenter.submittedCount()==count+1;auto edited=snapshot(L"producer-changed");pass=pass&&edited.pixels!=resized.pixels;
    count=presenter.submittedCount();ShowWindow(window,SW_HIDE);pump();pass=pass&&present()&&presenter.submittedCount()==count+1;
    ShowWindow(window,SW_SHOWNOACTIVATE);pump();count=presenter.submittedCount();pass=pass&&present()&&presenter.submittedCount()==count+1;
    auto restored=snapshot(L"restored");pass=pass&&restored.pixels==edited.pixels;
    std::cout<<"INVALIDATIONS pass="<<pass<<" finalPresents="<<presenter.submittedCount()<<" image="<<resized.width<<'x'<<resized.height<<std::endl;
    ring.drainQueue();output={};presenter.close();graph.shutdown();media.close();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);std::vector<unsigned char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,m,&size)))return 2;if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::cout<<"RESULT pass="<<(pass&&errors==0&&SUCCEEDED(removed))<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return pass&&errors==0&&SUCCEEDED(removed)?0:1;
}
