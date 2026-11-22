#include "veyra/source/MediaFileSource.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <cstring>
#include <libavutil/hwcontext_d3d12va.h>
#include "veyra/Log.h"
#include "veyra/engine/EngineController.h"
#include "veyra/media/FFmpegDeinterlacer.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <d3d12sdklayers.h>
extern "C" {
#include <libavutil/hwcontext.h>
}
using namespace veyra;
// Test-only plane readback on the DIRECT queue. The upstream FFmpeg COPY
// helper uses a mismatched barrier layout; product playback never uses either.
bool readLuma(gfx::D3D12DeviceContext& device,gfx::CommandSlotRing& ring,const AVFrame* frame,AVFrame* output){
    const auto* native=reinterpret_cast<const AVD3D12VAFrame*>(frame->data[0]);
    if(!native||!native->texture)return false;
    const auto desc=native->texture->GetDesc();if(desc.Format!=DXGI_FORMAT_NV12)return false;
    if(native->sync_ctx.fence&&FAILED(device.directQueue()->Wait(native->sync_ctx.fence,native->sync_ctx.fence_value)))return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
    device.device()->GetCopyableFootprints(&desc,native->subresource_index,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=1;
    buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    gfx::ComPtr<ID3D12Resource> download;
    if(FAILED(device.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&download))))return false;
    uint32_t slot=0;Status status;auto* list=ring.acquireNext(slot,status);if(!list)return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={native->texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION source{},target{};source.pResource=native->texture;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;source.SubresourceIndex=native->subresource_index;
    target.pResource=download.Get();target.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;target.PlacedFootprint=footprint;
    list->CopyTextureRegion(&target,0,0,0,&source,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!ring.submitAndSignal(slot)||!ring.waitIdle())return false;
    output->format=AV_PIX_FMT_GRAY8;output->width=frame->width;output->height=frame->height;
    if(av_frame_get_buffer(output,32)<0)return false;
    void* data=nullptr;D3D12_RANGE read{0,SIZE_T(bytes)};if(FAILED(download->Map(0,&read,&data)))return false;
    for(int y=0;y<frame->height;++y)std::memcpy(output->data[0]+ptrdiff_t(y)*output->linesize[0],static_cast<const uint8_t*>(data)+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,frame->width);
    D3D12_RANGE written{};download->Unmap(0,&written);return true;
}
int enginePlayback(const wchar_t* path,const wchar_t* directory,const wchar_t* effects){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const std::filesystem::path out=directory;std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"player.log").wstring());Logger::instance().setConsoleEnabled(false);
    HWND window=CreateWindowExW(0,L"STATIC",L"Network and deinterlace playback regression",WS_POPUP,0,0,960,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 2;ShowWindow(window,SW_SHOWNOACTIVATE);
    engine::EngineController player;player.setVolume(0,true);
    engine::PlayerOptions options;options.nr=std::wstring_view(effects)==L"nr-xess";options.fg=options.nr;options.fgMultiplier=2;
    options.settings.nrRuntime=engine::NrRuntime::NvidiaOriginal;options.settings.nrPolicy=pipeline::NrSizePolicy::Realtime;
    options.settings.frameGenerationBackend=engine::FrameGenerationBackend::XeSS;
    player.open(window,path,options);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
    uint64_t frames=0,generated=0;bool audio=false,failed=false,nr=false,fg=false,playing=false;
    while(std::chrono::steady_clock::now()<deadline){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        const auto s=player.snapshot();failed|=s.failed;audio|=s.audioAvailable;nr|=s.nrActive;fg|=s.fgActive;
        frames=std::max(frames,s.metrics.flow.counters.realPresented);generated=std::max({generated,s.metrics.flow.counters.generatedPresented,s.metrics.flow.counters.xessSdkGenerated});
        playing|=s.transport==engine::TransportState::Playing;
        if(failed||s.transport==engine::TransportState::Ended||frames>=100)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto stopAt=std::chrono::steady_clock::now();player.stop();
    const auto stopMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stopAt).count();
    DestroyWindow(window);
    std::cout<<"ENGINE frames="<<frames<<" generated="<<generated<<" audio="<<audio<<" nr="<<nr<<" fg="<<fg<<" failed="<<failed<<" playing="<<playing<<" stopMs="<<stopMs<<std::endl;
    return !failed&&playing&&frames>10&&stopMs<2500&&(!options.nr||(nr&&fg&&generated>0))?0:1;
}
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring_view(argv[1])==L"--field-duration"){
        media::FFmpegDeinterlacer filter;
        AVFrame* input=av_frame_alloc();
        if(!input)return 2;
        input->width=72;input->height=48;input->format=AV_PIX_FMT_YUV420P;
        input->flags=AV_FRAME_FLAG_INTERLACED|AV_FRAME_FLAG_TOP_FIELD_FIRST;
        input->duration=1800; // 20 ms metadata for each of two 40 ms decoded picture intervals.
        bool ok=av_frame_get_buffer(input,32)>=0;
        for(unsigned plane=0;ok&&plane<3;++plane)
            memset(input->data[plane],plane==0?96:128,size_t(input->linesize[plane])*(plane==0?48:24));
        ok=ok&&filter.open(input,nullptr,1,90000,50,1);
        int n=0;int64_t firstPts=0;const AVFrame* output=nullptr;
        const auto receive=[&]{
            while(filter.receive(&output)>=0){
                if(!n)firstPts=output->pts;
                const auto offset=(output->pts-firstPts)*double(filter.timeBaseNum())/filter.timeBaseDen();
                ok=ok&&std::abs(offset-n*.02)<.0001;
                ++n;
            }
        };
        for(int i=0;ok&&i<8;++i){input->pts=3600*i;ok=filter.push(input)>=0;receive();}
        if(ok){ok=filter.push(nullptr)>=0;receive();}
        av_frame_free(&input);
        std::cout<<"Field cadence fixture outputs="<<n<<" picture_fps="<<filter.observedPictureRate()<<'\n';
        return ok&&n==16&&std::abs(filter.observedPictureRate()-25)<.0001?0:1;
    }
    if(argc<3)return 2;
    if(std::wstring_view(argv[2])==L"engine")return argc>=4?enginePlayback(argv[1],argv[3],argc>=5?argv[4]:L"off"):2;
    std::atomic<bool> cancel=false;
    source::SourceOpenDesc desc;desc.path=argv[1];desc.cancelled=&cancel;
    if(std::wstring_view(argv[2])==L"cancel"){
        desc.preferHardwareDecode=false;
        std::jthread stop([&]{std::this_thread::sleep_for(std::chrono::milliseconds(200));cancel=true;});
        source::MediaFileSource input;const auto start=std::chrono::steady_clock::now();const bool opened=input.open(desc);
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"CANCEL opened="<<opened<<" milliseconds="<<ms<<std::endl;return !opened&&ms<2500?0:1;
    }
    if(argc<7)return 2;
    desc.preferHardwareDecode=std::wstring_view(argv[2])==L"hardware";
    desc.requireHardwareDecode=desc.preferHardwareDecode;
    desc.deinterlace=std::wstring_view(argv[3])==L"on";
    const int expected=_wtoi(argv[4]);const double fps=_wtof(argv[5]);
    const std::filesystem::path out=argv[6];std::filesystem::create_directories(out);
    Logger::instance().openFile((out/L"source.log").wstring());Logger::instance().setConsoleEnabled(false);
    gfx::D3D12DeviceContext device;Status status=Status::Ok;
    if(desc.preferHardwareDecode){
        gfx::DeviceContextDesc options;options.enableDebugLayer=true;
        if(!device.initialize(options,status))return 3;
        desc.d3d12Device=device.device();desc.d3d12Queue=device.directQueue();desc.d3d12AdapterLuid=device.adapter().luid;
    }
    gfx::CommandSlotRing readback;
    if(desc.preferHardwareDecode&&!readback.initialize(device.device(),device.directQueue(),device.fence(),device.fenceEvent(),4,status))return 3;
    source::MediaFileSource input;
    if(!input.open(desc)){std::wcerr<<L"OPEN FAILED "<<input.errorMessage()<<std::endl;return 1;}
    int failures=0;auto check=[&](bool ok,const char* name){failures+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;};
    for(int pass=0;pass<2;++pass){
        if(pass){
            if(media::networkMediaInput(desc.path))break;
            check(input.seek({0,1}),"seek after EOF resets decoder and filter");
        }
        unsigned count=0,pixelSamples=0,networkClockChanges=0;double last=-1e30,first=-1,comb=0;bool chronological=true,progressive=true;
        const AVFrame* decoded=nullptr;pipeline::FramePacket packet;source::SourceReadStatus result;
        while((result=input.read(packet,&decoded))==source::SourceReadStatus::Frame){
            check(decoded!=nullptr,"decoded field exists");if(!decoded)break;
            const double seconds=packet.pts.toDouble();
            if(count==0)first=seconds;
            const bool clockReset=(packet.flags&uint32_t(pipeline::FrameFlagBits::Discontinuity))!=0;
            chronological=chronological&&!packet.pts.isUnknown()&&(seconds>last||(media::networkMediaInput(desc.path)&&clockReset));
            if(count&&fps>0){
                const bool regular=std::abs((seconds-last)-1/fps)<0.0001;
                // RTSP's RTP/RTCP clock may legitimately rebase once sender
                // reports arrive. Preserve its PTS and demand reset on reversal.
                if(media::networkMediaInput(desc.path))networkClockChanges+=!regular;
                else check(regular,"field PTS has requested real cadence");
            }
            if(pass&&count==0)check(packet.flags&uint32_t(pipeline::FrameFlagBits::Seek),"first post-seek field carries seek reset");
            last=seconds;progressive=progressive&&!(decoded->flags&AV_FRAME_FLAG_INTERLACED);
            AVFrame* downloaded=nullptr;const AVFrame* pixels=decoded;
            if(decoded->format==AV_PIX_FMT_D3D12||decoded->format==AV_PIX_FMT_D3D11){
                downloaded=av_frame_alloc();
                const bool read=downloaded&&(decoded->format==AV_PIX_FMT_D3D12
                    ?readLuma(device,readback,decoded,downloaded):av_hwframe_transfer_data(downloaded,decoded,0)>=0);
                check(read,"diagnostic GPU readback succeeds");pixels=read?downloaded:nullptr;
            }
            if(pixels&&pixels->data[0]){
                ++pixelSamples;
                double energy=0;
                for(int y=1;y<decoded->height-1;++y)for(int x=0;x<decoded->width;++x){
                    const int a=pixels->data[0][ptrdiff_t(y-1)*pixels->linesize[0]+x];
                    const int b=pixels->data[0][ptrdiff_t(y)*pixels->linesize[0]+x];
                    const int c=pixels->data[0][ptrdiff_t(y+1)*pixels->linesize[0]+x];
                    energy+=std::abs(a-2*b+c);
                }
                comb+=energy/double(decoded->width*(decoded->height-2));
                if(pass==0&&count<4){
                    std::ofstream image(out/(L"field-"+std::to_wstring(count)+L".pgm"),std::ios::binary);
                    image<<"P5\n"<<decoded->width<<" "<<decoded->height<<"\n255\n";
                    for(int y=0;y<decoded->height;++y)image.write(reinterpret_cast<const char*>(pixels->data[0]+ptrdiff_t(y)*pixels->linesize[0]),decoded->width);
                }
            }
            av_frame_free(&downloaded);++count;
            if(expected<0&&count>=120)break;
            if(count>1000){check(false,"output remains bounded");break;}
        }
        if(expected>=0){check(result==source::SourceReadStatus::Eos,"all tail fields drain to EOF");check(int(count)==expected,"exact field/frame count including EOF");}
        check(count>0&&chronological,"PTS strictly increases across every field");
        if(desc.deinterlace)check(count>0&&progressive,"interlaced fields are delivered as progressive frames");
        if(fps>0)check(std::abs(input.info().averageFps-fps)<0.01,"source metadata reports actual progressive rate");
        std::cout<<"RESULT pass="<<pass<<" count="<<count<<" first="<<first<<" last="<<last<<" comb="<<(pixelSamples?comb/pixelSamples:-1)<<" networkClockChanges="<<networkClockChanges<<" pixelSamples="<<pixelSamples<<" deinterlace="<<input.info().deinterlacePath<<" hardware="<<input.info().hardwareDecodeActive<<" failures="<<failures<<std::endl;
        if(result==source::SourceReadStatus::Error)std::wcerr<<input.errorMessage()<<std::endl;
    }
    input.close();
    if(device.device()){
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
        if(SUCCEEDED(device.device()->QueryInterface(IID_PPV_ARGS(&queue)))){
            for(UINT64 n=0;n<queue->GetNumStoredMessages();++n){
                SIZE_T size=0;queue->GetMessage(n,nullptr,&size);std::vector<uint8_t> bytes(size);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                if(SUCCEEDED(queue->GetMessage(n,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                    ++failures;std::cout<<"GPU_VALIDATION "<<message->pDescription<<std::endl;
                }
            }
        }
    }
    return failures?1:0;
}
