// Tests the product GPU output, transport and lifetime, with independent CPU
// colour references. Fixture uploads/readback are restricted to this test.
#include "veyra/engine/VirtualCamera.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "shared-memory-queue.h"
#include "tiny-nv12-scale.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace veyra;
using Microsoft::WRL::ComPtr;
static unsigned checks=0;
static void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static double pq(double x){const auto p=std::pow(x*.008,2610./16384);return std::pow((3424./4096+(2413./128)*p)/(1+(2392./128)*p),2523./32);}
static double proxy(double x){const auto c=std::max(x*80/203,0.);const auto v=c<=.75?c:.75+.25*(1-std::exp(-5.778*(c-.75)));return v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;}
static std::array<double,3> yuv(std::array<double,3> c){const auto y=c[0]*.2126+c[1]*.7152+c[2]*.0722;return {16+219*y,128+224*(c[2]-y)/1.8556,128+224*(c[0]-y)/1.5748};}
static void run(){
    const auto suffix=L"gpu-"+std::to_wstring(GetCurrentProcessId());SetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_NAMESPACE",suffix.c_str());
    gfx::D3D12DeviceContext ctx;Status status{};gfx::DeviceContextDesc desc;desc.enableDebugLayer=true;
    require(ctx.initialize(desc,status),"D3D12 initialize");
    gfx::CommandSlotRing ring;require(ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),4,status),"ring initialize");
    ComPtr<ID3D12InfoQueue> debug;ctx.device()->QueryInterface(IID_PPV_ARGS(&debug));if(debug)debug->ClearStoredMessages();
    {
        engine::VirtualCamera camera;std::wstring error;require(!camera.status().active,"default off");
        require(camera.start(120,error),"camera start with packaged modules");
        for(unsigned mode=0;mode<3;++mode)for(unsigned scaled=0;scaled<3;++scaled){
            const unsigned w=scaled?32:647,h=scaled?18:363,nativeW=scaled?64:w,nativeH=scaled?36:h,outW=(nativeW+1)&~1u,outH=(nativeH+1)&~1u;
            camera.sessionEnded();
            std::vector<float> pixels(size_t(w)*h*4);
            const std::array<std::array<double,3>,6> sdr{{{1,0,0},{0,1,0},{0,0,1},{.375,.375,.375},{0,0,0},{1,1,1}}};
            const std::array<double,6> hdr{-1,0,.2,1,203./80,1000./80};
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const unsigned stripe=std::min(5u,x*6/w);auto c=mode?std::array<double,3>{hdr[stripe],hdr[stripe],hdr[stripe]}:sdr[stripe];
                if(mode==2)for(auto& v:c)v=pq(std::max(v,0.));for(unsigned k=0;k<3;++k)pixels[(size_t(y)*w+x)*4+k]=float(c[k]);pixels[(size_t(y)*w+x)*4+3]=1;}
            auto texture=pipeline::makeTexture(ctx.device(),w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,false);require(bool(texture),"fixture texture");
            const unsigned pitch=(w*16+255)&~255u;ComPtr<ID3D12Resource> upload;
            D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=uint64_t(pitch)*h;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            require(SUCCEEDED(ctx.device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload))),"fixture upload");
            void* mapped=nullptr;require(SUCCEEDED(upload->Map(0,nullptr,&mapped)),"upload map");for(unsigned y=0;y<h;++y)memcpy(static_cast<unsigned char*>(mapped)+size_t(y)*pitch,pixels.data()+size_t(y)*w*4,w*16);upload->Unmap(0,nullptr);
            unsigned slot=0;auto* list=ring.acquireNext(slot,status);require(list,"fixture command list");
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,w,h,1,pitch};to.pResource=texture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
            require(ring.submitAndSignal(slot),"upload submit");
            engine::PresentCapture::Frame frame{texture.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,w,h,mode,0,false,nativeW,nativeH,scaled==2};
            // Exercise discarded command lists as well as the normal path.
            if(mode==0&&scaled==0){list=ring.acquireNext(slot,status);require(camera.record(list,frame),"discard record");camera.discarded();require(ring.discardRecording(),"discard list");Sleep(12);}
            list=ring.acquireNext(slot,status);require(camera.record(list,frame),"camera GPU record");require(ring.submitAndSignal(slot),"camera submit");camera.submitted(ctx.fence(),ring.lastSignaledValue());
            video_queue_t* queue=nullptr;for(unsigned i=0;i<500;++i){queue=video_queue_open();if(queue&&video_queue_state(queue)==SHARED_QUEUE_STATE_READY)break;video_queue_close(queue);queue=nullptr;Sleep(5);}
            require(queue,"GPU output became ready");uint32_t qw=0,qh=0;uint64_t interval=0;video_queue_get_info(queue,&qw,&qh,&interval);require(qw==outW&&qh==outH,"output size and odd padding");
            std::vector<uint8_t> output(size_t(qw)*qh*3/2);nv12_scale_t scale{};nv12_scale_init(&scale,TARGET_FORMAT_NV12,qw,qh,qw,qh);uint64_t timestamp=0;bool read=false;
            for(unsigned i=0;i<50&&!read;++i){read=video_queue_read(queue,&scale,output.data(),&timestamp);if(!read)Sleep(2);}require(read&&timestamp>0,"actual GPU NV12 read");
            double worst=0;
            for(unsigned stripe=0;stripe<6;++stripe){const unsigned x=std::min(qw-2,unsigned((stripe+.5)*nativeW/6))&~1u,y=(qh/2)&~1u;auto c=mode?std::array<double,3>{proxy(hdr[stripe]),proxy(hdr[stripe]),proxy(hdr[stripe])}:sdr[stripe];const auto expected=yuv(c);const size_t at=size_t(y)*qw+x,uv=size_t(qw)*qh+size_t(y/2)*qw+x;
                for(unsigned k=0;k<3;++k){const auto actual=output[k==0?at:uv+k-1];worst=std::max(worst,std::abs(actual-expected[k]));}}
            require(worst<=2.0,"independent SDR/scRGB/PQ colour reference");
            if(!scaled){require(output[qw-1]==output[qw-2],"odd last column replicated");require(output[size_t(qh-1)*qw]==output[size_t(qh-2)*qw],"odd last row replicated");}
            require(camera.status().processingWidth==w&&camera.status().width==outW,"processing and output extents distinguished");
            camera.stop();require(video_queue_state(queue)==SHARED_QUEUE_STATE_STOPPING||!camera.status().active,"camera stop");video_queue_close(queue);
            for(unsigned i=0;i<100&&camera.status().width;++i)Sleep(5);
            require(camera.start(120,error),"restart");require(ring.waitIdle(),"fixture complete");
            std::printf("PASS mode=%u scaled=%u output=%ux%u maxError=%.4f\n",mode,scaled,outW,outH,worst);
            Sleep(12);
        }
        // A 59.94 Hz capture source and a 60 FPS camera have slightly different
        // clocks. Sub-ms arrival jitter must not discard fresh capture frames.
        camera.stop();require(ring.waitIdle(),"cadence setup idle");
        require(camera.start(60,error),"60 FPS cadence start");Sleep(35);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=640;td.Height=360;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> moving;require(SUCCEEDED(ctx.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&moving))),"dynamic fixture texture");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        ComPtr<ID3D12DescriptorHeap> rtvs;require(SUCCEEDED(ctx.device()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs))),"dynamic fixture RTV");
        const auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();ctx.device()->CreateRenderTargetView(moving.Get(),nullptr,rtv);
        engine::PresentCapture::Frame movingFrame{moving.Get(),td.Format,640,360,0,0,false,640,360,false};
        const auto emit=[&](unsigned id){unsigned slot=0;auto* list=ring.acquireNext(slot,status);require(list,"dynamic command list");
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={moving.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET};list->ResourceBarrier(1,&b);
            const float gray=float(24+id)/219.f,clear[]{gray,gray,gray,1};list->ClearRenderTargetView(rtv,clear,0,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
            const bool accepted=camera.record(list,movingFrame);require(ring.submitAndSignal(slot),"dynamic submit");if(accepted)camera.submitted(ctx.fence(),ring.lastSignaledValue());return accepted;};
        require(emit(0),"dynamic first frame");
        video_queue_t* movingQueue=nullptr;for(unsigned i=0;i<500;++i){movingQueue=video_queue_open();if(movingQueue&&video_queue_state(movingQueue)==SHARED_QUEUE_STATE_READY)break;video_queue_close(movingQueue);movingQueue=nullptr;Sleep(2);}require(movingQueue,"dynamic queue ready");
        // Sleep(1)/sleep_until alone can have 15.6 ms granularity on Windows,
        // which would test the fixture's burstiness instead of sub-ms jitter.
        HANDLE pacing=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS),reading=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        require(pacing&&reading,"high-resolution fixture timers");LARGE_INTEGER firstTick{};firstTick.QuadPart=-10000;require(SetWaitableTimer(reading,&firstTick,1,nullptr,nullptr,FALSE),"consumer fixture timer");
        std::atomic<unsigned> uniqueFrames=0;
        std::jthread reader([&](std::stop_token stop){std::vector<uint8_t> bytes(640*360*3/2);nv12_scale_t scale{};nv12_scale_init(&scale,TARGET_FORMAT_NV12,640,360,640,360);uint64_t ts=0;int previous=-1;
            while(!stop.stop_requested()){if(video_queue_read(movingQueue,&scale,bytes.data(),&ts)&&bytes[0]!=previous){previous=bytes[0];++uniqueFrames;}WaitForSingleObject(reading,10);}});
        const auto origin=std::chrono::steady_clock::now()+std::chrono::milliseconds(40);unsigned accepted=0;
        const auto before=camera.status().frames;
        for(unsigned i=1;i<=120;++i){const auto due=origin+std::chrono::nanoseconds(int64_t(i)*1001000000/60+(i%2?750000:-750000));
            const auto left=std::chrono::duration_cast<std::chrono::nanoseconds>(due-std::chrono::steady_clock::now()).count();if(left>300000){LARGE_INTEGER wait{};wait.QuadPart=-(left-300000)/100;require(SetWaitableTimer(pacing,&wait,0,nullptr,nullptr,FALSE),"source fixture timer");WaitForSingleObject(pacing,100);}while(std::chrono::steady_clock::now()<due)YieldProcessor();accepted+=emit(i);}
        require(ring.waitIdle(),"dynamic GPU complete");for(unsigned i=0;i<100&&camera.status().frames-before<accepted;++i)Sleep(2);Sleep(8);
        reader.request_stop();reader.join();CloseHandle(pacing);CloseHandle(reading);video_queue_close(movingQueue);
        const auto published=camera.status().frames-before;
        std::printf("CADENCE source=60000/1001 camera=60 jitter=+/-0.75ms input=120 accepted=%u published=%llu uniqueIncludingInitial=%u dropped=%llu\n",accepted,static_cast<unsigned long long>(published),uniqueFrames.load(),static_cast<unsigned long long>(camera.status().dropped));
        require(accepted>=117,"60 Hz jitter preserves fresh frames");require(published==accepted,"accepted dynamic frames published");require(uniqueFrames>=accepted,"dynamic NV12 frames actually change");
        camera.stop();require(ring.drainQueue(),"final GPU drain");
    }
    unsigned errors=0;if(debug)for(UINT64 i=0;i<debug->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());debug->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::printf("D3D12 ERROR %s\n",m->pDescription);}}
    require(errors==0,"D3D12 debug errors");std::printf("PASS checks=%u debugErrors=%u\n",checks,errors);ring.shutdown();
}
int main(){try{run();return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s after %u checks\n",e.what(),checks);return 2;}}
