#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/source/AudioInputRecovery.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string_view>
int main(int argc,char** argv){
    using namespace veyra::sink;using Clock=std::chrono::steady_clock;
    struct Pacer {
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        ~Pacer(){if(timer)CloseHandle(timer);}
        void until(Clock::time_point due){
            const auto ticks=std::chrono::duration_cast<std::chrono::nanoseconds>(due-Clock::now()).count()/100;
            if(ticks<=0)return;
            LARGE_INTEGER when{};when.QuadPart=-ticks;
            if(timer&&SetWaitableTimer(timer,&when,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,100);
            else std::this_thread::sleep_until(due);
        }
    } pacer;
    if(argc>=2&&std::string_view(argv[1])=="--xbox-float-rtp"){
        // Xbox's stereo float PCM and independent audio/video RTP origins,
        // through the real shared resampler and muted WASAPI endpoint.
        CaptureAudioSession output;
        const auto wave=floatWave({2,SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT});
        if(!output.configure(wave.Format,sizeof(wave))||!output.start())return 2;
        output.setGain(0);std::vector<float> block(960*2);
        for(size_t i=0;i<block.size();++i)block[i]=float(0.1*std::sin(i*0.025));
        const auto begin=Clock::now();
        for(unsigned i=0;i<250;++i){
            pacer.until(begin+std::chrono::milliseconds(i*20));
            if(!output.push(block.data(),block.size()*sizeof(float),i*20.0,i==0))return 3;
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            output.videoPresented(456789.0+i*20,host,host-350000);
        }
        const auto s=output.snapshot();output.stop();
        const bool pass=s.running&&s.inputFloating&&s.inputSampleRate==48000&&s.inputBlocks==250&&s.nonFiniteSamples==0&&s.bufferedMs<200&&s.compensationMs<100&&s.syncClockFallback;
        std::cout<<(pass?"PASS ":"FAIL ")<<"Xbox float/independent RTP origins: running="<<s.running<<" blocks="<<s.inputBlocks<<" bufferedMs="<<s.bufferedMs<<" compensationMs="<<s.compensationMs<<" fallback="<<s.syncClockFallback<<'\n';
        return pass?0:1;
    }
    const bool longOutage=argc>=2&&std::string_view(argv[1])=="--long-endpoint-loss";
    const bool endpointTest=longOutage||(argc>=2&&std::string_view(argv[1])=="--endpoint-loss");
    if(longOutage)SetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_LONG_OUTAGE",L"1");
    const bool slowStart=argc>=2&&std::string_view(argv[1])=="--slow-start";
    const bool jitterTest=slowStart||(argc>=2&&std::string_view(argv[1])=="--jitter");
    const bool transientTest=argc>=2&&std::string_view(argv[1])=="--transient";
    const bool compensatedTransientTest=argc>=2&&std::string_view(argv[1])=="--transient-comp";
    const bool fastDrift=argc>=2&&std::string_view(argv[1])=="--drift-fast";
    const bool driftTest=fastDrift||(argc>=2&&std::string_view(argv[1])=="--drift-slow");
    // Test-only stopped-device gap: no source callbacks should be replayed
    // to catch up with a deadline from the previous ingress epoch.
    const bool restartGap=argc>=2&&std::string_view(argv[1])=="--restart-gap";
    if(endpointTest)SetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_ENDPOINT_LOSS",L"1");
    if(slowStart)SetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_SLOW_START",L"1");
    const bool multichannel=argc>=3&&std::string_view(argv[2])=="--5.1";
    CaptureAudioSession audio;WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=transientTest?44100:48000;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
    auto extended=floatWave({6,0x60f});extended.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;extended.Format.wBitsPerSample=16;extended.Samples.wValidBitsPerSample=16;extended.Format.nBlockAlign=12;extended.Format.nAvgBytesPerSec=576000;
    if(!(multichannel?audio.configure(extended.Format,sizeof(extended)):audio.configure(f))||!audio.start())return 2;audio.setGain(0);
    const unsigned channels=multichannel?6:2;std::vector<int16_t> pcm(480*channels);
    for(size_t i=0;i<pcm.size()/channels;++i)for(unsigned c=0;c<channels;++c)pcm[i*channels+c]=int16_t(2000*std::sin((i*channels+c)*0.031));
    auto start=Clock::now();
    const bool legacyClock=argc>=2&&std::string_view(argv[1])=="--legacy-clock";
    auto present=[&](double pts,int64_t host,double localDelay){
        audio.videoPresented(pts,host,legacyClock?std::nullopt:std::optional<int64_t>(host-int64_t(localDelay*10000)));
    };
    if(argc>=2&&std::string_view(argv[1])=="--sync-clock-audit"){
        // Synthetic source timestamps with real, muted WASAPI output. A PTS
        // origin change must not be mistaken for extra video processing time.
        unsigned sequence=0;
        auto phase=[&](const char* name,unsigned mode,double videoOffset,unsigned blocks){
            audio.setSync(mode,0);
            unsigned nextVideo=sequence;
            double maxCompensation=0,maxQueue=0;unsigned observations=0;
            for(unsigned j=0;j<blocks;++j,++sequence){
                pacer.until(start+std::chrono::milliseconds(sequence*10));
                if(!audio.push(pcm.data(),pcm.size()*2,sequence*10.0,sequence==0))return false;
                const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
                if(sequence>=nextVideo){
                    audio.videoPresented(sequence*10.0-35-videoOffset,host,host-350000);
                    nextVideo=sequence+1+(sequence%3);
                }
                if(j+100>=blocks){
                    const auto s=audio.snapshot();
                    maxCompensation=std::max(maxCompensation,s.compensationMs);
                    maxQueue=std::max(maxQueue,s.bufferedMs);
                    if(s.running)++observations;
                }
            }
            const auto s=audio.snapshot();
            const bool pass=observations>=80&&maxCompensation<100&&maxQueue<200;
            std::cout<<(pass?"PASS ":"FAIL ")<<name<<" videoPtsOffsetMs="<<videoOffset
                <<" actualVideoDelayMs=35 maxCompensationMs="<<maxCompensation
                <<" maxQueuedMs="<<maxQueue<<" runningSamples="<<observations
                <<" resets="<<s.resets<<" overflows="<<s.overflows<<std::endl;
            return pass;
        };
        const bool jitter=phase("aligned_variable_cadence",0,0,400);
        const bool offset=phase("video_timestamp_offset",0,1200,500);
        const bool off=phase("same_offset_compensation_off",2,1200,250);
        audio.stop();
        std::cout<<"Synthetic clock-axis audit only; not physical VRR or acoustic measurement.\n";
        return jitter&&offset&&off?0:1;
    }
    if(transientTest){
        const size_t frames=441;std::vector<int16_t> block(frames*2);
        for(unsigned blockIndex=0;blockIndex<30;++blockIndex){
            pacer.until(start+std::chrono::milliseconds(blockIndex*10));
            std::fill(block.begin(),block.end(),blockIndex>=10&&blockIndex<20?int16_t(32767):int16_t(0));
            if(!audio.push(block.data(),block.size()*sizeof(int16_t),blockIndex*10.0,blockIndex==0))return 10;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto state=audio.snapshot();audio.stop();
        const bool pass=state.inputSampleRate==44100&&state.inputBlocks==30&&state.nonFiniteSamples==0&&state.clippedSamples==0&&state.overRangeSamples>0&&state.inputPeak>1.0;
        std::cout<<(pass?"PASS ":"FAIL ")<<"44.1k sharp transient conversion clipped="<<state.clippedSamples<<" peakProtected="<<state.peakProtectedSamples<<" nonFinite="<<state.nonFiniteSamples<<" peak="<<state.inputPeak<<" blocks="<<state.inputBlocks<<'\n';
        return pass?0:1;
    }
    if(compensatedTransientTest){
        const size_t frames=480;std::vector<int16_t> block(frames*2);
        for(unsigned blockIndex=0;blockIndex<500;++blockIndex){
            pacer.until(start+std::chrono::milliseconds(blockIndex*10));
            std::fill(block.begin(),block.end(),blockIndex>=300&&blockIndex<310?int16_t(32767):int16_t(0));
            if(!audio.push(block.data(),block.size()*sizeof(int16_t),blockIndex*10.0,blockIndex==0))return 11;
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            present(blockIndex*10.0-80,host,80);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto state=audio.snapshot();audio.stop();
        const bool pass=state.inputSampleRate==48000&&state.inputBlocks==500&&state.nonFiniteSamples==0&&state.clippedSamples==0&&state.overRangeSamples>0&&state.inputPeak>1.0;
        std::cout<<(pass?"PASS ":"FAIL ")<<"48k compensated sharp transient conversion clipped="<<state.clippedSamples<<" peakProtected="<<state.peakProtectedSamples<<" nonFinite="<<state.nonFiniteSamples<<" peak="<<state.inputPeak<<" correctionPpm="<<state.driftCorrectionPpm<<" blocks="<<state.inputBlocks<<'\n';
        return pass?0:1;
    }
    if(jitterTest){
        uint64_t settledResets=0,settledUnderruns=0;unsigned missing=0;std::vector<double> errors;
        for(unsigned i=0;i<500;++i){
            pacer.until(start+std::chrono::milliseconds(i*10));
            if(!audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0))return 9;
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            // Callback every 10ms; video updates at ~30fps with 30/35ms
            // software delay. Both share the original input PTS time base.
            if(i>=4&&i%3==0)present(i*10.0-((i/3)%2?30:35),now,(i/3)%2?30:35);
            const auto s=audio.snapshot();
            if(i==100){settledResets=s.resets;settledUnderruns=s.underruns;}
            if(i>100){if(s.running&&s.skewMs)errors.push_back(std::abs(*s.skewMs));else ++missing;}
        }
        const auto s=audio.snapshot();audio.stop();std::sort(errors.begin(),errors.end());
        const double p95=errors.empty()?999:errors[(errors.size()*95+99)/100-1];
        const bool inputTelemetry=s.inputBlocks==500&&std::abs(s.inputBlockMs-10)<.001&&s.inputIntervalMs>=0&&s.inputIntervalMs<100&&s.inputPeak>.01&&s.nonFiniteSamples==0&&s.clippedSamples==0;
        const bool pass=inputTelemetry&&missing==0&&s.resets==settledResets&&s.underruns==settledUnderruns&&s.overflows==0&&p95<35&&(!slowStart||s.recoveryDiscardedFrames>24000);
        std::cout<<(pass?"PASS ":"FAIL ")<<"CAPTURE_JITTER additionalResets="<<s.resets-settledResets<<" additionalUnderruns="<<s.underruns-settledUnderruns<<" missing="<<missing<<" p95SkewMs="<<p95<<" inputBlocks="<<s.inputBlocks<<" inputBlockMs="<<s.inputBlockMs<<" inputIntervalMs="<<s.inputIntervalMs<<" peak="<<s.inputPeak<<" nonFinite="<<s.nonFiniteSamples<<" clipped="<<s.clippedSamples<<'\n';
        return pass?0:1;
    }
    if(driftTest){
        std::vector<double> errors;unsigned missing=0;
        const double speed=fastDrift?1.001:.999;
        for(unsigned i=0;i<11980;++i){
            pacer.until(start+std::chrono::microseconds(int64_t(i*10000/speed)));
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            if(!audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0))return 6;
            if(i>=8)present(i*10.0-80,host,80);
            const auto s=audio.snapshot();if(i>1000){if(s.running&&s.skewMs)errors.push_back(std::abs(*s.skewMs));else ++missing;}
            if(i&&i%2000==0)std::cout<<"DRIFT seconds="<<i/100<<" skewMs="<<s.skewMs.value_or(-999)<<" resets="<<s.resets<<" queueMs="<<s.bufferedMs<<" correctionPpm="<<s.driftCorrectionPpm<<std::endl;
        }
        const auto state=audio.snapshot();audio.stop();std::sort(errors.begin(),errors.end());
        const double p95=errors.empty()?999:errors[(errors.size()*95+99)/100-1];
        const bool pass=p95<=30&&missing<100&&state.resets==1&&state.overflows==0&&state.bufferHighWaterMs<=500;
        std::cout<<(pass?"PASS ":"FAIL ")<<"120s capture clock speed="<<speed<<" p95SkewMs="<<p95<<" missing="<<missing<<" resets="<<state.resets<<" highWaterMs="<<state.bufferHighWaterMs<<'\n';return pass?0:1;
    }
    double sum=0;unsigned count=0;bool bounded=true,sawReconnecting=false;
    veyra::source::AudioInputRecovery inputRecovery;inputRecovery.reset(GetTickCount64());
    unsigned falseInputRecoveries=0,callbacksDuringOutage=0;
    const unsigned blocks=longOutage?650u:endpointTest?350u:160u;
    for(unsigned i=0;i<blocks;++i){
        const auto due=start+std::chrono::milliseconds(i*10);pacer.until(due);
        const auto time=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
        if(!audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0))return 3;
        if(i>=8)present(i*10.0-80,time,80);
        const auto s=audio.snapshot();bounded&=s.bufferedMs<=520;
        sawReconnecting|=!s.error.empty();
        if(!s.error.empty())++callbacksDuringOutage;
        if(inputRecovery.due(s.inputBlocks,GetTickCount64()))++falseInputRecoveries;
        if(i>(longOutage?540u:endpointTest?240u:70u)&&s.running&&s.skewMs){sum+=std::abs(*s.skewMs);++count;}
    }
    const auto state=audio.snapshot();
    std::cout<<"capture_audio meanAbsSkewMs="<<(count?sum/count:-1)<<" samples="<<count<<" compensationMs="<<state.compensationMs<<" queueMs="<<state.bufferedMs<<" resets="<<state.resets<<" overflows="<<state.overflows<<" underruns="<<state.underruns<<'\n';
    bool ok=count>=60&&sum/count<25&&state.compensationMs>=65&&state.compensationMs<=95&&bounded&&state.overflows==0;
    if(endpointTest){
        ok=ok&&sawReconnecting&&state.error.empty()&&state.endpointRetries>=2&&state.running;
        const auto lastProgress=GetTickCount64();
        inputRecovery.due(state.inputBlocks,lastProgress);
        const bool actualInputLossDetected=inputRecovery.due(state.inputBlocks,lastProgress+3001);
        if(longOutage)ok=ok&&falseInputRecoveries==0&&callbacksDuringOutage>=350&&state.inputBlocks==blocks&&actualInputLossDetected;
        audio.stop();std::cout<<(ok?"PASS ":"FAIL ")<<"owned WASAPI endpoint recovered without reopening capture retries="<<state.endpointRetries
            <<" falseInputRecoveries="<<falseInputRecoveries<<" callbacksDuringOutage="<<callbacksDuringOutage<<" inputBlocks="<<state.inputBlocks
            <<" actualInputLossDetected="<<actualInputLossDetected<<'\n';return ok?0:1;
    }
    std::cout<<(ok?"PASS":"FAIL")<<" synthetic capture PTS with 80ms video delay; real WASAPI, no physical capture\n";
    unsigned sequence=160;
    auto phase=[&](unsigned mode,int offset,double delay,double expectedComp,double expectedSkew,double commonInputMs=0,double maxSkewError=28){
        audio.setSync(mode,offset);double error=0;unsigned samples=0;bool boundedPhase=true;
        double maxIngressLateMs=0;unsigned catchUpCallbacks=0;
        const unsigned steps=delay>500?300:150;
        for(unsigned j=0;j<steps;++j,++sequence){
            const auto due=start+std::chrono::milliseconds(sequence*10);pacer.until(due);
            const double late=std::chrono::duration<double,std::milli>(Clock::now()-due).count();
            maxIngressLateMs=std::max(maxIngressLateMs,late);
            if(late>=10)++catchUpCallbacks;
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;
            if(!audio.push(pcm.data(),pcm.size()*2,sequence*10.0-commonInputMs,j==0&&commonInputMs!=0))return false;
            present(sequence*10.0-commonInputMs-delay,host,delay);const auto s=audio.snapshot();
            boundedPhase&=s.bufferedMs<=2000;
            if(j>steps-80&&s.running&&s.skewMs){error+=std::abs(*s.skewMs-expectedSkew);++samples;}
        }
        const auto s=audio.snapshot();const bool pass=samples>=60&&error/samples<maxSkewError&&std::abs(s.compensationMs-expectedComp)<15&&boundedPhase&&s.overflows==0;
        std::cout<<(pass?"PASS ":"FAIL ")<<"capture phase mode="<<mode<<" delay="<<delay<<" commonInputMs="<<commonInputMs<<" compensation="<<s.compensationMs<<" meanSkewError="<<(samples?error/samples:-1)<<" resets="<<s.resets<<" queueMs="<<s.bufferedMs<<" endpointMs="<<s.endpointBufferedMs<<" underruns="<<s.underruns<<" correctionPpm="<<s.driftCorrectionPpm<<" maxIngressLateMs="<<maxIngressLateMs<<" catchUpCallbacks="<<catchUpCallbacks<<'\n';return pass;
    };
    ok=phase(0,0,160,160,0)&&ok;
    ok=phase(0,0,400,400,0)&&ok;
    ok=phase(0,0,400,400,0,900)&&ok;
    ok=phase(0,0,900,900,0,900)&&ok;
    // Start a fresh ingress epoch after the shared-input offset fixture.
    audio.stop();
    if(restartGap)std::this_thread::sleep_for(std::chrono::milliseconds(400));
    if(!audio.start())return 7;
    // start() creates a fresh ingress clock. Restart the synthetic callback
    // schedule too: stopped time is not captured PCM waiting to be replayed.
    // Do not rebase between live phases or across the intentional input stall.
    start=Clock::now()-std::chrono::milliseconds(sequence*10);
    ok=phase(0,0,80,80,0)&&ok;
    const auto beforeGraphReset=audio.snapshot();
    audio.videoReset(false);
    for(unsigned i=0;i<35;++i,++sequence){
        pacer.until(start+std::chrono::milliseconds(sequence*10));
        // This models a video graph rebuild only.  The audio callback remains
        // on the same DirectShow epoch; a true audio discontinuity is tested
        // separately through the normal reset/re-anchor paths.
        if(!audio.push(pcm.data(),pcm.size()*2,sequence*10.0,false))return 8;
    }
    const auto afterGraphReset=audio.snapshot();
    const bool heldForGraph=afterGraphReset.running&&afterGraphReset.resets==beforeGraphReset.resets;
    std::cout<<(heldForGraph?"PASS ":"FAIL ")<<"video graph reset preserves running audio clock\n";ok=heldForGraph&&ok;
    ok=phase(0,0,80,80,0)&&ok;
    ok=phase(1,100,80,100,-20)&&ok;
    // With the shared endpoint's 20 ms safety window and no requested
    // compensation, the observed software skew has a wider scheduling
    // envelope than the auto/manual-delay phases. This is timing tolerance,
    // not permission to insert or drop PCM.
    ok=phase(2,0,80,0,80,0,45)&&ok;
    ok=phase(1,-100,80,0,80,0,45)&&ok;
    // Keep the SAME sync mode before and after the stall. Otherwise the
    // explicit mode-switch reset can falsely satisfy this recovery assertion.
    ok=phase(0,0,80,80,0)&&ok;
    const auto beforeStall=audio.snapshot().resets;
    std::this_thread::sleep_for(std::chrono::milliseconds(350));sequence+=35;
    const bool reanchoredWhileDry=audio.snapshot().resets>beforeStall;
    std::cout<<(reanchoredWhileDry?"PASS ":"FAIL ")<<"persistent dry input recovers without a sync setting change\n";ok=reanchoredWhileDry&&ok;
    ok=phase(0,0,80,80,0)&&ok;
    const bool recovered=audio.snapshot().resets>beforeStall;
    std::cout<<(recovered?"PASS ":"FAIL ")<<"audio reanchors real PCM after ingress stall\n";ok=recovered&&ok;
    audio.stop();
    const auto stopped=audio.snapshot();const bool stoppedClean=!stopped.available&&!stopped.running&&!stopped.skewMs&&stopped.bufferedMs==0;
    std::cout<<(stoppedClean?"PASS ":"FAIL ")<<"stopped audio has no stale clock or queued state\n";ok=stoppedClean&&ok;
    if(restartGap)std::this_thread::sleep_for(std::chrono::milliseconds(400));
    if(!audio.start())return 4;
    start=Clock::now()-std::chrono::milliseconds(sequence*10);
    ok=phase(0,0,80,80,0)&&ok;
    audio.stop();
    if(!audio.start())return 5;
    audio.setSync(0,0);
    bool burstAccepted=true;
    for(unsigned i=0;i<600;++i)burstAccepted=audio.push(pcm.data(),pcm.size()*2,i*10.0,i==0)&&burstAccepted;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    const auto burst=audio.snapshot();
    const bool burstBound=burstAccepted&&burst.bufferHighWaterMs<=2000&&burst.overflows>0;
    std::cout<<(burstBound?"PASS ":"FAIL ")<<"combined raw/converting/PCM burst budget highWaterMs="<<burst.bufferHighWaterMs<<" overflows="<<burst.overflows<<'\n';ok=burstBound&&ok;
    audio.stop();
    return ok?0:1;
}
