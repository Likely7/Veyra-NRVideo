#include "NrExperimentalFixture.h"
#include "veyra/Log.h"
#include <atomic>
#include <thread>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include "veyra/gfx/PresentSink.h"

// E2. Foreground Feature18 Evaluate and background independent Feature18
// Create/Evaluate/Release share one device, one queue and one snippet session.
// Each command ring owns a separate fence/event and allocator/list set.
// CLI: runtime-dir output-dir duration-seconds create-count concurrent|serial
int wmain(int argc,wchar_t** argv) {
    using namespace nrperf;
    if(argc!=6&&argc!=7)return 2;
    const bool actualPresent=argc==7&&std::wstring_view(argv[6])==L"present";
    if(argc==7&&!actualPresent)return 2;
    const unsigned seconds=unsigned(_wtoi(argv[3])),count=unsigned(_wtoi(argv[4]));
    const bool concurrent=std::wstring_view(argv[5])==L"concurrent";
    if(!seconds||seconds>240||!count||count>50)return 2;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"engine.log").wstring());
    Session s;if(!s.init(argv[1]))return 2;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter3> adapter;
    if(!hrOK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI.Factory")||
       !hrOK(factory->EnumAdapterByLuid(s.device.device()->GetAdapterLuid(),IID_PPV_ARGS(&adapter)),"DXGI.Adapter"))return 2;
    auto usage=[&](){DXGI_QUERY_VIDEO_MEMORY_INFO info{};return hrOK(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info),"DXGI.Memory")?info.CurrentUsage:UINT64_MAX;};
    const auto before=usage();std::atomic<bool> failed=false;
    unsigned created=0,released=0,evaluated=0,foregroundEvaluated=0;
    double maxForegroundGap=0;uint64_t after=0;
    std::ofstream csv(out/L"background.csv");csv<<"cycle,w,h,createMs,evaluateOK,releaseOK,vramLive,vramReleased\n";
    {
        gfx::PresentSink sink;
        if(actualPresent){gfx::PresentSink::Desc desc;desc.width=1920;desc.height=1080;
            desc.vsync=false;desc.tearing=false;desc.title=L"Veyra owned concurrent NR experiment";Status status;
            if(!sink.initialize(s.device.device(),s.device.directQueue(),desc,status))return 2;}
        std::ofstream presentCsv;if(actualPresent){presentCsv.open(out/L"present.csv");presentCsv<<"sequence,presentCount,returnedMs,intervalMs,evalGpuMs\n";}
        Feature foreground(s),background(s); // Parameter tracking is mutated only on this thread.
        if(!foreground.create(s.ring,1920,1080)||!foreground.textures(s.ring,1920,1080,1920,1080))return 2;
        for(unsigned i=0;i<30;++i)if(!foreground.evaluate(s.ring,1920,1080,i==0))return 2;
        const auto reference=foreground.read(s.ring);
        ComPtr<ID3D12Fence> fence;
        if(!hrOK(s.device.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Background.Fence"))return 2;
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 2;
        gfx::CommandSlotRing backgroundRing;Status status;
        if(!backgroundRing.initialize(s.device.device(),s.device.directQueue(),fence.Get(),event,4,status)){CloseHandle(event);return 2;}
        const double start=nowMs();
        auto worker=[&](){
            for(unsigned i=0;i<count&&!failed.load();++i){
                const auto due=start+double(i)*double(seconds)*1000.0/count;
                if(concurrent)std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double,std::milli>(due))));
                if(failed.load())break;
                const unsigned widths[]={1280,1920,960};const auto w=widths[i%3],h=(w*9/16)&~1u;
                const auto at=nowMs();const bool ok=background.create(backgroundRing,w,h);const auto ms=nowMs()-at;
                if(!ok){failed=true;break;}++created;
                const bool textures=background.textures(backgroundRing,w,h,w,h);
                const bool run=textures&&background.evaluate(backgroundRing,w,h,true);
                evaluated+=run;const auto live=usage();const bool release=background.release();released+=release;
                // Release probe-owned textures after completed work too.
                background.color.Reset();background.output.Reset();background.motion.Reset();background.depth.Reset();
                const auto freed=usage();csv<<i<<','<<w<<','<<h<<','<<ms<<','<<run<<','<<release<<','<<live<<','<<freed<<'\n';csv.flush();
                if(!run||!release){failed=true;break;}
            }
        };
        std::thread thread;
        if(concurrent)thread=std::thread(worker);else worker();
        auto next=nowMs(),previous=next;
        const auto foregroundStart=actualPresent?next:start;
        while(!failed.load()&&nowMs()-foregroundStart<double(seconds)*1000.0){
            if(!foreground.evaluate(s.ring,1920,1080,false)){failed=true;break;}
            if(actualPresent){
                bool closed=false;if(!sink.processMessages(closed)||closed){failed=true;break;}
                Status status;uint32_t slot;auto* list=s.ring.acquireNext(slot,status);
                auto* buffer=sink.currentBackBuffer();
                if(!list||!buffer){failed=true;break;}
                foreground.states.transition(list,foreground.output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
                foreground.states.transition(list,buffer,D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyResource(buffer,foreground.output.Get());
                foreground.states.transition(list,foreground.output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                foreground.states.transition(list,buffer,D3D12_RESOURCE_STATE_PRESENT);
                if(!s.ring.submitAndSignal(slot)||!sink.present(status)){failed=true;break;}
            }
            ++foregroundEvaluated;const auto current=nowMs();maxForegroundGap=std::max(maxForegroundGap,current-previous);previous=current;
            if(actualPresent){
                static double last=0;
                presentCsv<<foregroundEvaluated<<','<<sink.presentCount()<<','<<current<<','<<(last?current-last:0)<<','<<foreground.lastGpuEvaluateMs<<'\n';presentCsv.flush();last=current;
            }
            next+=1000.0/60.0;
            if(next>nowMs())std::this_thread::sleep_for(std::chrono::duration<double,std::milli>(next-nowMs()));
            else next=nowMs();
        }
        if(thread.joinable())thread.join();
        if(actualPresent){s.ring.drainQueue();sink.shutdown();}
        const auto actual=foreground.read(s.ring);
        // Static input history is warmed for 30 frames before the worker.
        // Save actual and reference pixels even if the model keeps changing.
        for(const auto& pair:{std::pair{L"foreground-before.rgba",&reference},std::pair{L"foreground-after.rgba",&actual}}){
            std::ofstream f(out/pair.first,std::ios::binary);f.write(reinterpret_cast<const char*>(pair.second->data()),std::streamsize(pair.second->size()));}
        uint64_t different=0;for(size_t i=0;i<std::min(reference.size(),actual.size());++i)different+=reference[i]!=actual[i];
        std::printf("PIXELS sameSize=%d differentBytes=%llu referenceBytes=%zu\n",reference.size()==actual.size(),different,reference.size());
        if(!foreground.release()||!background.release())failed=true;
        backgroundRing.drainQueue();backgroundRing.shutdown();CloseHandle(event);
    }
    s.ring.drainQueue();after=usage();
    ComPtr<ID3D12InfoQueue> debug;unsigned errors=0;
    if(!hrOK(s.device.device()->QueryInterface(IID_PPV_ARGS(&debug)),"Debug.Query"))return 2;
    for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);
        std::vector<uint8_t> data(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if(!hrOK(debug->GetMessage(i,m,&n),"Debug.Read"))return 2;
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::printf("DEBUG_ERROR %s\n",m->pDescription);}}
    const HRESULT removed=s.device.device()->GetDeviceRemovedReason();
    const bool pass=!failed&&created==count&&released==count&&evaluated==count&&foregroundEvaluated>0&&errors==0&&SUCCEEDED(removed);
    std::printf("RESULT pass=%d requested=%u created=%u released=%u backgroundEvaluated=%u foregroundEvaluated=%u seconds=%u concurrent=%d maxForegroundGapMs=%.6f vramBefore=%llu vramAfter=%llu debugErrors=%u deviceRemoved=0x%08X\n",
        pass,count,created,released,evaluated,foregroundEvaluated,seconds,concurrent,maxForegroundGap,before,after,errors,unsigned(removed));
    return pass?0:1;
}
