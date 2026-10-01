#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/sink/ArrivalClockMapping.h"
#include "veyra/sink/CaptureAudioDsp.h"
#include "veyra/sink/CaptureSyncTarget.h"
#include "veyra/diagnostics/CapturePcmRecording.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
extern "C" {
#include <libswresample/swresample.h>
}
namespace veyra::sink {
namespace {
int64_t hostTime(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;}
}
struct CaptureAudioSession::Impl : AudioPcmSource {
    diagnostics::CapturePcmRecording recording;
    struct Chunk {std::vector<uint8_t> bytes;double pts=0;bool discontinuity=false;};
    WAVEFORMATEX format{};
    AudioFormat layout;unsigned validBits=0;bool floating=false;
    AudioFormat pcmFormat()const override{return layout;}
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Chunk> input;
    size_t inputBytes=0,convertingBytes=0;
    // PCM queue: contiguous storage with a head cursor instead of a deque.
    // pull() used to copy and pop_front one sample at a time under the mutex
    // (8ch/48k: ~3840 deque operations per 10 ms) while the capture thread
    // waited on the same lock (sweep 2026-09-22 C5).
    struct PcmQueue {
        std::vector<float> data;size_t head=0;
        size_t size()const{return data.size()-head;}
        bool empty()const{return size()==0;}
        void clear(){data.clear();head=0;}
        const float* begin()const{return data.data()+head;}
        float operator[](size_t i)const{return data[head+i];}
        void append(const float* first,const float* last){compact();data.insert(data.end(),first,last);}
        void popFront(size_t n){head=std::min(data.size(),head+n);if(head==data.size())clear();}
        void compact(){if(head>=65536&&head*2>=data.size()){data.erase(data.begin(),data.begin()+ptrdiff_t(head));head=0;}}
    } pcm;
    double headPts=0;
    AudioFrameTimeline pcmTimeline;
    uint64_t pcmHead=0,pcmTail=0;
    std::optional<double> pullEnd;
    bool haveHead=false,pendingReset=false;
    std::atomic<bool> stop{false};
    std::atomic<float> gain{1};
    std::atomic<unsigned> mode{0};std::atomic<int> offset{0};
    double videoPts=0,videoHost=0;bool haveVideo=false;
    std::optional<int64_t> videoArrival;
    uint64_t videoObservation=0;
    double ingressMapping=0;bool haveIngress=false;
    ArrivalClockMapping ingressClock;
    int64_t lastArrival=0;
    CaptureAudioState state;
    std::thread thread;
    static constexpr double maxAutoDelayMs=1500,maxQueuedMs=2000;
    double queuedMs()const{return 1000.0*(pcm.size()/layout.channels)/kAudioRate+1000.0*(inputBytes+convertingBytes)/format.nAvgBytesPerSec;}
    void queueChanged(){state.bufferedMs=queuedMs();state.bufferHighWaterMs=std::max(state.bufferHighWaterMs,state.bufferedMs);}
    void clearPcmLocked(){pcm.clear();haveHead=false;pcmTimeline.clear();pcmHead=pcmTail=0;pullEnd.reset();}
    void clearPcm(){std::lock_guard lock(mutex);clearPcmLocked();queueChanged();}
    void discardPcm(size_t frames){
        pcm.popFront(frames*layout.channels);pcmHead+=frames;
        headPts=pcmTimeline.at(double(pcmHead)).value_or(headPts);
        pcmTimeline.discardBefore(pcmHead);queueChanged();
    }
    void fail(const wchar_t* reason){std::lock_guard lock(mutex);state.available=false;state.running=false;state.outputRecovering=false;state.skewMs.reset();state.error=reason;}
    size_t pull(float* dst,size_t frames,double* pts)override{
        std::lock_guard lock(mutex);
        const size_t take=std::min(frames,pcm.size()/layout.channels);
        *pts=haveHead?headPts:-1;
        if(take)std::memcpy(dst,pcm.begin(),take*layout.channels*sizeof(float));
        recording.pulled(dst,take*layout.channels);
        discardPcm(take);pullEnd=take?std::optional<double>(headPts):std::nullopt;
        return take;
    }
    std::optional<double> lastPullEndPtsMs()const override{return pullEnd;}
    bool padUnderruns()const override{return false;}
    void observeRenderedPcm(const float* samples,size_t frames,unsigned channels,unsigned padding)override{recording.rendered(samples,frames,channels,padding);}
    void run(){
        AudioRenderer renderer;
        bool endpointReady=false;int64_t retryAt=0;
        const bool injectEndpointLoss=GetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_ENDPOINT_LOSS",nullptr,0)>0;
        const bool injectSlowStart=GetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_SLOW_START",nullptr,0)>0;
        bool startupDelayed=false;
        bool injected=false;uint64_t endpointPumps=0;
        SwrContext* swr=nullptr;
        const auto inputFormat=floating?AV_SAMPLE_FMT_FLT:format.wBitsPerSample==16?AV_SAMPLE_FMT_S16:AV_SAMPLE_FMT_S32;
        auto makeResampler=[&](SwrContext*& result)->bool{
            AVChannelLayout out{},in{};
            const int outResult=av_channel_layout_from_mask(&out,layout.mask);
            const int inResult=av_channel_layout_from_mask(&in,layout.mask);
            const int configured=outResult<0||inResult<0?-1:swr_alloc_set_opts2(&result,&out,AV_SAMPLE_FMT_FLT,kAudioRate,&in,inputFormat,format.nSamplesPerSec,0,nullptr);
            av_channel_layout_uninit(&in);av_channel_layout_uninit(&out);
            if(configured<0||!result||swr_init(result)<0){swr_free(&result);return false;}
            return true;
        };
        if(!makeResampler(swr)){log::error("capture-audio","resampler initialization failed");renderer.shutdown();fail(L"Audio resampler initialization failed");return;}
        log::info("capture-audio-resampler",std::format("inputRate={} outputRate={} filter=default-kaiser continuousHistory=1 blockAgc=0 floatHeadroom=1",format.nSamplesPerSec,kAudioRate));
        const auto releaseSwr=[](SwrContext* value){swr_free(&value);};
        std::unique_ptr<SwrContext,decltype(releaseSwr)> resampler(swr,releaseSwr);
        double filteredError=0,correctionPpm=0;
        double correctionQueuedMs=0;bool correctionReserveLimited=false;
        CaptureRateCorrection rateCorrection;
        int64_t nextCorrection=0;
        int64_t lastReset=0;
        int64_t nextSyncLog=0;
        int64_t nextPeakLog=0;
        unsigned appliedMode=mode.load();int appliedOffset=offset.load();
        CaptureSyncTarget syncTarget;
        CaptureSyncTarget::Result syncEstimate;
        uint64_t lastVideoObservation=0;
        uint64_t lastUnderruns=0;
        uint64_t lastUnderrunFrames=0,lastSilenceFrames=0;
        uint64_t lastEmptyPulls=0;
        int64_t starvationSince=0;
        static constexpr int64_t liveUnderrunGrace100ns=300000; // 30 ms
        bool endpointEventReady=false;
        auto clearCorrection=[&]{
            filteredError=correctionPpm=0;nextCorrection=hostTime()+2500000;
            const int result=rateCorrection.set(swr,0);
            if(result<0){log::error("capture-audio",std::format("clear compensation failed code={}",result));return false;}
            std::lock_guard lock(mutex);state.driftCorrectionPpm=0;return true;
        };
        auto recoverEndpoint=[&]{
            const auto error=renderer.lastError();renderer.shutdown();endpointReady=false;retryAt=hostTime()+5000000;
            if(injectEndpointLoss&&GetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_AUDIO_LONG_OUTAGE",nullptr,0)>0)
                retryAt=hostTime()+40000000;
            clearPcm();swr_close(swr);if(swr_init(swr)<0){fail(L"Audio resampler reset failed");return false;}
            if(!clearCorrection())return false;
            endpointEventReady=false;
            std::lock_guard lock(mutex);input.clear();inputBytes=convertingBytes=0;pendingReset=false;haveVideo=false;
            state.available=false;state.running=false;state.skewMs.reset();state.endpointBufferedMs=0;
            state.outputRecovering=true;
            state.error=L"Audio output disconnected; reconnecting ("+std::to_wstring(unsigned(error))+L")";queueChanged();return true;
        };
        while(!stop){
            if(!endpointReady){
                const auto time=hostTime();
                if(time<retryAt){std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(20),[&]{return stop.load();});continue;}
                {std::lock_guard lock(mutex);++state.endpointRetries;}
                if(injectSlowStart&&!startupDelayed){
                    startupDelayed=true;log::info("capture-audio-test","delay owned endpoint startup by 650 ms");
                    std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(650),[&]{return stop.load();});
                    if(stop)break;
                }
                if(!renderer.start(layout,20.0)){if(!recoverEndpoint())break;continue;}
                endpointReady=true;lastUnderruns=renderer.underruns();
                lastUnderrunFrames=renderer.underrunFrames();lastSilenceFrames=renderer.silenceFrames();starvationSince=0;
                {std::lock_guard lock(mutex);state.error.clear();state.outputRecovering=false;state.inputChannels=layout.channels;state.inputChannelMask=layout.mask;const auto output=renderer.outputFormat();state.outputChannels=output.channels;state.outputChannelMask=output.mask;pendingReset=true;}
            }
            renderer.setGain(gain);
            // Wait before collecting callbacks, so PCM arriving during the
            // endpoint wait is available for this fill instead of padded silence.
            if(renderer.started()&&!endpointEventReady){if(!renderer.waitForEvent()){if(!recoverEndpoint())break;continue;}endpointEventReady=true;}
            Chunk chunk;bool reset=false,syncChanged=false;double vPts=0,vHost=0,ingress=0;bool video=false;int64_t arrival=0;
            std::optional<int64_t> vArrival;uint64_t observation=0;
            {
                std::unique_lock lock(mutex);
                if(input.empty())wake.wait_for(lock,std::chrono::milliseconds(2));
                if(!input.empty()){chunk=std::move(input.front());input.pop_front();inputBytes-=chunk.bytes.size();convertingBytes=chunk.bytes.size();}
                reset=pendingReset;pendingReset=false;video=haveVideo;vPts=videoPts;vHost=videoHost;
                vArrival=videoArrival;observation=videoObservation;
                ingress=ingressMapping;
                arrival=lastArrival;
            }
            if(mode!=appliedMode||offset!=appliedOffset){
                syncChanged=true;reset=true;appliedMode=mode;appliedOffset=offset;
                log::info("capture-audio-sync",std::format("mode={} offsetMs={} reanchor=explicit-audio-setting",appliedMode,appliedOffset));
            }
            if(syncChanged&&!clearCorrection()){fail(L"Audio sync compensation reset failed");break;}
            if(reset||chunk.discontinuity){
                syncTarget.reset();syncEstimate={};lastVideoObservation=0;
                renderer.fadeAndReset(*this,stop);clearPcm();swr_close(swr);
                if(swr_init(swr)<0){fail(L"Audio resampler reset failed");break;}
                const int prepared=rateCorrection.prepare(swr,appliedMode==0);
                if(prepared<0){log::error("capture-audio",std::format("prepare compensation failed code={}",prepared));fail(L"Audio compensation initialization failed");break;}
                if(!clearCorrection()){fail(L"Audio drift correction reset failed");break;}
                std::lock_guard lock(mutex);++state.resets;
            }
            if(!chunk.bytes.empty()){
                const int inputFrames=int(chunk.bytes.size()/format.nBlockAlign);
                const auto delay=swr_get_delay(swr,format.nSamplesPerSec);
                const int capacity=swr_get_out_samples(swr,inputFrames);
                if(capacity<=0||capacity>int(kAudioRate)){log::error("capture-audio","invalid converted block capacity");fail(L"Audio conversion block size is invalid");break;}
                std::vector<float> converted(size_t(capacity)*layout.channels);
                uint8_t* dst[]={reinterpret_cast<uint8_t*>(converted.data())};const uint8_t* src[]={chunk.bytes.data()};
                std::vector<int16_t> normalized16;
                std::vector<int32_t> normalized32;
                std::vector<int32_t> packed24;
                uint64_t invalidPadding=0;
                const unsigned paddingBits=format.wBitsPerSample-validBits;
                const uint32_t paddingMask=paddingBits?((uint32_t(1)<<paddingBits)-1u):0u;
                if(!floating&&validBits<format.wBitsPerSample&&format.wBitsPerSample==16){
                    normalized16.resize(size_t(inputFrames)*layout.channels);
                    for(size_t i=0;i<normalized16.size();++i){
                        uint16_t raw=0;memcpy(&raw,chunk.bytes.data()+i*2,sizeof(raw));
                        if(raw&paddingMask)++invalidPadding;raw=static_cast<uint16_t>(raw&~paddingMask);
                        memcpy(&normalized16[i],&raw,sizeof(raw));
                    }
                    src[0]=reinterpret_cast<const uint8_t*>(normalized16.data());
                }else if(!floating&&validBits<format.wBitsPerSample&&format.wBitsPerSample==32){
                    normalized32.resize(size_t(inputFrames)*layout.channels);
                    for(size_t i=0;i<normalized32.size();++i){
                        uint32_t raw=0;memcpy(&raw,chunk.bytes.data()+i*4,sizeof(raw));
                        if(raw&paddingMask)++invalidPadding;raw&=~paddingMask;
                        memcpy(&normalized32[i],&raw,sizeof(raw));
                    }
                    src[0]=reinterpret_cast<const uint8_t*>(normalized32.data());
                }else if(!floating&&format.wBitsPerSample==24){
                    // WAVEFORMATEXTENSIBLE PCM stores valid bits left-aligned
                    // in the byte container. Expand the signed 24-bit sample
                    // to the high 24 bits of S32 for libswresample.
                    packed24.resize(size_t(inputFrames)*layout.channels);
                    for(size_t i=0;i<packed24.size();++i){
                        const auto* p=chunk.bytes.data()+i*3;uint32_t raw=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16;
                        if(paddingMask&&((raw&0xFFFFFFu)&paddingMask))++invalidPadding;raw&=0xFFFFFFu&~paddingMask;
                        const uint32_t expanded=raw<<8;memcpy(&packed24[i],&expanded,sizeof(expanded));
                    }
                    src[0]=reinterpret_cast<const uint8_t*>(packed24.data());
                }
                const int count=swr_convert(swr,dst,capacity,src,inputFrames);
                if(count<0){log::error("capture-audio",std::format("convert failed code={}",count));fail(L"Audio conversion failed");break;}
                const auto stats=inspectCapturePcm(converted.data(),size_t(count)*layout.channels);
                recording.converted(converted.data(),size_t(count)*layout.channels);
                {
                    std::lock_guard lock(mutex);
                    state.invalidPaddingSamples+=invalidPadding;state.nonFiniteSamples+=stats.nonFinite;
                    state.overRangeSamples+=stats.overRange;state.inputPeak=std::max<double>(state.inputPeak,stats.peak);
                }
                const auto sampleLogTime=hostTime();
                if((invalidPadding||stats.nonFinite||stats.overRange)&&sampleLogTime>=nextPeakLog){
                    nextPeakLog=sampleLogTime+5000000;
                    log::warn("capture-audio-samples",std::format("invalidPadding={} nonFinite={} overRange={} convertedPeak={:.6f} blockAgc=0 format={}Hz/{}bit validBits={}",invalidPadding,stats.nonFinite,stats.overRange,stats.peak,format.nSamplesPerSec,format.wBitsPerSample,validBits));
                }
                const double start=chunk.pts-1000.0*delay/format.nSamplesPerSec;
                const double end=chunk.pts+1000.0*(inputFrames-swr_get_delay(swr,format.nSamplesPerSec))/format.nSamplesPerSec;
                if(reset||chunk.discontinuity)log::info("capture-audio-reset",std::format("mode={} pts={} delayBefore={} delayAfter={} input={} output={}",appliedMode,chunk.pts,delay,swr_get_delay(swr,format.nSamplesPerSec),inputFrames,count));
                if(haveHead&&std::abs(start-pcmTimeline.at(double(pcmTail)).value_or(start))>50){
                    renderer.fadeAndReset(*this,stop);clearPcm();
                    std::lock_guard lock(mutex);++state.resets;
                }
                {
                    std::lock_guard lock(mutex);convertingBytes=0;
                    if(pendingReset){queueChanged();continue;}
                    if(queuedMs()+1000.0*count/kAudioRate>maxQueuedMs){
                        input.clear();inputBytes=0;clearPcmLocked();pendingReset=true;++state.overflows;
                    }else{
                        if(!haveHead){headPts=start;haveHead=true;}
                        pcmTimeline.append(pcmTail,count,start,end);pcmTail+=count;
                        pcm.append(converted.data(),converted.data()+size_t(count)*layout.channels);
                    }
                    queueChanged();
                }
            }
            {
                std::lock_guard lock(mutex);
                // Drain callback blocks already available before waiting for
                // another endpoint event; otherwise silence can build a backlog.
                // Before (re)anchoring, include the whole bounded input backlog
                // so expiry below can remove old sound after a slow device open.
                if(!input.empty()&&(!renderer.started()||1000.0*(pcm.size()/layout.channels)/kAudioRate<renderer.capacityMs()+10))continue;
            }
            // Both streams use graph PTS. Relate the latest displayed video PTS
            // to host time; the endpoint's own queued frames are not added again.
            const double now=double(hostTime())/10000;
            const bool fresh=video&&now>=vHost&&now-vHost<500;
            if(appliedMode==0&&fresh&&observation!=lastVideoObservation){
                const bool previousFallback=syncEstimate.fallback;
                syncEstimate=syncTarget.observe(vHost-vPts-ingress,vHost,vArrival?std::optional<double>(double(*vArrival)/10000):std::nullopt);
                lastVideoObservation=observation;
                if(previousFallback!=syncEstimate.fallback)
                    log::warn("capture-audio-clock",std::format("fallback={} rawMs={:.3f} localVideoMs={:.3f} acceptedMs={:.3f} basis=matched-original-arrival hardwareOffsetUnmeasured=1",syncEstimate.fallback,syncEstimate.rawMs,syncEstimate.localMs,syncEstimate.targetMs));
            }
            if(!fresh)syncTarget.invalidate();
            const double requested=appliedMode==0?syncEstimate.targetMs:appliedMode==1?double(appliedOffset):0;
            const double maxDelay=appliedMode==0?maxAutoDelayMs:250.0;
            const double target=std::clamp(requested,0.0,maxDelay);
            const double mapping=ingress+target;
            const bool limited=requested<0||requested>maxDelay||(appliedMode==0&&syncEstimate.limited);
            // A temporary video rebuild must not stop a healthy capture audio
            // clock.  The next presented video sample will refresh the
            // mapping; a real PCM discontinuity is handled above instead.
            bool justStarted=false;
            // The capture callback is negotiated at 10ms, while shared-mode
            // WASAPI commonly returns a ~22ms endpoint buffer. Starting with
            // only one callback leaves half of that endpoint exposed to
            // scheduler jitter. Keep one full 20ms safety window for capture;
            // this is startup/re-anchor buffering, not a claimed end-to-end
            // latency measurement.
            const double startupPrefillMs=std::min(renderer.capacityMs(),20.0);
            if(!renderer.started()&&haveHead&&1000.0*(pcm.size()/layout.channels)/kAudioRate>=startupPrefillMs&&(appliedMode!=0||fresh)){
                // Live recovery discards expired sound rather than replaying
                // an obsolete half-second after a GPU stall or graph reset.
                const size_t prefill=size_t(std::ceil(startupPrefillMs*kAudioRate/1000));
                const size_t expired=std::min(pcm.size()/layout.channels-prefill,size_t(std::max(0.0,(now-mapping-headPts-5)*kAudioRate/1000)));
                {std::lock_guard lock(mutex);discardPcm(expired);state.recoveryDiscardedFrames+=expired;}
                if(expired)log::info("capture-audio-reanchor",std::format("expiredFrames={} expiredMs={:.3f} targetMs={:.3f} reason=startup-or-recovery",expired,1000.0*expired/kAudioRate,target));
                const bool due=now>=mapping+headPts;
                if(due&&!pcm.empty()){
                    if(!renderer.startAnchored(*this)){if(!recoverEndpoint())break;continue;}
                    justStarted=true;endpointEventReady=false;
                    lastReset=hostTime();filteredError=correctionPpm=0;nextCorrection=lastReset+2500000;
                }
            }
            if(renderer.started()&&!justStarted){
                // Test only: release this session's endpoint, never change a
                // system device or interfere with another application's audio.
                if(injectEndpointLoss&&!injected&&++endpointPumps==60){injected=true;log::warn("capture-audio-test","inject owned endpoint loss");if(!recoverEndpoint())break;continue;}
                double pts=-1;if(!renderer.pumpOnce(*this,&pts,false)){if(!recoverEndpoint())break;continue;}
                endpointEventReady=false;
                const auto underruns=renderer.underruns();
                const auto underrunFrames=renderer.underrunFrames();
                const auto silenceFrames=renderer.silenceFrames();
                if(underruns>lastUnderruns||underrunFrames>lastUnderrunFrames||
                   (starvationSince&&renderer.bufferedMs()==0&&renderer.emptyPulls()>lastEmptyPulls)){
                    const auto underrunNow=hostTime();
                    if(!starvationSince)starvationSince=underrunNow;
                    const double starvationMs=double(underrunNow-starvationSince)/10000.0;
                    if(underruns==1||(underruns%16)==0||starvationMs>=30.0)
                        log::warn("capture-audio-underrun",std::format("events={} deltaEvents={} missingFrames={} silenceFrames={} starvationMs={:.3f} inputAgeMs={:.3f} endpointMs={:.3f}",underruns,underruns-lastUnderruns,underrunFrames-lastUnderrunFrames,silenceFrames-lastSilenceFrames,starvationMs,double(underrunNow-arrival)/10000.0,renderer.bufferedMs()));
                    if(underrunNow-starvationSince>=liveUnderrunGrace100ns&&underrunNow-arrival>=liveUnderrunGrace100ns){
                        // A single missed callback is kept as controlled
                        // silence. Persistent starvation gets a bounded fade
                        // and a fresh PCM/PTS anchor instead of a hard click.
                        const auto fade=renderer.fadeAndReset(*this,stop);
                        clearPcm();swr_close(swr);
                        if(swr_init(swr)<0){fail(L"Audio underrun recovery failed");break;}
                        if(!clearCorrection()){fail(L"Audio drift correction reset failed");break;}
                        lastReset=hostTime();starvationSince=0;endpointEventReady=false;
                        std::lock_guard lock(mutex);++state.resets;
                        log::warn("capture-audio-underrun",std::format("reanchor result={} afterMs={:.3f} underruns={} missingFrames={}",int(fade),starvationMs,underruns,underrunFrames));
                    }
                }else if(starvationSince){
                    const double recoveredMs=double(hostTime()-starvationSince)/10000.0;
                    if(recoveredMs>=5.0)
                        log::info("capture-audio-underrun",std::format("recovered before reanchor starvationMs={:.3f}",recoveredMs));
                    starvationSince=0;
                }
                lastUnderruns=underruns;lastUnderrunFrames=underrunFrames;lastSilenceFrames=silenceFrames;
                lastEmptyPulls=renderer.emptyPulls();
                // Large video-delay changes need a bounded re-anchor. Never
                // chase every jitter sample by stopping the audio endpoint.
                const auto observed=hostTime();const double audioPts=renderer.mediaTimeMs();
                if(appliedMode==0&&fresh&&std::isfinite(audioPts)&&observed>=nextCorrection){
                    const double error=audioPts-(double(observed)/10000-mapping);
                    filteredError=.75*filteredError+.25*error;nextCorrection=observed+2500000;
                    if(observed-lastReset>10000000&&std::abs(error)>60){
                        renderer.fadeAndReset(*this,stop);lastReset=hostTime();filteredError=correctionPpm=0;
                        if(!clearCorrection()){fail(L"Audio drift correction reset failed");break;}
                        std::lock_guard lock(mutex);++state.resets;
                    }else{
                        const double targetPpm=std::clamp(filteredError*250.0,-5000.0,5000.0);
                        // Include all pending source PCM plus the endpoint's
                        // real padding after this write. A presentation target
                        // below this reserve is physically unattainable without
                        // starving playback; do not squeeze out those samples.
                        {std::lock_guard lock(mutex);correctionQueuedMs=queuedMs()+renderer.bufferedMs();}
                        const double requestedPpm=correctionPpm+std::clamp(targetPpm-correctionPpm,-250.0,250.0);
                        correctionPpm=captureSafeCorrectionPpm(targetPpm,correctionPpm,correctionQueuedMs,startupPrefillMs);
                        correctionReserveLimited=correctionPpm>requestedPpm+.01;
                        const int delta=int(std::llround(correctionPpm*kAudioRate/1000000));
                        {
                            const int result=rateCorrection.set(swr,delta,kAudioRate);
                            if(result<0){log::error("capture-audio",std::format("drift compensation failed code={}",result));fail(L"Audio drift correction failed");break;}
                        }
                        std::lock_guard lock(mutex);state.driftCorrectionPpm=correctionPpm;
                    }
                }
            }
            {
                std::lock_guard lock(mutex);state.available=true;state.running=renderer.started();state.limited=limited;
                queueChanged();
                state.compensationMs=target;state.underruns=renderer.underruns();
                state.syncClockFallback=appliedMode==0&&syncEstimate.fallback;
                state.rawCompensationMs=syncEstimate.rawMs;state.localVideoDelayMs=syncEstimate.localMs;
                state.underrunFrames=renderer.underrunFrames();state.silenceFrames=renderer.silenceFrames();
                state.emptyPulls=renderer.emptyPulls();state.recoveryFades=renderer.recoveryFades();
                state.clockStalledGaps=renderer.clockStalledGaps();
                state.endpointBufferedMs=renderer.bufferedMs();
                const auto audioPts=renderer.mediaTimeMs();
                const double observedNow=double(hostTime())/10000;
                state.skewMs=fresh&&std::isfinite(audioPts)?std::optional<double>(audioPts-(state.syncClockFallback?observedNow-mapping:vPts+observedNow-vHost)):std::nullopt;
                if(hostTime()>=nextSyncLog){
                    nextSyncLog=hostTime()+20000000;
                    if(appliedMode==0)log::info("capture-audio-reserve",std::format("queuedAtCorrectionMs={:.3f} minimumMs={:.3f} rateLimited={} correctionPpm={:.1f} (real scheduling reserve, not extra video delay)",correctionQueuedMs,startupPrefillMs,correctionReserveLimited,correctionPpm));
                    if(vArrival)log::info("capture-audio-target",std::format("mode={} rawMs={:.3f} localVideoMs={:.3f} acceptedMs={:.3f} fallback={} fresh={} rawSkewMs={:.3f} presentation=original inputArrival100ns={}",appliedMode,syncEstimate.rawMs,syncEstimate.localMs,target,state.syncClockFallback,fresh,fresh&&std::isfinite(audioPts)?audioPts-(vPts+observedNow-vHost):-999,*vArrival));
                    log::info("live-audio-sync",std::format("videoPtsMs={:.3f} videoHostMs={:.3f} audioIngressMapMs={:.3f} compensationMs={:.3f} pcmMs={:.3f} endpointMs={:.3f} skewMs={:.3f} correctionPpm={:.1f} resets={} underruns={} underrunFrames={} silenceFrames={} emptyPulls={} recoveryFades={} inputBlockMs={:.3f} inputIntervalMs={:.3f} inputBlocks={} format={}Hz/{}bit validBits={} convertedPeak={:.5f} overRange={} clipped={} (local clock alignment, not GPU execution time)",vPts,vHost,ingress,target,state.bufferedMs,state.endpointBufferedMs,state.skewMs.value_or(-999),state.driftCorrectionPpm,state.resets,state.underruns,state.underrunFrames,state.silenceFrames,state.emptyPulls,state.recoveryFades,state.inputBlockMs,state.inputIntervalMs,state.inputBlocks,state.inputSampleRate,state.inputContainerBits,state.inputValidBits,state.inputPeak,state.overRangeSamples,state.clippedSamples));
                }
            }
        }
        renderer.shutdown();std::lock_guard lock(mutex);state.available=false;state.running=false;state.skewMs.reset();state.endpointBufferedMs=0;
    }
};
CaptureAudioSession::CaptureAudioSession():p_(std::make_unique<Impl>()){}
CaptureAudioSession::~CaptureAudioSession(){stop();}
void CaptureAudioSession::setInputBitstream(std::wstring kind){
    std::lock_guard lock(p_->mutex);
    p_->state.inputBitstream=std::move(kind);
}
bool CaptureAudioSession::configure(const WavePcmFormat& parsed){
    if(p_->thread.joinable()||!parsed.layout.valid()||!parsed.validBits)return false;
    p_->format=parsed.wave;p_->layout=parsed.layout;p_->validBits=parsed.validBits;p_->floating=parsed.floating;
    p_->recording.configure(parsed.wave.nSamplesPerSec,parsed.layout.channels,parsed.wave.wBitsPerSample,parsed.validBits,parsed.floating);
    {
        std::lock_guard lock(p_->mutex);
        p_->state.inputChannels=parsed.layout.channels;p_->state.inputChannelMask=parsed.layout.mask;
        p_->state.inputSampleRate=parsed.wave.nSamplesPerSec;p_->state.inputContainerBits=parsed.wave.wBitsPerSample;
        p_->state.inputValidBits=parsed.validBits;p_->state.inputFloating=parsed.floating;
    }
    log::info("capture-audio-format",std::format("selected channels={} mask=0x{:X} rate={} containerBits={} validBits={} floating={} blockAlign={} avgBytesPerSec={}",parsed.layout.channels,parsed.layout.mask,parsed.wave.nSamplesPerSec,parsed.wave.wBitsPerSample,parsed.validBits,parsed.floating?1:0,parsed.wave.nBlockAlign,parsed.wave.nAvgBytesPerSec));
    return true;
}
bool CaptureAudioSession::configure(const WAVEFORMATEX& f,size_t bytes){
    WavePcmFormat parsed;
    if(!parseWavePcm(&f,bytes,parsed))return false;
    return configure(parsed);
}
bool CaptureAudioSession::start(){
    if(!p_->format.nBlockAlign||p_->thread.joinable())return false;
    {std::lock_guard lock(p_->mutex);p_->input.clear();p_->inputBytes=p_->convertingBytes=0;p_->clearPcmLocked();p_->haveHead=p_->haveVideo=p_->haveIngress=p_->pendingReset=false;p_->lastArrival=0;p_->state={};p_->state.inputChannels=p_->layout.channels;p_->state.inputChannelMask=p_->layout.mask;p_->state.inputSampleRate=p_->format.nSamplesPerSec;p_->state.inputContainerBits=p_->format.wBitsPerSample;p_->state.inputValidBits=p_->validBits;p_->state.inputFloating=p_->floating;}
    p_->stop=false;
    try{p_->thread=std::thread([this]{try{p_->run();}catch(const std::exception& e){log::error("capture-audio",std::format("audio thread failed: {}",e.what()));p_->fail(L"Audio thread exception; please reopen capture");}});}
    catch(const std::exception& e){log::error("capture-audio",std::format("audio thread start failed: {}",e.what()));p_->fail(L"Could not create audio thread");return false;}
    return true;
}
void CaptureAudioSession::stop(){p_->stop=true;p_->wake.notify_all();if(p_->thread.joinable())p_->thread.join();std::lock_guard lock(p_->mutex);p_->recording.finish();p_->input.clear();p_->inputBytes=p_->convertingBytes=0;p_->clearPcmLocked();p_->state.bufferedMs=0;p_->state.driftCorrectionPpm=0;}
bool CaptureAudioSession::push(const void* data,size_t bytes,double pts,bool discontinuity){
    auto& p=*p_;if(!data||!p.format.nBlockAlign||bytes%p.format.nBlockAlign||bytes>p.format.nAvgBytesPerSec/2||!std::isfinite(pts))return false;if(!bytes)return true;
    std::lock_guard lock(p.mutex);
    if(p.stop||(!p.state.error.empty()&&!p.state.outputRecovering))return true;
    const auto arrival=hostTime();
    p.state.inputIntervalMs=p.state.inputBlocks&&!discontinuity?double(arrival-p.lastArrival)/10000:0;
    p.state.inputBlockMs=1000.0*bytes/p.format.nAvgBytesPerSec;++p.state.inputBlocks;
    p.lastArrival=arrival;
    // A lost output endpoint does not mean the capture device stopped sending.
    // Keep its liveness visible without accumulating stale PCM for replay.
    if(p.state.outputRecovering)return true;
    p.recording.raw(data,bytes);
    if(discontinuity){p.input.clear();p.inputBytes=0;p.pendingReset=true;p.haveVideo=false;}
    if(!p.haveIngress||discontinuity){p.ingressClock.reset();p.haveIngress=true;}
    p.ingressMapping=p.ingressClock.observe(double(p.lastArrival)/10000,pts);
    const double blockMs=1000.0*bytes/p.format.nAvgBytesPerSec;
    if(p.queuedMs()+blockMs>Impl::maxQueuedMs){
        p.input.clear();p.inputBytes=0;p.pendingReset=true;++p.state.overflows;p.queueChanged();
        // Only the audio thread can reset its resampler and converted queue.
        // Drop this callback if that outstanding data still consumes the budget.
        if(p.queuedMs()+blockMs>Impl::maxQueuedMs){p.wake.notify_one();return true;}
    }
    Impl::Chunk c;c.bytes.resize(bytes);memcpy(c.bytes.data(),data,bytes);c.pts=pts;c.discontinuity=discontinuity;
    p.inputBytes+=bytes;p.input.push_back(std::move(c));p.queueChanged();p.wake.notify_one();return true;
}
void CaptureAudioSession::videoPresented(double pts,int64_t time,std::optional<int64_t> arrival){std::lock_guard lock(p_->mutex);p_->videoPts=pts;p_->videoHost=double(time)/10000;p_->videoArrival=arrival;++p_->videoObservation;p_->haveVideo=true;}
void CaptureAudioSession::videoReset(bool resetAudio){std::lock_guard lock(p_->mutex);p_->haveVideo=false;if(resetAudio&&p_->mode==0)p_->pendingReset=true;p_->wake.notify_all();}
void CaptureAudioSession::setGain(float value){p_->gain=std::clamp(value,0.0f,1.0f);}
void CaptureAudioSession::setSync(unsigned mode,int offset){p_->mode=std::min(mode,2u);p_->offset=std::clamp(offset,-250,250);}
CaptureAudioState CaptureAudioSession::snapshot()const{std::lock_guard lock(p_->mutex);return p_->state;}
}
