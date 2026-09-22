// Real-hardware test of the product capture facade, not a second capture path.
#include "veyra/source/CaptureCardSource.h"
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
extern "C" {
#include <libavutil/frame.h>
}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto devices=veyra::source::CaptureCardSource::deviceDetails();
    for(unsigned i=0;i<devices.size();++i)wprintf(L"device %u %ls path=%ls\n",i,devices[i].name.c_str(),devices[i].path.c_str());
    if(argc<2){CoUninitialize();return 0;}
    const bool withAudio=argc>3&&std::wstring_view(argv[3])==L"--audio";
    std::wstring target=argv[1];auto it=std::find_if(devices.begin(),devices.end(),[&](const auto& d){return d.path==target;});
    if(it==devices.end()){puts("FAIL requested native device missing");return 2;}
    veyra::source::CaptureCardSource source;veyra::source::SourceOpenDesc desc;
    desc.path=veyra::source::CaptureCardSource::makeCapturePath(unsigned(it-devices.begin()),*it,0,withAudio?veyra::source::kCaptureAudioFromVideoDevice:veyra::source::kCaptureAudioDisabled,nullptr);
    if(!source.configure(desc)){wprintf(L"FAIL configure: %ls\n",source.errorMessage().c_str());return 3;}
    auto info=source.info();printf("input %ux%u %d/%d\n",info.width,info.height,info.nominalRateNum,info.nominalRateDen);
    if(source.metrics().received||source.info().opened){puts("FAIL premature capture");return 4;}
    if(!source.start()){wprintf(L"FAIL start: %ls\n",source.errorMessage().c_str());return 5;}
    const auto begin=std::chrono::steady_clock::now();auto deadline=begin+std::chrono::seconds(12);
    unsigned frames=0;int64_t prior=-1;uint64_t sequence=0;unsigned minimum=255,maximum=0;bool dumped=false;
    const AVFrame* frame=nullptr;veyra::pipeline::FramePacket packet;
    while(std::chrono::steady_clock::now()<deadline){
        auto r=source.read(packet,&frame);if(r==veyra::source::SourceReadStatus::Error){puts("FAIL capture read");return 6;}if(r!=veyra::source::SourceReadStatus::Frame)continue;
        if(packet.pts.to100ns()<=prior||packet.sequence<=sequence){puts("FAIL nonmonotonic timestamp/sequence");return 7;}prior=packet.pts.to100ns();sequence=packet.sequence;++frames;
        if(!dumped){
            for(unsigned y=0;y<info.height;++y)for(unsigned x=1;x<info.width*2;x+=2){auto v=frame->data[0][y*frame->linesize[0]+x];minimum=std::min(minimum,unsigned(v));maximum=std::max(maximum,unsigned(v));}
            if(argc>2){std::ofstream out(std::filesystem::path(argv[2]),std::ios::binary);for(unsigned y=0;y<info.height;++y)out.write(reinterpret_cast<const char*>(frame->data[0]+y*frame->linesize[0]),info.width*2);if(!out){puts("FAIL frame dump");return 8;}}
            dumped=true;
        }
    }
    auto m=source.metrics();auto audio=source.audioState();source.close();
    if(withAudio){printf("audio blocks=%llu peak=%.6f running=%d underruns=%llu error=%d\n",audio.inputBlocks,audio.inputPeak,audio.running,audio.underruns,!audio.error.empty());if(!audio.running||audio.inputBlocks<100||!audio.error.empty()){puts("FAIL embedded PCM capture");return 10;}}printf("frames=%u received=%llu dropped=%llu callbackFps=%.3f lumaMin=%u lumaMax=%u\n",frames,m.received,m.dropped,m.callbackFps,minimum,maximum);
    bool ok=frames>=unsigned(info.averageFps*10)&&maximum>minimum+16&&m.callbackFps>info.averageFps*.9;
    puts(ok?"PASS native AJA product capture":"FAIL native AJA product capture");CoUninitialize();return ok?0:9;
}
