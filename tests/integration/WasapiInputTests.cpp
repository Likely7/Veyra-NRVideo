#include "veyra/source/WasapiAudioInput.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/sink/WasapiAudioSink.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
using namespace veyra::source;
int wmain(int argc,wchar_t** argv){
    int failures=0,checks=0;auto check=[&](bool ok,const char* label){++checks;printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    if(argc==2&&wcscmp(argv[1],L"--list")==0){for(const auto& d:WasapiAudioInput::devices())wprintf(L"WASAPI %ls ID=%ls\n",d.name.c_str(),d.id.c_str());return 0;}
    if(argc==3&&wcscmp(argv[1],L"--endpoint")==0){
        WasapiAudioInput input;check(input.configure(argv[2]),"explicit endpoint configured");input.setGain(0);input.setSync(2,0);
        check(input.start(),"capture thread started");std::this_thread::sleep_for(std::chrono::seconds(3));
        const auto state=input.snapshot();check(state.running&&state.inputBlocks>20&&state.error.empty(),"real selected input packets reach shared audio session");
        const bool injected=GetEnvironmentVariableW(L"VEYRA_TEST_WASAPI_INPUT_LOSS",nullptr,0)>0;
        check(input.metrics().retries==(injected?1u:0u),"only injected loss reconnects exactly the selected endpoint");
        printf("packets=%llu rate=%u channels=%u peak=%f errors=%zu\n",state.inputBlocks,state.inputSampleRate,state.inputChannels,state.inputPeak,state.error.size());
        input.stop();check(!input.snapshot().running,"stop joins input and output");
        if(injected)SetEnvironmentVariableW(L"VEYRA_TEST_WASAPI_INPUT_LOSS",nullptr);
        check(input.start(),"explicit same endpoint restart");std::this_thread::sleep_for(std::chrono::seconds(1));check(input.snapshot().inputBlocks>10,"restart delivers packets");input.stop();
    }else if(argc==2&&wcscmp(argv[1],L"--invalid")==0){
        WasapiAudioInput input;input.configure(L"veyra-invalid-endpoint-no-fallback");input.setGain(0);check(input.start(),"invalid endpoint begins paced retries");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(13);
        while(input.metrics().retries<4&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto state=input.snapshot();check(!state.error.empty()&&!state.running&&state.inputBlocks==0,"invalid ID never falls back to microphone");input.stop();
        check(input.metrics().retries>=4&&input.metrics().retries<=5,"selected endpoint keeps retrying beyond old limit with bounded backoff");
        check(input.start(),"invalid endpoint can explicitly restart");std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto before=std::chrono::steady_clock::now();input.stop();check(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(500),"stop interrupts reconnect backoff");
    }else if(argc==2&&wcscmp(argv[1],L"--blocked-output")==0){
        const auto outputs=veyra::sink::enumerateRenderEndpoints();
        const auto defaultOutput=std::find_if(outputs.begin(),outputs.end(),[](const auto& e){return e.isDefault;});
        if(defaultOutput==outputs.end()){printf("No system default render endpoint available.\n");return 2;}
        veyra::sink::AudioRenderer renderer;
        check(!renderer.start({},20.0,defaultOutput->id)&&
              renderer.lastError()==HRESULT_FROM_WIN32(ERROR_CIRCULAR_DEPENDENCY),
              "refuse to open the render endpoint used for loopback");
        check(!renderer.started()&&renderer.framesWritten()==0,"no feedback PCM submitted");
    }else if(argc==2&&wcscmp(argv[1],L"--offline")==0){
        WasapiPacketClock clock;clock.reset(48000,50);
        auto a=clock.stamp(0,10000000,480,false,false,2000);check(a.ptsMs==1050&&a.discontinuity&&!a.estimated,"QPC 100ns maps to host axis; first epoch resets");
        auto b=clock.stamp(480,10100000,480,false,false,3000);check(b.ptsMs==1060&&!b.discontinuity,"arrival delay does not change sample PTS");
        auto c=clock.stamp(960,0,480,false,true,4000);check(c.ptsMs==1070&&c.estimated&&!c.discontinuity,"bad timestamp continues frame timeline");
        auto d=clock.stamp(1920,10400000,480,false,false,5000);check(d.discontinuity,"missing device frames reset epoch");
        check(clock.stamp(2400,10500000,480,true,false,5000).discontinuity,"explicit device discontinuity retained");
        check(clock.stamp(2880,10000000,480,false,false,5000).discontinuity,"backwards timestamp resets");
        clock.reset(48000,0);clock.stamp(0,10000000,480,false,false,1000);
        auto bad=clock.stamp(999999,0,480,false,true,3000);check(!bad.discontinuity&&bad.ptsMs==1010,"unreliable device position does not fabricate a gap");
        check(!clock.stamp(960,10200000,480,false,false,3000).discontinuity,"valid timestamp resumes after estimated packet");
        clock.reset(44100,0);auto e=clock.stamp(0,0,441,false,true,1000);check(e.ptsMs==990&&e.estimated&&e.discontinuity,"first invalid timestamp estimated from packet duration");
        CaptureDevice video{L"Video",L"video:path"},audio{L"Audio",L"endpoint:id",false,true};
        auto path=CaptureCardSource::makeCapturePath(0,video,0,0,&audio);check(path.starts_with(L"capture2:")&&path.find(L":-3:")!=std::wstring::npos,"WASAPI provider encoded with explicit stable ID");
        audio.virtualSpeaker=true;
        check(CaptureCardSource::makeCapturePath(0,video,0,0,&audio).find(L":-5:")!=std::wstring::npos,
              "virtual speaker is a separate capture2 transport");
        audio.path.clear();check(CaptureCardSource::makeCapturePath(0,video,0,0,&audio).empty(),"no endpoint ID cannot become default input");
        audio={L"DShow",L"ds:audio"};check(CaptureCardSource::makeCapturePath(0,video,0,0,&audio).find(L":0:")!=std::wstring::npos,"DirectShow connection retained");
        video.path.clear();check(CaptureCardSource::makeCapturePath(0,video,0,-2,nullptr)==L"capture:0:0:-2:0","legacy embedded path retained");
        std::vector<uint8_t> zeros(3840,0);const uint8_t data[]={1,2,3,4};
        check(wasapiPacketData(data,4,false,zeros)==data,"non-silent PCM preserved without modification");
        check(wasapiPacketData(nullptr,3840,true,zeros)==zeros.data()&&zeros.back()==0,"silent null packet uses bounded zero PCM");
        check(!wasapiPacketData(nullptr,4,false,zeros),"null non-silent packet rejected");
        check(!wasapiPacketData(data,3841,false,zeros),"oversized packet rejected");
        check(!wasapiPacketData(data,0,false,zeros),"empty packet rejected");
    }else {printf("Use --offline, --list, --invalid, --blocked-output, or --endpoint <explicit capture ID>. No default recording.\n");return 2;}
    printf("WASAPI_INPUT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
