#include "veyra/source/ScreenCaptureSource.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/engine/EngineController.h"
#include <windows.h>
#include <dwmapi.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
extern "C" {
#include <libavutil/frame.h>
}
using namespace veyra;
namespace {
int failures=0;
void check(bool pass,const char* name){std::cout<<"SCREEN "<<name<<" pass="<<pass<<std::endl;failures+=!pass;}
void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
LRESULT CALLBACK pattern(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_TIMER){InvalidateRect(w,nullptr,FALSE);return 0;}
    if(m==WM_APP+1){KillTimer(w,1);return 0;}
    if(m==WM_APP+2){SetTimer(w,1,16,nullptr);return 0;}
    if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);
        const COLORREF colors[]={RGB(225,30,45),RGB(20,210,70),RGB(25,60,225)};
        for(int i=0;i<3;++i){RECT band{r.right*i/3,0,r.right*(i+1)/3,r.bottom};auto brush=CreateSolidBrush(colors[i]);FillRect(dc,&band,brush);DeleteObject(brush);}
        RECT moving{int(GetTickCount64()/10%std::max(1L,r.right-24)),8,0,32};moving.right=moving.left+24;FillRect(dc,&moving,HBRUSH(GetStockObject(WHITE_BRUSH)));EndPaint(w,&ps);return 0;}
    if(m==WM_CLOSE){DestroyWindow(w);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
bool next(source::ScreenCaptureSource& source,pipeline::FramePacket& p,const AVFrame** f,unsigned timeout=3000){
    const auto end=GetTickCount64()+timeout;while(GetTickCount64()<end){auto status=source.read(p,f);if(status==source::SourceReadStatus::Frame)return true;if(status==source::SourceReadStatus::Error){std::wcerr<<source.status()<<std::endl;return false;}pump();Sleep(2);}return false;
}
bool content(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,source::ScreenCaptureSource& source,pipeline::FramePacket& p,const AVFrame* f){
    pipeline::EnhanceGraph graph(ctx,ring);pipeline::EnhanceGraphDesc gd;gd.sourceWidth=gd.workWidth=f->width;gd.sourceHeight=gd.workHeight=f->height;gd.rgbInput=true;gd.noFeatures=true;gd.enableNr=gd.enableSr=gd.enableFg=false;gd.hdrInput=p.colorInfo.isHdrPath();
    pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage image;
    bool ok=graph.initialize(gd)&&graph.createViews()&&graph.process(f,double(p.pts.to100ns())/10000,true,out,1,&p.colorInfo, &p.hardwareSurface);
    if(ok)ok=sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),image);
    if(ok){const unsigned y=image.height/2;for(unsigned i=0;i<3;++i){unsigned x=image.width*(2*i+1)/6;auto* pixel=&image.pixels[(size_t(y)*image.width+x)*4];std::cout<<"PIXEL "<<i<<" "<<int(pixel[0])<<","<<int(pixel[1])<<","<<int(pixel[2])<<std::endl;ok&=pixel[i]>160&&pixel[(i+1)%3]<110&&pixel[(i+2)%3]<110;}}
    out={};ring.drainQueue();graph.shutdown();return ok;
}
}
int wmain(int argc,wchar_t** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(argc>1&&std::wstring(argv[1])==L"--target"){
        WNDCLASSW wc{};wc.lpfnWndProc=pattern;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraScreenTestPattern";RegisterClassW(&wc);
        auto window=CreateWindowW(wc.lpszClassName,L"Veyra capture test pattern",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,660,420,nullptr,nullptr,wc.hInstance,nullptr);SetTimer(window,1,16,nullptr);
        auto end=GetTickCount64()+240000;MSG msg;while(GetTickCount64()<end){if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)break;TranslateMessage(&msg);DispatchMessageW(&msg);}else Sleep(2);}return 0;
    }
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);std::wstring command=L"\""+std::wstring(path)+L"\" --target";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION process{};if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&process))return 2;
    struct Child {PROCESS_INFORMATION p;HWND window=nullptr;~Child(){if(window)PostMessageW(window,WM_CLOSE,0,0);if(WaitForSingleObject(p.hProcess,3000)==WAIT_TIMEOUT)TerminateProcess(p.hProcess,1);CloseHandle(p.hThread);CloseHandle(p.hProcess);}} child{process};
    for(int i=0;i<100&&!child.window;++i){for(auto& t:source::ScreenCaptureSource::targets(source::ScreenTargetKind::Window))if(t.processId==process.dwProcessId)child.window=HWND(t.handle);Sleep(20);}if(!child.window)return 3;
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status st;gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;
    if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,st))return 4;
    source::ScreenCaptureOptions options;check(options.fps==0,"default follows display refresh");options.target=uint64_t(child.window);options.fps=60;options.cursor=false;
    source::ScreenCaptureOptions parsed;check(source::ScreenCaptureOptions::parse(options.uri(),parsed)&&parsed.target==options.target,"URI round trip");check(!source::ScreenCaptureOptions::parse(options.uri()+L":3",parsed),"reject malformed URI");
    {source::ScreenCaptureOptions game=options;game.borderless=true;game.cursor=false;source::ScreenCaptureOptions back;
     check(source::ScreenCaptureOptions::parse(game.uri(),back)&&back.borderless&&!back.cursor&&back.target==game.target,"borderless URI round trip");
     game.borderless=false;game.cursor=true;check(source::ScreenCaptureOptions::parse(game.uri(),back)&&!back.borderless&&back.cursor,"old cursor field keeps the border");}
    source::SourceOpenDesc desc;desc.path=options.uri();desc.d3d12Device=ctx.device();desc.d3d12Queue=ctx.directQueue();source::ScreenCaptureSource source;
    bool opened=source.open(desc);check(opened,"WGC open");if(!opened){std::wcerr<<source.status()<<std::endl;return 5;}
    pipeline::FramePacket p;const AVFrame* f=nullptr;bool frame=next(source,p,&f);check(frame,"WGC first real frame");if(!frame)return 6;
    check(content(ctx,ring,source,p,f),"WGC shared GPU graph RGB stripe content");
    const auto initialReceived=source.metrics().received,initialDropped=source.metrics().dropped;
    auto end=GetTickCount64()+4000;unsigned frames=0;int64_t last=0;bool monotonic=true;
    while(GetTickCount64()<end){if(source.read(p,&f)==source::SourceReadStatus::Frame){monotonic&=p.pts.to100ns()>last;last=p.pts.to100ns();++frames;}pump();Sleep(2);}
    const auto received=source.metrics().received-initialReceived,dropped=source.metrics().dropped-initialDropped;
    check(monotonic&&frames>60&&frames<=245&&frames+2>=received-dropped&&dropped<received/10+2,"sustained capture follows producer with monotonic PTS and bounded loss");std::cout<<"RATE fourSecondsFrames="<<frames<<" received="<<received<<" dropped="<<dropped<<std::endl;
    // Hold every lease: source must stop producing rather than overwrite.
    std::vector<AVFrame*> held;for(int i=0;i<6;++i)if(next(source,p,&f))held.push_back(av_frame_clone(f));
    auto before=source.metrics().delivered;end=GetTickCount64()+200;while(GetTickCount64()<end){source.read(p,&f);Sleep(2);}check(held.size()==6&&source.metrics().delivered==before,"bounded pool refuses reuse while all leases held");
    for(auto* value:held)av_frame_free(&value);held.clear();check(next(source,p,&f),"pool resumes after lease release");
    ShowWindow(child.window,SW_MINIMIZE);Sleep(100);check(source.read(p,&f)==source::SourceReadStatus::Waiting,"minimized waits");ShowWindow(child.window,SW_RESTORE);check(next(source,p,&f)&&pipeline::breaksHistory(p.flags),"restore resets temporal history");
    SetWindowPos(child.window,nullptr,40,40,820,510,SWP_NOZORDER);end=GetTickCount64()+3000;bool resized=false;
    while(GetTickCount64()<end&&next(source,p,&f)){if(f->width>660){resized=true;break;}}
    check(resized&&unsigned(f->width)==source.info().width,"resize updates dimensions");check(content(ctx,ring,source,p,f),"resize retains RGB content");
    check(resized&&std::abs(source.info().displayAspect-double(f->width)/f->height)<1e-9,"resize updates source display aspect");
    SetWindowPos(child.window,nullptr,40,40,460,720,SWP_NOZORDER);end=GetTickCount64()+4000;resized=false;
    while(GetTickCount64()<end&&next(source,p,&f)){
        if(f->height>f->width){resized=true;break;}
    }
    check(resized&&pipeline::hasFrameFlag(p.flags,pipeline::FrameFlagBits::Resize)&&
          std::abs(source.info().displayAspect-double(f->width)/f->height)<1e-9,
          "portrait resize publishes new DAR and temporal reset flag together");
    check(resized&&content(ctx,ring,source,p,f),"portrait resize retains actual RGB content");
    // A static window cannot rely on an animation to deliver another frame
    // after the300ms pool debounce. Exercise that source contract directly,
    // independently of a QML Timer's render-loop wakeups in an inactive window.
    PostMessageW(child.window,WM_APP+1,0,0);Sleep(80);
    SetWindowPos(child.window,nullptr,40,40,700,430,SWP_NOZORDER);
    end=GetTickCount64()+4000;bool staticResized=false;
    while(GetTickCount64()<end&&next(source,p,&f)){
        if(f->width>f->height&&f->width>600){staticResized=true;break;}
    }
    check(staticResized&&pipeline::hasFrameFlag(p.flags,pipeline::FrameFlagBits::Resize)&&
          source.info().width==unsigned(f->width)&&source.info().height==unsigned(f->height)&&
          std::abs(source.info().displayAspect-double(f->width)/f->height)<1e-9,
          "static window resize updates real frame metadata without ongoing animation");
    PostMessageW(child.window,WM_APP+2,0,0);
    source.close();options.left=30;options.right=30;options.top=20;options.bottom=20;desc.path=options.uri();check(source.open(desc)&&next(source,p,&f)&&f->width<820,"cropped source opens");source.close();
    options.left=options.right=options.top=options.bottom=0;
    options.fps=0;check(source::ScreenCaptureOptions::parse(options.uri(),parsed)&&parsed.fps==0,"automatic rate URI round trip");
    MONITORINFOEXW monitorInfo{};monitorInfo.cbSize=sizeof(monitorInfo);DEVMODEW displayMode{};displayMode.dmSize=sizeof(displayMode);
    const bool displayKnown=GetMonitorInfoW(MonitorFromWindow(child.window,MONITOR_DEFAULTTONEAREST),&monitorInfo)&&EnumDisplaySettingsW(monitorInfo.szDevice,ENUM_CURRENT_SETTINGS,&displayMode);
    desc.path=options.uri();const bool autoOpened=source.open(desc)&&next(source,p,&f);
    check(autoOpened&&displayKnown&&source.info().nominalRateNum>0&&source.info().nominalRateDen>0&&std::abs(source.info().averageFps-displayMode.dmDisplayFrequency)<1.1,"automatic window rate matches target display");
    check(autoOpened&&p.duration.to100ns()>0&&std::abs(double(p.duration.to100ns())*source.info().averageFps-10000000)<1000,"automatic first frame has valid nominal duration");
    std::cout<<"AUTO rate="<<source.info().nominalRateNum<<"/"<<source.info().nominalRateDen<<" displayHz="<<displayMode.dmDisplayFrequency<<std::endl;
    if(autoOpened)check(content(ctx,ring,source,p,f),"automatic window capture actual RGB content");source.close();
    if(argc>1&&std::wstring(argv[1])==L"--engine"){
        HWND host=CreateWindowW(L"STATIC",L"Screen engine test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,880,40,720,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        SetWindowDisplayAffinity(host,WDA_EXCLUDEFROMCAPTURE);engine::EngineController engine;engine::PlayerOptions settings;settings.nr=true;settings.fg=true;settings.fgMultiplier=4;engine.open(host,options.uri(),settings);
        end=GetTickCount64()+20000;while(GetTickCount64()<end){pump();Sleep(10);}auto snapshot=engine.snapshot();check(snapshot.running&&!snapshot.failed&&snapshot.frames>100&&snapshot.generated>100&&snapshot.nrEvaluated>100,"screen NR and DLSS4x actual evaluation");std::wcout<<L"ENGINE "<<snapshot.status<<L" frames="<<snapshot.frames<<L" generated="<<snapshot.generated<<L" nr="<<snapshot.nrEvaluated<<std::endl;
        const auto framesBeforeResize=snapshot.frames;
        SetWindowPos(child.window,nullptr,40,40,820,510,SWP_NOZORDER);
        end=GetTickCount64()+10000;bool engineResized=false;
        while(GetTickCount64()<end){
            pump();Sleep(10);snapshot=engine.snapshot();
            if(snapshot.sourceWidth>snapshot.sourceHeight&&snapshot.sourceWidth>700&&snapshot.frames>framesBeforeResize+5){engineResized=true;break;}
            if(snapshot.failed)break;
        }
        check(engineResized&&std::abs(snapshot.sourceDisplayAspect-double(snapshot.sourceWidth)/snapshot.sourceHeight)<1e-9,
              "engine window resize refreshes UI dimensions and DAR while frames advance");
        engine.stop();end=GetTickCount64()+10000;while(!engine.idle()&&GetTickCount64()<end){pump();Sleep(10);}check(engine.idle(),"engine stop drains");DestroyWindow(host);
    }
    HWND excluded=CreateWindowW(L"STATIC",L"Screen exclusion test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,880,40,200,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    DWORD affinity=0;check(GetWindowDisplayAffinity(excluded,&affinity)&&affinity==WDA_NONE,"initial own window remains capturable");
    auto monitors=source::ScreenCaptureSource::targets(source::ScreenTargetKind::Monitor);check(!monitors.empty(),"enumerate monitors");
    if(!monitors.empty())for(auto method:{source::ScreenCaptureMethod::Wgc,source::ScreenCaptureMethod::Duplication}){
        const auto monitor=MonitorFromWindow(child.window,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(monitor,&mi);
        RECT client{};GetClientRect(child.window,&client);POINT origin{};ClientToScreen(child.window,&origin);
        SetWindowPos(child.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);Sleep(150);
        options.kind=source::ScreenTargetKind::Monitor;options.target=uint64_t(monitor);options.method=method;
        options.left=origin.x-mi.rcMonitor.left;options.top=origin.y-mi.rcMonitor.top;options.right=mi.rcMonitor.right-origin.x-client.right;options.bottom=mi.rcMonitor.bottom-origin.y-client.bottom;
        desc.path=options.uri();bool ok=source.open(desc)&&next(source,p,&f);check(ok&&content(ctx,ring,source,p,f),method==source::ScreenCaptureMethod::Wgc?"WGC monitor actual RGB content":"DXGI monitor actual RGB content");if(!ok)std::wcerr<<source.status()<<std::endl;
        check(GetWindowDisplayAffinity(excluded,&affinity)&&affinity==WDA_EXCLUDEFROMCAPTURE,"monitor capture excludes own window");
        source.close();check(GetWindowDisplayAffinity(excluded,&affinity)&&affinity==WDA_NONE,"stop restores original window affinity");
    }
    DestroyWindow(excluded);
    options.left=options.top=options.right=options.bottom=0;
    options.kind=source::ScreenTargetKind::Window;options.target=uint64_t(child.window);options.method=source::ScreenCaptureMethod::Wgc;desc.path=options.uri();check(source.open(desc),"window reopen");PostMessageW(child.window,WM_CLOSE,0,0);WaitForSingleObject(process.hProcess,3000);check(source.read(p,&f)==source::SourceReadStatus::Error,"closed target reports error");source.close();
    std::cout<<"SCREEN failures="<<failures<<std::endl;return failures?1:0;
}
