#include "DuplicateGpuFixture.h"
#include "veyra/source/ScreenCaptureSource.h"
#include "veyra/Log.h"
#include <d3d12sdklayers.h>
#include <fstream>
#include <thread>
#include <algorithm>

// Isolated measurement, never a product source-frame skip. WGC captures only
// this test's own pattern window; no user desktop/application pixels are read.
namespace {
using namespace veyra;
unsigned failures=0;
void check(bool ok,const char* name){std::cout<<"CHECK "<<name<<" pass="<<ok<<std::endl;failures+=!ok;}
pipeline::ComPtr<ID3D12Resource> upload(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,
    unsigned w,unsigned h,bool half,unsigned mutation){
    const unsigned bytes=half?8:4;
    const auto fmt=half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
    auto texture=pipeline::makeTexture(ctx.device(),w,h,fmt,false);if(!texture)return {};
    auto desc=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    ctx.device()->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
    auto staging=pipeline::makeUploadBuffer(ctx.device(),total);if(!staging)return {};
    void* p=nullptr;D3D12_RANGE empty{};if(FAILED(staging->Map(0,&empty,&p)))return {};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto* dst=static_cast<unsigned char*>(p)+footprint.Offset+y*footprint.Footprint.RowPitch+x*bytes;
        if(half){const uint16_t v[]={0x3800,0x3400,0x3000,0x3c00};std::memcpy(dst,v,8);}
        else {const unsigned char v[]={123,37,71,255};std::memcpy(dst,v,4);}
        // All four corners and odd-size edges are deliberately exercisable.
        if(mutation&&x==w-1&&y==h-1){if(half){uint16_t v;const unsigned off=mutation==2?6:0;std::memcpy(&v,dst+off,2);++v;std::memcpy(dst+off,&v,2);}else ++dst[mutation==2?3:0];}
    }
    staging->Unmap(0,nullptr);Status status;uint32_t slot;auto* list=ring.acquireNext(slot,status);if(!list)return {};
    pipeline::StateTracker states;states.transition(list,texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=staging.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);states.transition(list,texture.Get(),D3D12_RESOURCE_STATE_COMMON);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return {};return texture;
}
bool animated=false;
LRESULT CALLBACK pattern(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_APP+1){animated=wp!=0;InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    if(message==WM_TIMER){InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);RECT rect;GetClientRect(window,&rect);
        const COLORREF colors[]={RGB(220,30,45),RGB(20,210,70),RGB(25,60,225)};
        for(unsigned i=0;i<3;++i){RECT band{LONG(rect.right*i/3),0,LONG(rect.right*(i+1)/3),rect.bottom};auto brush=CreateSolidBrush(colors[i]);FillRect(dc,&band,brush);DeleteObject(brush);}
        if(animated){RECT box{LONG(GetTickCount64()/8%std::max(1L,rect.right-40)),20,0,60};box.right=box.left+40;FillRect(dc,&box,HBRUSH(GetStockObject(WHITE_BRUSH)));}
        EndPaint(window,&ps);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
bool capture(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,dupperf::ExactChecker& checker,const std::filesystem::path& out){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring command=L"\""+std::wstring(exe)+L"\" --target";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))return false;
    struct OwnedChild {PROCESS_INFORMATION process;HWND window=nullptr;~OwnedChild(){if(window)PostMessageW(window,WM_CLOSE,0,0);if(WaitForSingleObject(process.hProcess,3000)==WAIT_TIMEOUT)TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);CloseHandle(process.hProcess);}} owned{child};
    struct Find {DWORD pid;HWND window=nullptr;} find{child.dwProcessId};
    for(unsigned tries=0;tries<100&&!find.window;++tries){EnumWindows([](HWND w,LPARAM arg)->BOOL{auto& f=*reinterpret_cast<Find*>(arg);DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==f.pid&&IsWindowVisible(w)){f.window=w;return FALSE;}return TRUE;},LPARAM(&find));Sleep(20);}
    const auto window=find.window;owned.window=window;if(!window)return false;
    source::ScreenCaptureOptions options;options.target=uint64_t(window);options.fps=60;options.cursor=false;
    source::SourceOpenDesc desc;desc.path=options.uri();desc.d3d12Device=ctx.device();desc.d3d12Queue=ctx.directQueue();
    source::ScreenCaptureSource source;bool pass=source.open(desc);if(!pass)std::wcerr<<source.status()<<std::endl;
    for(unsigned phase=0;phase<3&&pass;++phase){
        PostMessageW(window,WM_APP+1,phase==1,0);checker.invalidate();const auto before=source.metrics();unsigned frames=0,duplicates=0,waiting=0;int64_t lastPts=-1;
        std::ofstream csv(out/(L"capture-"+std::to_wstring(phase)+L".csv"));csv<<"sequence,pts100ns,first,duplicate,gpuCompareMs,cpuFenceWaitUs,totalCpuUs\n";
        const auto started=GetTickCount64();uint64_t moved=0;
        while(GetTickCount64()-started<60000&&pass){
            pump();const auto elapsed=GetTickCount64()-started;
            if(phase==2&&elapsed/100!=moved){moved=elapsed/100;SetWindowPos(window,nullptr,40+int(moved%15)*4,40+int(moved%9)*3,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
            pipeline::FramePacket packet;const AVFrame* frame=nullptr;const auto read=source.read(packet,&frame);
            if(read==source::SourceReadStatus::Waiting){++waiting;Sleep(1);continue;}
            if(read!=source::SourceReadStatus::Frame){std::wcerr<<source.status()<<std::endl;pass=false;break;}
            if(!packet.hardwareSurface.present()||packet.pts.to100ns()<=lastPts){pass=false;break;}lastPts=packet.pts.to100ns();
            if(pipeline::breaksHistory(packet.flags))checker.invalidate();
            if(packet.hardwareSurface.waitFence&&FAILED(ctx.directQueue()->Wait(packet.hardwareSurface.waitFence,packet.hardwareSurface.waitValue))){pass=false;break;}
            dupperf::ExactChecker::Result result;
            pass=checker.check(packet.hardwareSurface.texture,D3D12_RESOURCE_STATE_COMMON,packet.colorInfo.isHdrPath(),result);if(!pass)break;
            ++frames;duplicates+=result.duplicate;csv<<packet.sequence<<','<<lastPts<<','<<result.first<<','<<result.duplicate<<','<<result.gpuCompareMs<<','<<result.cpuFenceWaitUs<<','<<result.totalCpuUs<<'\n';
        }
        const auto metrics=source.metrics();std::cout<<"CAPTURE phase="<<phase<<" durationMs="<<GetTickCount64()-started<<" frames="<<frames<<" duplicates="<<duplicates<<" waitingPolls="<<waiting<<" received="<<metrics.received-before.received<<" dropped="<<metrics.dropped-before.dropped<<" pass="<<pass<<std::endl;
        pass=pass&&frames>0;csv.flush();
    }
    ring.drainQueue();source.close();CoUninitialize();return pass;
}
}
int wmain(int argc,wchar_t** argv){
    using namespace veyra;
    if(argc==2&&std::wstring(argv[1])==L"--target"){
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{};wc.lpfnWndProc=pattern;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraDupPerfOwnedPattern";if(!RegisterClassW(&wc))return 2;
        auto window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Veyra own-pattern duplicate measurement",WS_OVERLAPPEDWINDOW,40,40,660,420,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)return 2;ShowWindow(window,SW_SHOWNOACTIVATE);SetTimer(window,1,16,nullptr);const auto started=GetTickCount64();
        while(GetTickCount64()-started<240000&&IsWindow(window)){pump();Sleep(1);}if(IsWindow(window))DestroyWindow(window);return 0;
    }
    if(argc!=4)return 2;const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;Status status;
    gfx::DeviceContextDesc dd;dd.enableDebugLayer=true;if(!ctx.initialize(dd,status)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status))return 2;
    dupperf::ExactChecker checker(ctx,ring);if(!checker.initialize(argv[1]))return 2;
    if(std::wstring(argv[3])==L"capture")check(capture(ctx,ring,checker,out),"actual WGC own-window 3x60 seconds");
    else {
        for(bool half:{false,true}){
            auto a=upload(ctx,ring,19,17,half,0),rgb=upload(ctx,ring,19,17,half,1),alpha=upload(ctx,ring,19,17,half,2);
            dupperf::ExactChecker::Result r;checker.invalidate();check(checker.check(a.Get(),D3D12_RESOURCE_STATE_COMMON,half,r)&&r.first&&!r.duplicate,"first accepted");
            check(checker.check(a.Get(),D3D12_RESOURCE_STATE_COMMON,half,r)&&!r.first&&r.duplicate,"exact same pixels");
            check(checker.check(alpha.Get(),D3D12_RESOURCE_STATE_COMMON,half,r)&&r.duplicate!=half,"SDR RGB versus FP16 RGBA alpha semantics");
            check(checker.check(rgb.Get(),D3D12_RESOURCE_STATE_COMMON,half,r)&&!r.duplicate,"last odd-size pixel single LSB");
            check(checker.check(rgb.Get(),D3D12_RESOURCE_STATE_COMMON,half,r)&&r.duplicate,"accepted source history");
            checker.invalidate();check(checker.check(rgb.Get(),D3D12_RESOURCE_STATE_COMMON,half,r)&&r.first,"epoch invalidation");
        }
        std::ofstream csv(out/L"compare.csv");csv<<"width,height,changing,frame,gpuCompareMs,cpuFenceWaitUs,totalCpuUs,historyCopyCpuUs,duplicate\n";
        for(unsigned w:{1920u,3840u}){
            const unsigned h=w*9/16;auto a=upload(ctx,ring,w,h,false,0),b=upload(ctx,ring,w,h,false,1);
            for(bool changing:{false,true}){checker.invalidate();for(unsigned i=0;i<300;++i){
                dupperf::ExactChecker::Result r;const bool ok=checker.check(changing&&i%2?b.Get():a.Get(),D3D12_RESOURCE_STATE_COMMON,false,r);
                if(!ok||(i>0&&r.duplicate==changing)){check(false,"300-frame compare sequence");break;}
                csv<<w<<','<<h<<','<<changing<<','<<i<<','<<r.gpuCompareMs<<','<<r.cpuFenceWaitUs<<','<<r.totalCpuUs<<','<<r.historyCopyCpuUs<<','<<r.duplicate<<'\n';
            }}
            std::cout<<"TIMING width="<<w<<" pass="<<(failures==0)<<std::endl;
        }
    }
    ring.drainQueue();Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T size=0;debug->GetMessage(i,nullptr,&size);std::vector<unsigned char> data(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(FAILED(debug->GetMessage(i,message,&size)))return 2;if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<message->pDescription<<std::endl;}}
    const auto removed=ctx.device()->GetDeviceRemovedReason();std::cout<<"RESULT failures="<<failures<<" debugErrors="<<errors<<" deviceRemoved="<<unsigned(removed)<<std::endl;
    return failures==0&&errors==0&&SUCCEEDED(removed)?0:1;
}
