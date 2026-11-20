// Optional real-device integration test. Saves no media; validates the product
// source's deferred Run, bounded mailbox, and reader-buffer lifetime.
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/CaptureFrameRate.h"
#include "veyra/sink/HdmiBitstreamOutput.h"
#include "veyra/Log.h"
#include <windows.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}
int wmain(int argc,wchar_t** argv){
    if (argc == 4 && wcscmp(argv[1], L"--bitstream-capture") == 0) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
        int result = 2;
        {
            veyra::Logger::instance().openFile(L"bitstream-capture.log", false);
            veyra::sink::HdmiBitstreamSettings settings;
            settings.enabled = true;
            for (const auto& endpoint : veyra::sink::HdmiBitstreamOutput::endpoints()) {
                if (endpoint.name == argv[3]) settings.endpointId = endpoint.id;
            }
            if (!settings.endpointId.empty()) {
                auto output = std::make_shared<veyra::sink::HdmiBitstreamOutput>();
                output->configure(settings);
                veyra::source::CaptureCardSource capture;
                capture.setHdmiBitstream(output);
                veyra::source::SourceOpenDesc desc;
                desc.path = argv[2];
                if (capture.configure(desc) && capture.start()) {
                    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(65);
                    unsigned active = 0;
                    uint64_t read = 0;
                    const AVFrame* frame = nullptr;
                    veyra::pipeline::FramePacket packet;
                    while (std::chrono::steady_clock::now() < until) {
                        if (capture.read(packet, &frame) == veyra::source::SourceReadStatus::Frame) {
                            ++read;
                            if (output->state().status == veyra::sink::HdmiBitstreamStatus::Active) ++active;
                        }
                    }
                    const auto info = capture.info();
                    printf("Capture %ux%u at %.3f fps, frames read=%llu, frames with bitstream active=%u\n",
                        info.width, info.height, info.averageFps, static_cast<unsigned long long>(read), active);
                    result = read && active ? 0 : 4;
                    capture.close();
                } else {
                    wprintf(L"Capture start failed: %ls\n", capture.errorMessage().c_str());
                    result = 3;
                }
            } else {
                wprintf(L"Endpoint unavailable: %ls\n", argv[3]);
            }
        }
        CoUninitialize();
        return result;
    }
    if(argc==4&&wcscmp(argv[1],L"--rate-test")==0){
        double expected=0;if(!veyra::source::parseCaptureFrameRate(argv[3],expected))return 2;
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        veyra::source::CaptureCardSource source;veyra::source::SourceOpenDesc desc;desc.path=argv[2];
        if(desc.path.starts_with(L"capture:")){
            unsigned device=0;int format=0,audio=0;
            if(swscanf_s(argv[2],L"capture:%u:%d:%d",&device,&format,&audio)!=3||audio!=-1)return 2;
            const auto devices=veyra::source::CaptureCardSource::deviceDetails();if(device>=devices.size())return 2;
            const auto formats=veyra::source::CaptureCardSource::formatsByPath(devices[device].path);
            const auto* selected=veyra::source::selectCaptureFormat(formats,format,L"");
            if(!selected)return 2;
            // An obsolete ordinal must not override the persisted format identity.
            desc.path=veyra::source::CaptureCardSource::makeCapturePath(device,devices[device],999999,audio,nullptr,0,expected,selected->key);
            auto missing=desc;
            missing.path=veyra::source::CaptureCardSource::makeCapturePath(device,devices[device],format,audio,nullptr,0,expected,L"missing-test-format");
            if(source.configure(missing)){printf("FAIL missing format silently accepted\n");return 1;}
        }
        if(!source.configure(desc)){wprintf(L"RATE_REJECTED %ls\n",source.errorMessage().c_str());return 3;}
        if(!source.start())return 4;
        int failures=0;
        for(int pass=0;pass<2;++pass){
            // Check physical callbacks, independent of the consumer's mailbox drops.
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const auto before=source.metrics();const auto begin=std::chrono::steady_clock::now();
            const auto until=begin+std::chrono::seconds(6);
            const AVFrame* frame=nullptr;veyra::pipeline::FramePacket packet;
            double lastPts=-1;unsigned read=0;bool monotonic=true;
            while(std::chrono::steady_clock::now()<until){
                if(source.read(packet,&frame)==veyra::source::SourceReadStatus::Frame){
                    const double pts=packet.pts.toDouble();monotonic&=pts>lastPts;lastPts=pts;++read;
                }
            }
            const auto after=source.metrics();
            const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            const double actual=(after.received-before.received)/seconds;
            const double target=expected>0?expected:source.info().averageFps;
            const bool ok=read>0&&monotonic&&std::abs(actual-target)<std::max(1.0,target*.05);
            printf("%s RATE pass=%d requested=%.6f connected=%.6f callback=%.6f reads=%u monotonic=%d\n",ok?"PASS":"FAIL",pass,expected,source.info().averageFps,actual,read,monotonic);
            if(!ok)++failures;
            if(pass==0&&!source.reconnect(0,0,0)){printf("FAIL reconnect\n");++failures;break;}
        }
        source.close();CoUninitialize();return failures?1:0;
    }
    if(argc==2&&wcscmp(argv[1],L"--list")==0){
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto videos=veyra::source::CaptureCardSource::deviceDetails();auto audios=veyra::source::CaptureCardSource::deviceDetails(true);
        for(unsigned d=0;d<videos.size();++d){wprintf(L"VIDEO %u %ls embeddedAudio=%d path=%ls\n",d,videos[d].name.c_str(),videos[d].hasEmbeddedAudio?1:0,videos[d].path.c_str());const auto formats=videos[d].path.empty()?veyra::source::CaptureCardSource::formats(d):veyra::source::CaptureCardSource::formatsByPath(videos[d].path);for(const auto& f:formats)wprintf(L"FORMAT %u:%d %ls\n",d,f.index,f.label.c_str());}
        for(unsigned a=0;a<audios.size();++a)wprintf(L"AUDIO %u [%ls] %ls path=%ls\n",a,audios[a].wasapi?L"WASAPI":L"DirectShow",audios[a].name.c_str(),audios[a].path.c_str());
        CoUninitialize();return videos.empty()?1:0;
    }
    const bool wasapiTest=argc==3&&wcscmp(argv[1],L"--wasapi")==0;
    const bool outageTest=argc==2&&wcscmp(argv[1],L"--output-outage")==0;
    if(argc!=2&&!wasapiTest){printf("Specify capture:device:format:audio or --wasapi <explicit endpoint ID>; real hardware required\n");return 2;}
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    veyra::source::CaptureCardSource source;veyra::source::SourceOpenDesc d;d.path=argv[1];
    if(outageTest){
        const auto videos=veyra::source::CaptureCardSource::deviceDetails();
        const auto audios=veyra::source::CaptureCardSource::deviceDetails(true);
        const auto video=std::find_if(videos.begin(),videos.end(),[](const auto& v){return v.name==L"USB3 Video";});
        const auto audio=std::find_if(audios.begin(),audios.end(),[](const auto& a){return !a.wasapi&&a.name.find(L"USB3 Digital Audio")!=std::wstring::npos;});
        if(video==videos.end()||audio==audios.end()){printf("FAIL explicit physical USB3 A/V fixture unavailable\n");return 3;}
        d.path=veyra::source::CaptureCardSource::makeCapturePath(unsigned(video-videos.begin()),*video,0,unsigned(audio-audios.begin()),&*audio);
        SetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_ENDPOINT_LOSS",L"1");
        SetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_LONG_OUTAGE",L"1");
    }
    if(wasapiTest){
        const auto videos=veyra::source::CaptureCardSource::deviceDetails();
        const auto video=std::find_if(videos.begin(),videos.end(),[](const auto& v){return v.name==L"USB3 Video";});
        if(video==videos.end()){printf("FAIL explicit USB3 Video fixture unavailable\n");return 3;}
        const veyra::source::CaptureDevice audio{L"Explicit WASAPI test",argv[2],false,true};
        d.path=veyra::source::CaptureCardSource::makeCapturePath(unsigned(video-videos.begin()),*video,0,0,&audio);
    }
    int failures=0;auto check=[&](bool ok,const char* label){printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    if(!source.configure(d)){printf("FAIL configure\n");return 3;}
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    check(!source.info().opened&&source.metrics().received==0,"configure does not start capture/audio");
    if(!source.start()){printf("FAIL start\n");return 4;}
    // Keep the physical regression silent while still exercising the real
    // capture-card audio renderer and its format negotiation.
    source.setAudioGain(0.0f);
    // This source-only test has no presenter to provide video host/PTS
    // anchors. Use the explicit audio-clock mode so the renderer remains
    // running while the test validates actual audio callbacks.
    source.setAudioSync(wasapiTest?0:2,0);
    const AVFrame* frame=nullptr;veyra::pipeline::FramePacket packet;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    auto readFrame=[&](){while(std::chrono::steady_clock::now()<deadline){auto r=source.read(packet,&frame);if(r==veyra::source::SourceReadStatus::Frame){if(wasapiTest)source.videoPresented(packet.pts.toDouble()*1000,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100,packet.arrivalHost100ns);return true;}if(r==veyra::source::SourceReadStatus::Error)return false;}return false;};
    if(!readFrame()){printf("FAIL first frame\n");return 5;}
    if(outageTest){
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(7);
        auto last=std::chrono::steady_clock::now();double maxGapMs=0;
        unsigned frames=0;bool sawOutage=false;uint64_t firstBlocks=0,lastBlocks=0;
        bool monotonic=true;
        while(std::chrono::steady_clock::now()<until){
            source.recoverAudio(0,2,0);
            deadline=until;if(!readFrame())break;
            const auto now=std::chrono::steady_clock::now();
            maxGapMs=std::max(maxGapMs,std::chrono::duration<double,std::milli>(now-last).count());last=now;++frames;
            const auto s=source.audioState();
            if(s.outputRecovering){sawOutage=true;if(!firstBlocks)firstBlocks=s.inputBlocks;}
            monotonic&=s.inputBlocks>=lastBlocks;lastBlocks=s.inputBlocks;
        }
        const auto s=source.audioState();
        check(sawOutage&&s.running&&!s.outputRecovering&&s.error.empty()&&s.endpointRetries>=2,"owned output outage recovers on physical audio input");
        check(monotonic&&firstBlocks&&s.inputBlocks>firstBlocks+350,"input continues without graph/audio-session restart");
        check(frames>=350&&maxGapMs<150,"physical video stays continuous through output outage");
        printf("PHYSICAL_OUTAGE frames=%u maxReadGapMs=%.3f inputBlocks=%llu endpointRetries=%llu monotonic=%d\n",frames,maxGapMs,s.inputBlocks,s.endpointRetries,monotonic);
        source.close();CoUninitialize();return failures?1:0;
    }
    const auto initialPts=packet.pts.toDouble();const auto initialReceived=source.metrics().received;
    const int rowBytes=av_image_get_linesize(AVPixelFormat(frame->format),frame->width,0);
    std::vector<unsigned char> row(frame->data[0],frame->data[0]+rowBytes);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto stalled=source.metrics();
    check(stalled.received>=initialReceived+3,"callbacks continue while reader retains frame");
    check(stalled.dropped>=2,"bounded pending frame overwritten while reader stalls");
    check(memcmp(row.data(),frame->data[0],row.size())==0,"returned frame remains owned and unchanged");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    check(readFrame(),"read after consumer stall");
    auto recovered=source.metrics();
    const bool recoveryDrop=veyra::pipeline::hasFrameFlag(packet.flags,veyra::pipeline::FrameFlagBits::Drop);
    if(wasapiTest){
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<until){deadline=until;if(!readFrame())break;}
    }
    const auto audio=source.audioState();
    check(audio.running&&audio.error.empty()&&audio.inputSampleRate==48000&&audio.inputBlocks>=20,
        "real capture-card audio renderer runs at selected 48 kHz format");
    check(packet.pts.toDouble()>initialPts+.15,"read skips stale frames rather than draining FIFO");
    check(recoveryDrop,"drop invalidates temporal history");
    check(recovered.readAgeMs<100,"latest frame callback age below 100ms");
    printf("received=%llu dropped=%llu callbackFps=%.3f readAgeMs=%.3f\n",recovered.received,recovered.dropped,recovered.callbackFps,recovered.readAgeMs);
    printf("audioBlocks=%llu format=%uHz/%ubit validBits=%u underruns=%llu underrunFrames=%llu peak=%.5f resets=%llu\n",
        audio.inputBlocks,audio.inputSampleRate,audio.inputContainerBits,audio.inputValidBits,audio.underruns,
        audio.underrunFrames,audio.inputPeak,audio.resets);
    source.close();CoUninitialize();return failures?1:0;
}
