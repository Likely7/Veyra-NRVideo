#include "veyra/engine/VideoPresenter.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <iostream>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
using namespace veyra;
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const auto dir=std::filesystem::path(argv[1]);std::filesystem::create_directories(dir);
    HWND window=CreateWindowExW(0,L"STATIC",L"Preview geometry test",WS_POPUP,0,0,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 1;
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc device;
    if(!ctx.initialize(device,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 1;
    pipeline::EnhanceGraph graph(ctx,ring);engine::VideoPresenter presenter;
    pipeline::EnhanceGraphDesc desc;desc.sourceWidth=desc.workWidth=64;desc.sourceHeight=desc.workHeight=32;desc.rgbInput=desc.stillImage=desc.noFeatures=true;desc.enableNr=desc.enableFg=false;
    bool ok=graph.initialize(desc)&&presenter.open(ctx,window,graph)&&graph.createViews();
    AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_RGBA;f->width=64;f->height=32;f->color_trc=AVCOL_TRC_IEC61966_2_1;
    ok=ok&&av_frame_get_buffer(f,32)>=0;
    if(ok)for(int y=0;y<32;++y)for(int x=0;x<64;++x){auto* p=f->data[0]+y*f->linesize[0]+x*4;bool right=x>=32,bottom=y>=16;p[0]=!right||bottom?255:0;p[1]=right?255:0;p[2]=bottom?255:0;p[3]=255;}
    pipeline::EnhanceGraph::FrameOutputs out;ok=ok&&graph.process(f,0,true,out,1);
    struct Sample {int x,y,r,g,b;};
    auto test=[&](const char* name,engine::PreviewView view,int comparison,std::initializer_list<Sample> samples){
        const bool referencesValid=out.batch.count&&out.batch.frames[out.batch.count-1].lease&&out.batch.frames[out.batch.count-1].lease->referencesValid;
        sink::RgbaImage image;bool pass=presenter.present(ctx,ring,graph,out.videoSlot,false,referencesValid,comparison,false,.5f,{},view)&&presenter.readPresentedFrameForTest(ctx,ring,image);
        pass=pass&&image.width==320&&image.height==240;
        if(pass)for(auto s:samples){auto* p=image.pixels.data()+(s.y*image.width+s.x)*4;pass=pass&&std::abs(int(p[0])-s.r)<=2&&std::abs(int(p[1])-s.g)<=2&&std::abs(int(p[2])-s.b)<=2;}
        if(pass)pass=sink::saveImage((dir/(std::string(name)+".png")).wstring(),image);
        std::cout<<"PREVIEW_GEOMETRY "<<name<<" pass="<<pass<<std::endl;return pass;
    };
    if(ok)ok=test("fit",{},0,{{160,10,0,0,0},{40,80,255,0,0},{280,80,0,255,0},{40,160,255,0,255},{280,160,255,255,255}});
    if(ok)ok=test("zoom",{2,.5f,.5f},0,{{40,10,255,0,0},{280,10,0,255,0},{40,230,255,0,255}});
    if(ok)ok=test("pan",{2,.25f,.5f},0,{{280,60,255,0,0},{280,200,255,0,255}});
    if(ok)ok=test("reference",{2,.5f,.5f},1,{{40,10,255,0,0},{280,10,0,255,0}});
    if(ok)ok=test("reset",{},0,{{160,10,0,0,0},{280,80,0,255,0}});
    if(ok)ok=test("dar-4-3",{1,.5f,.5f,0,4.0/3},0,{{40,10,255,0,0},{280,10,0,255,0},{40,230,255,0,255}});
    if(ok)ok=test("stretch",{1,.5f,.5f,3},0,{{40,10,255,0,0},{280,10,0,255,0},{280,230,255,255,255}});
    if(ok)ok=test("forced-16-9",{1,.5f,.5f,4},0,{{160,10,0,0,0},{40,40,255,0,0},{280,200,255,255,255}});
    if(ok)ok=test("native",{1,.5f,.5f,1},0,{{40,80,0,0,0},{145,111,255,0,0},{175,129,255,255,255}});
    if(ok){
        SetPropW(window,L"Veyra.ResizeDeferUntil",reinterpret_cast<HANDLE>(uintptr_t(GetTickCount64()+60000)));
        SetWindowPos(window,nullptr,0,0,400,400,SWP_NOMOVE|SWP_NOZORDER);
        ok=test("retained-square-client",{},0,{{160,40,0,0,0},{40,80,255,0,0},{280,80,0,255,0},{40,160,255,0,255},{160,200,0,0,0}});
        if(ok)ok=test("retained-zoom",{2,.5f,.5f},0,{{40,10,255,0,0},{280,10,0,255,0},{40,230,255,0,255}});
        SetWindowPos(window,nullptr,0,0,320,240,SWP_NOMOVE|SWP_NOZORDER);
        RemovePropW(window,L"Veyra.ResizeDeferUntil");
    }
    if(ok){
        for(int y=0;y<32;++y)for(int x=0;x<64;++x){auto* p=f->data[0]+y*f->linesize[0]+x*4;p[0]=0;p[1]=0;p[2]=255;p[3]=255;}
        pipeline::EnhanceGraph::FrameOutputs current;ok=graph.process(f,33.333,false,current,2,nullptr,nullptr,false);
        if(ok){out=std::move(current);ok=test("invalid-reference",{},1,{{40,80,0,0,255},{280,160,0,0,255}});}
    }
    ring.drainQueue();out={};presenter.close();graph.shutdown();av_frame_free(&f);ring.shutdown();ctx.shutdown();DestroyWindow(window);CoUninitialize();return ok?0:1;
}
