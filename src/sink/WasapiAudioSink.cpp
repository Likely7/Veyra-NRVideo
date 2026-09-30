#include "veyra/sink/AudioGain.h"
#include "veyra/sink/WasapiAudioSink.h"
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#pragma comment(lib, "propsys.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

namespace veyra::sink {

AudioPipeline::AudioPipeline()
    : packet_(av_packet_alloc())
{
}

AudioPipeline::~AudioPipeline()
{
    stopThread();
    closeAll();
}

bool AudioPipeline::open(const std::wstring& path)
{
    const int length=WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
    std::string narrow(length,'\0');
    WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),narrow.data(),length,nullptr,nullptr);
    if (avformat_open_input(&fmt_, narrow.c_str(), nullptr, nullptr) != 0) return false;
    if (avformat_find_stream_info(fmt_, nullptr) < 0) return false;
    tracks_.clear();
    for(unsigned i=0;i<fmt_->nb_streams;++i){
        const auto* stream=fmt_->streams[i];const auto* p=stream->codecpar;
        if(p->codec_type!=AVMEDIA_TYPE_AUDIO)continue;
        const auto* language=av_dict_get(stream->metadata,"language",nullptr,0);
        const auto* title=av_dict_get(stream->metadata,"title",nullptr,0);
        tracks_.push_back({int(i),language?language->value:"",title?title->value:"",avcodec_get_name(p->codec_id),unsigned(p->ch_layout.nb_channels)});
    }
    const AVCodec* codec = nullptr;
    const int si = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (si < 0 || codec == nullptr) {
        veyra::log::info("audio", "no audio stream in source");
        return false;
    }
    return selectTrack(si);
}

bool AudioPipeline::selectTrack(int si)
{
    if(thread_.joinable()||!fmt_||si<0||unsigned(si)>=fmt_->nb_streams)return false;
    const auto* parameters=fmt_->streams[si]->codecpar;
    if(parameters->codec_type!=AVMEDIA_TYPE_AUDIO)return false;
    const auto* codec=avcodec_find_decoder(parameters->codec_id);
    if(!codec)return false;
    auto* candidate=avcodec_alloc_context3(codec);
    if(!candidate)return false;
    int result=avcodec_parameters_to_context(candidate,parameters);
    if(result>=0)result=avcodec_open2(candidate,codec,nullptr);
    AudioFormat format;
    const auto& layout=candidate->ch_layout;
    if(layout.order==AV_CHANNEL_ORDER_NATIVE&&layout.u.mask<=UINT32_MAX)format={unsigned(layout.nb_channels),uint32_t(layout.u.mask)};
    else if(layout.nb_channels==1)format={1,SPEAKER_FRONT_CENTER};
    else if(layout.nb_channels!=2)result=AVERROR(EINVAL);
    if(result<0||!format.valid()){
        log::error("audio-track",std::format("decoder rejected stream={} code={}",si,result));
        avcodec_free_context(&candidate);return false;
    }
    avcodec_free_context(&codecCtx_);codecCtx_=candidate;streamIndex_=si;stream_=fmt_->streams[si];pcmFormat_=format;
    if(swr_)swr_free(&swr_);
    av_packet_unref(packet_);havePacket_=demuxEof_=drainSent_=false;decodedEof_=false;
    {std::lock_guard lock(mutex_);ring_.clear();segments_.clear();ringFrames_=0;}
    log::info("audio-format",std::format("file input channels={} mask=0x{:X}; preserve to renderer",pcmFormat_.channels,pcmFormat_.mask));
    veyra::log::info("audio", std::format("audio stream idx={} codec={} rate={}",
        si, codec->name, codecCtx_->sample_rate));
    return true;
}

double AudioPipeline::bufferedMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return 1000.0 * static_cast<double>(ringFrames_) / kAudioRate;
}

double AudioPipeline::headPtsMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (segments_.empty()) return -1.0;
    return segments_.front().startPtsMs;
}

double AudioPipeline::tailPtsMs() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (segments_.empty()) return -1.0;
    const Segment& t = segments_.back();
    return t.startPtsMs + 1000.0 * static_cast<double>(t.frames) / kAudioRate;
}

uint64_t AudioPipeline::underruns() const { return underruns_.load(); }
uint64_t AudioPipeline::overruns() const { return overruns_.load(); } // must stay 0
uint64_t AudioPipeline::seekCount() const { return seekCount_.load(); }
double AudioPipeline::lastPrefillMs() const { return lastPrefillMs_.load(); }
double AudioPipeline::firstPtsAfterLastSeek() const { return firstPtsAfterSeek_.load(); }

size_t AudioPipeline::pull(float* dst, size_t maxFrames, double* firstPtsMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (firstPtsMs) *firstPtsMs = segments_.empty() ? -1.0 : segments_.front().startPtsMs;
    const size_t take = std::min(maxFrames, ringFrames_);
    size_t copied = 0;
    while (copied < take && !segments_.empty()) {
        Segment& seg = segments_.front();
        const size_t n = std::min(take - copied, seg.frames);
        // Copy n interleaved frames, retaining every source channel.
        for (size_t i = 0; i < n * pcmFormat_.channels; ++i) {
            dst[copied * pcmFormat_.channels + i] = ring_.front();
            ring_.pop_front();
        }
        copied += n;
        seg.frames -= n;
        seg.startPtsMs += 1000.0 * static_cast<double>(n) / kAudioRate;
        if (seg.frames == 0) segments_.pop_front();
    }
    ringFrames_ -= take;
    return take;
}

void AudioPipeline::stopThread()
{
    stopFlag_ = true;
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

double AudioPipeline::requestSeek(double targetMs)
{
    if(videoSync_)holdForVideo();
    std::unique_lock<std::mutex> lock(mutex_);
    seekTargetMs_ = targetMs;
    seekDone_ = false;
    seekRequested_ = true;
    wake_.notify_all();
    // The audio thread signals when the ring is prefilled past target.
    if (!seekDoneCv_.wait_for(lock, std::chrono::milliseconds(3000), [this] { return seekDone_; })) {
        veyra::log::error("audio", "seek prefill timed out");
    }
    return seekDone_ ? firstPtsAfterSeek_.load() : std::numeric_limits<double>::quiet_NaN();
}

void AudioPipeline::pushDecoded(const AVFrame* frame)
{
    // Fail closed on an unannounced format change instead of reusing an old
    // channel stride/rate and reading the wrong planes. Seek clears swr_.
    const auto& layout=frame->ch_layout;
    const bool layoutMatches=layout.nb_channels==int(pcmFormat_.channels)&&
        ((layout.order==AV_CHANNEL_ORDER_NATIVE&&layout.u.mask==pcmFormat_.mask)||
         (layout.order==AV_CHANNEL_ORDER_UNSPEC&&layout.nb_channels<=2));
    if(!layoutMatches||frame->sample_rate<=0){
        log::error("audio-format","Decoded audio channel layout changed; reopen source required");
        stopFlag_=true;return;
    }
    if(swr_&&(frame->sample_rate!=swrInputRate_||frame->format!=swrInputFormat_)){
        // Sample rate / sample format changes (common at chapter or ad
        // boundaries) rebuild the resampler instead of going silent
        // (sweep 2026-09-22 C7). The output layout is unchanged.
        log::info("audio-format",std::format("decoded audio rate/format changed {}Hz/{} -> {}Hz/{}; rebuilding resampler",swrInputRate_,int(swrInputFormat_),frame->sample_rate,frame->format));
        swr_free(&swr_);swr_=nullptr;
    }
    // Convert to 48 kHz float in converted_, preserving the speaker layout.
    if (swr_ == nullptr) {
        AVChannelLayout outLayout{};av_channel_layout_from_mask(&outLayout,pcmFormat_.mask);
        AVChannelLayout inLayout = frame->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
            ? frame->ch_layout : outLayout;
        swrInputRate_=frame->sample_rate;swrInputFormat_=frame->format;
        const int result = swr_alloc_set_opts2(&swr_,
            &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate,
            &inLayout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
            0, nullptr);
        if (result < 0 || swr_ == nullptr || swr_init(swr_) < 0) {
            log::error("audio", "resampler initialization failed");
            return;
        }
    }
    // Output begins at this input PTS minus the samples retained by swr.
    // Tagging it with the input block end shifts audio by one whole block.
    const auto timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
        ? frame->best_effort_timestamp : frame->pts;
    if (timestamp != AV_NOPTS_VALUE) {
        const double ptsMs = timestamp * av_q2d(stream_->time_base) * 1000.0;
        nextPtsMs_ = ptsMs - 1000.0 * swr_get_delay(swr_, frame->sample_rate) / frame->sample_rate;
    }
    const auto capacity = swr_get_out_samples(swr_, frame->nb_samples);
    if (capacity <= 0 || capacity > static_cast<int>(kMaxRingFrames)) {
        log::error("audio", "decoded audio block exceeds bounded conversion capacity");
        return;
    }
    converted_.resize(static_cast<size_t>(capacity) * pcmFormat_.channels);
    uint8_t* planes[1] = { reinterpret_cast<uint8_t*>(converted_.data()) };
    const int outSamples = swr_convert(swr_, planes,
        static_cast<int>(converted_.size() / pcmFormat_.channels),
        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    if (outSamples < 0) { log::error("audio", std::format("swr_convert failed code={}", outSamples)); return; }
    pushConverted(outSamples);
}

void AudioPipeline::holdForVideo()
{
    if(!videoHold_.exchange(true))log::info("audio-continuity",std::format("event=hold coverageMs={:.3f}",std::isfinite(videoLimitMs_.load())?videoLimitMs_.load():-1.0));
    videoSync_=true;wake_.notify_all();
}

void AudioPipeline::videoReady(double ptsMs)
{
    // Initial/reset prefill must wait for an actual Present, not GPU readiness.
    if(videoSync_&&!videoHold_&&std::isfinite(ptsMs))
        videoLimitMs_=std::max(videoLimitMs_.load(),ptsMs+2.0);
    wake_.notify_all();
}

void AudioPipeline::videoPresented(double nextPtsMs)
{
    if(!videoSync_||!std::isfinite(nextPtsMs))return;
    const bool released=videoHold_.exchange(false);
    videoLimitMs_=nextPtsMs;
    if(released)log::info("audio-continuity",std::format("event=release coverageMs={:.3f}",nextPtsMs));
    wake_.notify_all();
}

void AudioPipeline::pushConverted(int frames)
{
    if (frames <= 0) return;
    size_t skip = 0;
    if (discardUntilPtsMs_ >= 0 && nextPtsMs_ < discardUntilPtsMs_) {
        skip = std::min(static_cast<size_t>(frames), static_cast<size_t>(
            std::ceil((discardUntilPtsMs_ - nextPtsMs_) * kAudioRate / 1000.0 - 1e-7)));
    }
    nextPtsMs_ += 1000.0 * skip / kAudioRate;
    const size_t addFrames = static_cast<size_t>(frames) - skip;
    if (!addFrames) return;

    std::unique_lock<std::mutex> lock(mutex_);
    if (ringFrames_ + addFrames > kMaxRingFrames) {
        // Player mode: never drop. This is a hard error (bounded ring
        // should never overflow because production is watermarked).
        overruns_.fetch_add(1);
        veyra::log::error("audio", "ring overflow despite watermarks (bug)");
        return;
    }
    ring_.insert(ring_.end(), converted_.data() + skip * pcmFormat_.channels, converted_.data() + static_cast<size_t>(frames) * pcmFormat_.channels);
    ringFrames_ += addFrames;
    if (!segments_.empty()) {
        Segment& last = segments_.back();
        const double lastEnd = last.startPtsMs + 1000.0 * static_cast<double>(last.frames) / kAudioRate;
        if (std::fabs(lastEnd - nextPtsMs_) < 1.0) {
            last.frames += addFrames;      // contiguous: extend
        } else {
            segments_.push_back({ nextPtsMs_, addFrames });
        }
    } else {
        segments_.push_back({ nextPtsMs_, addFrames });
    }
    nextPtsMs_ += 1000.0 * static_cast<double>(addFrames) / kAudioRate;
}

void AudioPipeline::decodeBlock()
{
    AVFrame* frame = av_frame_alloc();
    if (!frame) return;
    // Always receive pending frames before sending another packet. EAGAIN
    // leaves the packet owned here until the decoder actually accepts it.
    while (bufferedMs() < kAudioHighWatermarkMs && !stopFlag_ && !seekRequested_ && !decodedEof_) {
        const int received = avcodec_receive_frame(codecCtx_, frame);
        if (received == 0) {
            pushDecoded(frame);
            av_frame_unref(frame);
            continue;
        }
        if (received == AVERROR_EOF) {
            if (swr_) {
                uint8_t* planes[] = {reinterpret_cast<uint8_t*>(converted_.data())};
                const int count = swr_convert(swr_, planes, static_cast<int>(converted_.size() / pcmFormat_.channels), nullptr, 0);
                if (count > 0) { pushConverted(count); continue; }
                if (count < 0) log::error("audio", std::format("resampler drain failed code={}", count));
            }
            decodedEof_ = true;
            break;
        }
        if (received != AVERROR(EAGAIN)) {
            log::error("audio", std::format("decode receive failed code={}", received));
            decodedEof_ = true;
            break;
        }
        if (demuxEof_) {
            if (drainSent_) { log::error("audio", "decoder requested input after drain"); decodedEof_ = true; break; }
            const int sent = avcodec_send_packet(codecCtx_, nullptr);
            if (sent < 0 && sent != AVERROR_EOF) { log::error("audio", std::format("decoder drain failed code={}", sent)); break; }
            drainSent_ = true;
            continue;
        }
        if (!havePacket_) {
            if (av_read_frame(fmt_, packet_) < 0) {
                demuxEof_ = true;
                continue;
            }
            havePacket_ = true;
            if (packet_->stream_index != streamIndex_) {
                av_packet_unref(packet_);
                havePacket_ = false;
                continue;
            }
        }
        const int sent = avcodec_send_packet(codecCtx_, packet_);
        if (sent == 0) {
            av_packet_unref(packet_);
            havePacket_ = false;
        } else if (sent != AVERROR(EAGAIN)) {
            log::error("audio", std::format("decode send failed code={}", sent));
            decodedEof_ = true;
            break;
        }
    }
    av_frame_free(&frame);
}

void AudioPipeline::closeAll()
{
    if (swr_ != nullptr) swr_free(&swr_);
    if (packet_ != nullptr) av_packet_free(&packet_);
    if (codecCtx_ != nullptr) avcodec_free_context(&codecCtx_);
    if (fmt_ != nullptr) avformat_close_input(&fmt_);
}

bool AudioRenderer::copyPcm(BYTE* destination,const float* input,size_t frames){
    if(!channelMix_){std::memcpy(destination,input,frames*outputFormat_.channels*sizeof(float));return true;}
    mixed_.resize(frames*outputFormat_.channels);
    uint8_t* out[]={reinterpret_cast<uint8_t*>(mixed_.data())};const uint8_t* in[]={reinterpret_cast<const uint8_t*>(input)};
    const int converted=swr_convert(channelMix_,out,int(frames),in,int(frames));
    if(converted!=int(frames))return checked(E_FAIL,"Channel mapping unexpectedly changed frame count");
    std::memcpy(destination,mixed_.data(),frames*outputFormat_.channels*sizeof(float));return true;
}

namespace {
std::mutex gEndpointMutex;
std::wstring gPreferredEndpoint;
bool gForceStereo = false;
ActiveRenderEndpoint gActiveEndpoint;
std::atomic<uint64_t> gEndpointGeneration{1};
std::wstring endpointName(IMMDevice* device){
    std::wstring name;IPropertyStore* store=nullptr;
    if(device&&SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store))){
        PROPVARIANT value;PropVariantInit(&value);
        if(SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)name=value.pwszVal;
        PropVariantClear(&value);store->Release();
    }
    return name;
}
std::wstring endpointId(IMMDevice* device){
    std::wstring id;LPWSTR raw=nullptr;
    if(device&&SUCCEEDED(device->GetId(&raw))&&raw){id=raw;CoTaskMemFree(raw);}
    return id;
}
std::string narrowName(const std::wstring& w){
    if(w.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(std::max(n,0)),'\0');
    if(n>0)WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),out.data(),n,nullptr,nullptr);
    return out;
}
}
std::vector<RenderEndpoint> enumerateRenderEndpoints(){
    std::vector<RenderEndpoint> out;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator)))){
        std::wstring defaultId;IMMDevice* fallback=nullptr;
        if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&fallback))){defaultId=endpointId(fallback);fallback->Release();}
        IMMDeviceCollection* devices=nullptr;
        if(SUCCEEDED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))){
            UINT count=0;devices->GetCount(&count);
            for(UINT i=0;i<count;++i){
                IMMDevice* device=nullptr;
                if(FAILED(devices->Item(i,&device)))continue;
                RenderEndpoint e;e.id=endpointId(device);e.name=endpointName(device);e.isDefault=e.id==defaultId;
                device->Release();
                if(!e.id.empty())out.push_back(std::move(e));
            }
            devices->Release();
        }
        enumerator->Release();
    }
    if(SUCCEEDED(com))CoUninitialize();
    return out;
}
void setPreferredRenderEndpoint(std::wstring id){
    std::lock_guard lock(gEndpointMutex);
    if(gPreferredEndpoint==id)return;
    gPreferredEndpoint=std::move(id);
    gEndpointGeneration.fetch_add(1);
    log::info("audio-endpoint",std::format("preferred endpoint set id={}",gPreferredEndpoint.empty()?std::string("<system default>"):narrowName(gPreferredEndpoint)));
}
std::wstring preferredRenderEndpoint(){std::lock_guard lock(gEndpointMutex);return gPreferredEndpoint;}
void setForceStereoDownmix(bool enabled){
    std::lock_guard lock(gEndpointMutex);
    if(gForceStereo==enabled)return;
    gForceStereo=enabled;gEndpointGeneration.fetch_add(1);
    log::info("audio-endpoint",std::format("force stereo downmix={}",enabled));
}
bool forceStereoDownmix(){std::lock_guard lock(gEndpointMutex);return gForceStereo;}
ActiveRenderEndpoint activeRenderEndpoint(){std::lock_guard lock(gEndpointMutex);return gActiveEndpoint;}
uint64_t renderEndpointGeneration(){return gEndpointGeneration.load();}

struct AudioRenderer::EndpointNotifier : IMMNotificationClient {
    std::atomic<HRESULT>* error;std::atomic<unsigned long> refs{1};
    AudioRenderer* owner=nullptr;
    explicit EndpointNotifier(std::atomic<HRESULT>* target,AudioRenderer* renderer):error(target),owner(renderer){}
    void invalidate(const char* why){
        error->store(AUDCLNT_E_DEVICE_INVALIDATED);
        log::info("audio",std::format("{}; scheduling endpoint rebuild",why));
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ULONG(++refs);}
    ULONG STDMETHODCALLTYPE Release()override{const ULONG n=ULONG(--refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** pp)override{if(!pp)return E_POINTER;if(id==__uuidof(IUnknown)||id==__uuidof(IMMNotificationClient)){*pp=this;AddRef();return S_OK;}*pp=nullptr;return E_NOINTERFACE;}
    // The chosen device going away drops to the default; its return switches back.
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id,DWORD state)override{
        if(!id||!owner)return S_OK;
        if(!owner->followsDefault_.load()&&owner->deviceId_==id&&state!=DEVICE_STATE_ACTIVE)invalidate("chosen render endpoint left the active state");
        else if(owner->onFallback_.load()&&owner->preferredId_==id&&state==DEVICE_STATE_ACTIVE)invalidate("chosen render endpoint is back");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id)override{
        if(id&&owner&&owner->onFallback_.load()&&owner->preferredId_==id)invalidate("chosen render endpoint was added");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id)override{
        if(id&&owner&&!owner->followsDefault_.load()&&owner->deviceId_==id)invalidate("chosen render endpoint was removed");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR)override{
        if(flow==eRender&&role==eConsole&&(!owner||owner->followsDefault_.load())){
            // Same code the write path reports when the device disappears; the
            // pipeline thread treats it as a recoverable endpoint loss.
            error->store(AUDCLNT_E_DEVICE_INVALIDATED);
            log::info("audio","default render endpoint changed; scheduling endpoint rebuild");
        }
        return S_OK;
    }
};
void AudioRenderer::registerEndpointNotification(){
    if(!enum_||notifier_)return;
    notifier_=new EndpointNotifier(&lastError_,this);
    const HRESULT hr=enum_->RegisterEndpointNotificationCallback(notifier_);
    if(FAILED(hr)){log::warn("audio",std::format("RegisterEndpointNotificationCallback hr=0x{:08X}; device changes recover on the next write error",unsigned(hr)));notifier_->Release();notifier_=nullptr;}
}
void AudioRenderer::unregisterEndpointNotification(){
    if(!notifier_)return;
    if(enum_)enum_->UnregisterEndpointNotificationCallback(notifier_);
    notifier_->Release();notifier_=nullptr;
}
bool AudioRenderer::start(AudioFormat input,double requestedBufferMs)
{
    std::lock_guard endpointLock(endpointMutex_);
    if(client_||enum_)return checked(E_UNEXPECTED,"Endpoint already initialized");
    lastError_=S_OK;bufferedMs_=0;smoothedGain_=0;fadeInRemaining_=0;underruns_=0;underrunFrames_=0;silenceFrames_=0;
    liveGap_.reset();
    emptyPulls_=0;recoveryFades_=0;
    clockStalledGaps_=0;
    if(!input.valid())return checked(E_INVALIDARG,"Invalid input channel layout");
    inputFormat_=input;lastRaw_.assign(input.channels,0);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInited_ = SUCCEEDED(hr);
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return checked(hr,"Initialize COM");
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enum_));
    if (!checked(hr,"Create MMDeviceEnumerator")) return false;
    // Read the choice and its generation together, so a change made while this
    // endpoint opens is seen as stale on the next check and rebuilds again.
    bool stereo=false;
    {std::lock_guard lock(gEndpointMutex);preferredId_=gPreferredEndpoint;stereo=gForceStereo;endpointGeneration_.store(gEndpointGeneration.load());}
    followsDefault_=true;onFallback_=false;
    if(!preferredId_.empty()){
        IMMDevice* chosen=nullptr;DWORD state=0;
        if(SUCCEEDED(enum_->GetDevice(preferredId_.c_str(),&chosen))&&SUCCEEDED(chosen->GetState(&state))&&state==DEVICE_STATE_ACTIVE){
            device_=chosen;followsDefault_=false;
        }else{
            if(chosen)chosen->Release();
            onFallback_=true;
            log::warn("audio-endpoint","chosen render endpoint unavailable; using the system default until it returns");
        }
    }
    if(!device_){
        hr = enum_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (!checked(hr,"GetDefaultAudioEndpoint")) return false;
    }
    deviceId_=endpointId(device_);
    const std::wstring deviceName=endpointName(device_);
    registerEndpointNotification();
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&client_));
    if (!checked(hr,"Activate AudioClient")) return false;
    WAVEFORMATEX* deviceMix=nullptr;
    if(!checked(client_->GetMixFormat(&deviceMix),"GetMixFormat"))return false;
    WavePcmFormat deviceFormat;const bool known=parseWavePcm(deviceMix,sizeof(WAVEFORMATEX)+deviceMix->cbSize,deviceFormat);
    outputFormat_=input;
    if(!known||(deviceFormat.layout.mask&input.mask)!=input.mask)
        outputFormat_=known?deviceFormat.layout:AudioFormat{};
    // Forced stereo: the engine's own swr downmix, then Windows maps stereo onto
    // the device (AUTOCONVERTPCM), exactly as for any stereo source.
    if(stereo&&input.channels>2)outputFormat_=AudioFormat{};
    CoTaskMemFree(deviceMix);
    {
        std::lock_guard lock(gEndpointMutex);
        gActiveEndpoint={deviceId_,deviceName,onFallback_.load(),outputFormat_.channels,outputFormat_.channels<input.channels,true};
    }
    log::info("audio-endpoint",std::format("opened name={} chosen={} fallback={} forceStereo={}",
        narrowName(deviceName),!followsDefault_.load(),onFallback_.load(),stereo));
    auto mix=floatWave(outputFormat_);
    if(outputFormat_!=inputFormat_){
        AVChannelLayout from{},to{};av_channel_layout_from_mask(&from,inputFormat_.mask);av_channel_layout_from_mask(&to,outputFormat_.mask);
        const int result=swr_alloc_set_opts2(&channelMix_,&to,AV_SAMPLE_FMT_FLT,kAudioRate,&from,AV_SAMPLE_FMT_FLT,kAudioRate,0,nullptr);
        av_channel_layout_uninit(&from);av_channel_layout_uninit(&to);
        if(result<0||!channelMix_||swr_init(channelMix_)<0)return checked(E_FAIL,"Configure endpoint channel mapping");
    }
    log::info("audio-format",std::format("input={} mask=0x{:X} output={} mask=0x{:X} downmix={} deviceLayoutKnown={}",input.channels,input.mask,outputFormat_.channels,outputFormat_.mask,outputFormat_.channels<input.channels,known));
    sampleRate_ = kAudioRate;
    requestedBufferMs=std::clamp(requestedBufferMs,5.0,50.0);
    const REFERENCE_TIME requestedBufferHns=static_cast<REFERENCE_TIME>(std::llround(requestedBufferMs*10000.0));
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        requestedBufferHns, 0, &mix.Format, nullptr);
    const bool initOk = checked(hr,"Initialize AudioClient");
    if (!initOk) return false;
    if (!checked(client_->GetBufferSize(&bufferFrames_),"GetBufferSize")) return false;
    REFERENCE_TIME devicePeriod=0;
    if(!checked(client_->GetDevicePeriod(&devicePeriod,nullptr),"GetDevicePeriod"))return false;
    devicePeriodMs_=std::max(.1,double(devicePeriod)/10000.0);
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if(event_==nullptr)return checked(HRESULT_FROM_WIN32(GetLastError()),"CreateEvent");
    if(!checked(client_->SetEventHandle(event_),"SetEventHandle"))return false;
    if (!checked(client_->GetService(__uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&render_)),"Get RenderClient")) return false;
    if (!checked(client_->GetService(__uuidof(IAudioClock),
            reinterpret_cast<void**>(&clock_)),"Get AudioClock")) return false;
    UINT64 freq = 0;
    if(!checked(clock_->GetFrequency(&freq),"GetFrequency"))return false;
    if(!freq)return checked(E_UNEXPECTED,"Zero audio clock frequency");
    clockFrequency_=freq;
    running_ = true;
    veyra::log::info("audio", std::format("renderer opened {}Hz event-mode requestedBufferMs={:.1f} actualBufferMs={:.3f} buffer={} frames (not started; prefill first)",
        sampleRate_,requestedBufferMs,1000.0*bufferFrames_/sampleRate_,bufferFrames_));
    return true;
}

bool AudioRenderer::startAnchored(AudioPcmSource& pipeline, bool paused)
{
    std::lock_guard endpointLock(endpointMutex_);
    double firstBufferPtsMs = -1;
    if (!pumpOnce(pipeline, &firstBufferPtsMs) || framesWritten_ == 0) return false;
    UINT64 pos = 0, qpc = 0;
    if (!checked(clock_->GetPosition(&pos, &qpc),"Anchor GetPosition") || !clockFrequency_) return false;
    anchorPos_ = pos;
    anchorPtsMs_.store(firstBufferPtsMs);
    if (!paused && !checked(client_->Start(),"Start")) return false;
    pausedEndpoint_=paused;
    started_ = true;
    veyra::log::info("audio", std::format("renderer ANCHORED ptsMs={:.1f} devicePos={} freq={} paused={}",
        firstBufferPtsMs, pos, clockFrequency_,paused));
    return true;
}

bool AudioRenderer::waitForEvent()
{
    if(!running_)return false;
    if(started_){const auto wait=WaitForSingleObject(event_,50);if(wait==WAIT_FAILED)return checked(HRESULT_FROM_WIN32(GetLastError()),"Wait audio event");}
    return true;
}

bool AudioRenderer::pumpOnce(AudioPcmSource& pipeline, double* firstWrittenPtsMs, bool wait)
{
    if (!running_) return false;
    if(wait&&!waitForEvent())return false;
    UINT32 padding = 0;
    if (!checked(client_->GetCurrentPadding(&padding),"GetCurrentPadding")) return false;
    if(padding>bufferFrames_)return checked(E_UNEXPECTED,"Invalid endpoint padding");
    bufferedMs_=1000.0*padding/sampleRate_;
    const UINT32 avail = bufferFrames_ - padding;
    if (avail == 0) return true;
    // The endpoint can play silence after a live source runs dry. Its device
    // clock may then pass our last write; leave that gap unmapped.
    if(started_&&!padding&&!pipeline.padUnderruns()){
        UINT64 pos=0,qpc=0;
        if(!checked(clock_->GetPosition(&pos,&qpc),"Live write GetPosition"))return false;
        const auto consumed=static_cast<uint64_t>(double(pos-anchorPos_)*sampleRate_/clockFrequency_);
        const auto gap=liveGap_.observe(padding,consumed,timelineWriteFrame_.load());
        if(gap.frames){
            underrunFrames_.fetch_add(gap.frames);
            if(gap.began){underruns_.fetch_add(1);fadeInRemaining_=240;recoveryFades_.fetch_add(1);}
        }
        timelineWriteFrame_=std::max(timelineWriteFrame_.load(),consumed);
    }
    if(pipeline.pcmFormat()!=inputFormat_)return checked(E_INVALIDARG,"PCM source channel layout changed without reset");
    chunk_.resize(static_cast<size_t>(avail) * inputFormat_.channels);
    BYTE* dest = nullptr;
    if (!checked(render_->GetBuffer(avail, &dest),"GetBuffer")) return false;
    double firstPts = -1.0;
    const size_t got = pipeline.pull(chunk_.data(), avail, &firstPts);
    const auto endPts=pipeline.lastPullEndPtsMs();
    if (firstWrittenPtsMs) *firstWrittenPtsMs = firstPts;
    if (!started_ && !got) return checked(render_->ReleaseBuffer(0, 0),"Release empty prefill");
    // File playback owns a continuous media timeline, so it may explicitly
    // fill an empty read with silence. Live capture must not invent media
    // frames for a short gap: doing so advances the timeline with synthetic
    // audio and creates a clock jump when the next real block arrives. The
    // endpoint remains silent when ReleaseBuffer(0) is used; the capture
    // owner applies the bounded fade/re-anchor policy only for a persistent
    // starvation.
    const bool padSilence=started_&&pipeline.padUnderruns();
    const UINT32 written=padSilence?avail:static_cast<UINT32>(got);
    if(started_&&!pausedEndpoint_&&!padSilence){
        const double nowMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(liveGap_.observeEmpty(padding,got,nowMs,devicePeriodMs_)){
            underruns_.fetch_add(1);clockStalledGaps_.fetch_add(1);
            fadeInRemaining_=240;recoveryFades_.fetch_add(1);
            log::warn("audio-gap",std::format("persistent empty endpoint periodMs={:.3f} clockMeasuredMissingFrames=unknown recoveryFadeArmed=true",devicePeriodMs_));
        }
    }
    if (got > 0) {
        for(unsigned c=0;c<inputFormat_.channels;++c)lastRaw_[c]=chunk_[(got-1)*inputFormat_.channels+c];
        const float target=std::clamp(gain_.load(),0.0f,1.0f);
        applyPcmGain(chunk_.data(),got,inputFormat_.channels,target,smoothedGain_);
        if(fadeInRemaining_)fadePcmHead(chunk_.data(),got,inputFormat_.channels,fadeInRemaining_);
        if(target!=loggedGain_&&std::abs(smoothedGain_-target)<.00001f){loggedGain_=target;log::info("audio-gain",std::format("target={} reached={} framesWritten={} clockPreserved=true applicationPCM=true",target,smoothedGain_,framesWritten_.load()));}
        if(!copyPcm(dest,chunk_.data(),got)){render_->ReleaseBuffer(0,0);return false;}
        if (got < written) {
            std::memset(dest + got * outputFormat_.channels*4, 0, (written - got) * outputFormat_.channels*4);
            underruns_.fetch_add(1);
            underrunFrames_.fetch_add(written-got);
            silenceFrames_.fetch_add(written-got);
        }
    } else {
        std::memset(dest, 0, static_cast<size_t>(written) * outputFormat_.channels*4);
        if(started_&&!padSilence)emptyPulls_.fetch_add(1);
        if(padSilence){
            underruns_.fetch_add(1);
            underrunFrames_.fetch_add(written);
            silenceFrames_.fetch_add(written);
        }
    }
    // ReleaseBuffer invalidates dest. The same gain/mapped samples remain in
    // our owned staging storage for the source-specific diagnostic observer.
    if (!checked(render_->ReleaseBuffer(written, 0),"ReleaseBuffer")) return false;
    if(got)pipeline.observeRenderedPcm(channelMix_?mixed_.data():chunk_.data(),got,outputFormat_.channels,padding);
    liveGap_.submitted(got);
    if(written>got)std::fill(lastRaw_.begin(),lastRaw_.end(),0);
    {
        std::lock_guard lock(timelineMutex_);
        if(got&&endPts){timedPcm_=true;outputTimeline_.append(timelineWriteFrame_,got,firstPts,*endPts);}
        outputTimeline_.discardBefore(timelineWriteFrame_>sampleRate_?timelineWriteFrame_-sampleRate_:0);
        timelineWriteFrame_+=written;
    }
    bufferedMs_=1000.0*(padding+written)/sampleRate_;
    framesWritten_ += written;
    return true;
}

double AudioRenderer::mediaTimeMs() const
{
    std::lock_guard endpointLock(endpointMutex_);
    if (!running_ || !started_ || fading_) return std::numeric_limits<double>::quiet_NaN();
    UINT64 pos = 0, qpc = 0;
    if (!checked(clock_->GetPosition(&pos, &qpc),"Clock GetPosition") || clockFrequency_ == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double consumedMs = 1000.0 * static_cast<double>(pos - anchorPos_) /
        static_cast<double>(clockFrequency_);
    {std::lock_guard lock(timelineMutex_);if(timedPcm_){
        const double frame=consumedMs*sampleRate_/1000;
        if(frame>=timelineWriteFrame_)return std::numeric_limits<double>::quiet_NaN();
        return outputTimeline_.at(frame).value_or(std::numeric_limits<double>::quiet_NaN());
    }}
    return anchorPtsMs_.load() + consumedMs;
}

void AudioRenderer::stopAndReset()
{
    std::lock_guard endpointLock(endpointMutex_);
    started_ = false;
    if (client_) { checked(client_->Stop(),"Stop for reset");checked(client_->Reset(),"Reset"); }
    framesWritten_ = 0;
    timelineWriteFrame_=0;
    bufferedMs_=0;smoothedGain_=0;
    {std::lock_guard lock(timelineMutex_);outputTimeline_.clear();timedPcm_=false;}
    fading_=false;pausedEndpoint_=false;fadeInRemaining_=0;liveGap_.reset();std::fill(lastRaw_.begin(),lastRaw_.end(),0);
}

AudioFadeResult AudioRenderer::fadeAndReset(AudioPcmSource& source,const std::atomic<bool>& cancel)
{
    if(!running_||!started_||pausedEndpoint_||FAILED(lastError_)){
        stopAndReset();return AudioFadeResult::NotPlaying;
    }
    fading_=true;
    const auto begin=std::chrono::steady_clock::now();
    constexpr UINT32 frames=kAudioRate/200;
    std::vector<float> tail(size_t(frames)*inputFormat_.channels);
    bool submitted=false;AudioFadeResult result=AudioFadeResult::TimedOut;
    while(std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(80)){
        if(cancel){result=AudioFadeResult::Cancelled;break;}
        UINT32 padding=0;
        if(!checked(client_->GetCurrentPadding(&padding),"Fade GetCurrentPadding")){result=AudioFadeResult::Failed;break;}
        if(padding>bufferFrames_){checked(E_UNEXPECTED,"Fade invalid padding");result=AudioFadeResult::Failed;break;}
        if(submitted&&!padding){result=AudioFadeResult::Drained;break;}
        if(!submitted&&bufferFrames_-padding>=frames){
            BYTE* dest=nullptr;
            if(!checked(render_->GetBuffer(frames,&dest),"Fade GetBuffer")){result=AudioFadeResult::Failed;break;}
            double pts=-1;const size_t got=source.pull(tail.data(),frames,&pts);
            for(unsigned c=0;c<inputFormat_.channels;++c){const float last=got?tail[(got-1)*inputFormat_.channels+c]:(padding?lastRaw_[c]:0);for(size_t i=got;i<frames;++i)tail[i*inputFormat_.channels+c]=last;}
            fadePcmTail(tail.data(),frames,inputFormat_.channels,smoothedGain_);
            if(!copyPcm(dest,tail.data(),frames)){render_->ReleaseBuffer(0,0);result=AudioFadeResult::Failed;break;}
            if(!checked(render_->ReleaseBuffer(frames,0),"Fade ReleaseBuffer")){result=AudioFadeResult::Failed;break;}
            submitted=true;framesWritten_+=frames;
            log::info("audio-fade",std::format("submitted frames={} realPcm={} finalLeft={} finalRight={}",frames,got,tail[tail.size()-inputFormat_.channels],tail.back()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    log::info("audio-fade",std::format("result={} submitted={} durationMs={:.3f} endpoint-consumption-not-speaker-measurement",int(result),submitted,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()));
    stopAndReset();return result;
}

bool AudioRenderer::started() const { return started_; }
uint64_t AudioRenderer::underruns() const { return underruns_.load(); }
uint64_t AudioRenderer::underrunFrames() const { return underrunFrames_.load(); }
uint64_t AudioRenderer::silenceFrames() const { return silenceFrames_.load(); }
uint64_t AudioRenderer::framesWritten() const { return framesWritten_; }

void AudioRenderer::shutdown()
{
    std::lock_guard endpointLock(endpointMutex_);
    if (client_) { (void)client_->Stop(); (void)client_->Reset(); }
    #define REL(x) if (x) { x->Release(); x = nullptr; }
    unregisterEndpointNotification();
    REL(clock_); REL(render_); REL(client_); REL(device_); REL(enum_);
    swr_free(&channelMix_);
    #undef REL
    if (event_ != nullptr) { CloseHandle(event_); event_ = nullptr; }
    running_ = false;
    started_ = false;
    fading_=false;pausedEndpoint_=false;fadeInRemaining_=0;liveGap_.reset();
    bufferedMs_=0;framesWritten_=0;clockFrequency_=0;
    timelineWriteFrame_=0;
    {std::lock_guard lock(timelineMutex_);outputTimeline_.clear();timedPcm_=false;}
    if (comInited_) { CoUninitialize(); comInited_ = false; }
}

void AudioRenderer::setPaused(bool value)
{
    std::lock_guard endpointLock(endpointMutex_);
    if (!client_ || !started_) return;
    if(checked(value ? client_->Stop() : client_->Start(),"Pause/resume")){pausedEndpoint_=value;liveGap_.reset();}
}

void AudioPipeline::runOnAudioThread(AudioRenderer* renderer, bool ownEndpoint, double initialMs)
{
    const auto t0 = std::chrono::steady_clock::now();
    bool endpointReady = renderer && (!ownEndpoint || renderer->start(pcmFormat_));
    double lastClockMs = 0, recoveryTargetMs = 0;
    auto retryAt = t0;
    // Steady-state under-rate must never stop sound: the audio device clock is
    // the master timeline and slow video enhancement drops preview frames
    // instead (engine-side, plan P1). Only explicit transport holds — open,
    // seek, settings rebuild, pause — gate the endpoint here.
    auto videoBlocked=[&]{return videoSync_&&videoHold_.load();};
    auto shouldPause=[&]{return paused_.load()||videoBlocked();};
    auto publishVideoWait=[&](bool paused){const bool waiting=paused&&videoBlocked();if(videoWaiting_.exchange(waiting)!=waiting&&waiting)++videoWaitCount_;};
    auto nextContinuitySummary=t0+std::chrono::milliseconds(1000);
    auto recovering = [&](HRESULT error) {
        endpointError_ = FAILED(error) ? error : E_FAIL;
        endpointRecovering_ = true;
        recoveryTargetMs = lastClockMs;
        endpointReady = false;
        if (ownEndpoint) renderer->shutdown();
        retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        log::warn("audio-recovery",std::format("endpoint unavailable hr=0x{:08X} holdPtsMs={:.3f}",unsigned(endpointError_.load()),recoveryTargetMs));
    };
    auto rewind = [&](double target) {
        const auto seekBegin=std::chrono::steady_clock::now();
        {
            std::lock_guard lock(mutex_);
            ring_.clear();ringFrames_=0;segments_.clear();
            if(havePacket_){av_packet_unref(packet_);havePacket_=false;}
            avcodec_flush_buffers(codecCtx_);
            if(swr_)swr_free(&swr_);
            // Matroska commonly indexes only video keyframes. Seeking an audio
            // stream can use its sparse, previously observed packet index and
            // decode minutes of audio. Seek the container's video index, then
            // trim decoded PCM to the exact requested audio timestamp.
            const int video=av_find_best_stream(fmt_,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
            const int seekStream=video>=0?video:streamIndex_;
            const auto tb=fmt_->streams[seekStream]->time_base;
            const int64_t tbTarget=av_rescale_q(int64_t(target*1000),{1,1000000},tb);
            const int result=av_seek_frame(fmt_,seekStream,tbTarget,AVSEEK_FLAG_BACKWARD);
            if(result<0){log::error("audio",std::format("seek failed code={}",result));clockExhausted_=false;return false;}
            demuxEof_=drainSent_=false;decodedEof_=false;
            nextPtsMs_=target;discardUntilPtsMs_=target;
        }
        decodeBlock();
        discardUntilPtsMs_=-1;
        clockExhausted_=decodedEof_&&headPtsMs()<0;
        log::info("audio-seek",std::format("targetMs={:.3f} firstPtsMs={:.3f} totalMs={:.3f}",target,headPtsMs(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-seekBegin).count()));
        return true;
    };
    if(renderer&&ownEndpoint&&!endpointReady)recovering(renderer->lastError());
    // Initial prefill (open case).
    if(initialMs>=0){if(!rewind(initialMs))recovering(E_FAIL);}
    else decodeBlock();
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (segments_.empty()) {
            veyra::log::warn("audio", "no audio decoded at startup");
        }
    }
    const double firstPts = headPtsMs();
    const double prefetchedMs = bufferedMs();
    clockExhausted_=decodedEof_&&prefetchedMs<=0.0;
    bool pauseApplied = shouldPause();
    // A/V containers commonly give the first AAC frame a small negative PTS
    // for encoder priming (this MOV starts at about -1.3 ms).  Negative does
    // not mean "no audio"; headPtsMs() also uses -1 as its empty sentinel.
    // Use the actual prefetched sample count to distinguish those cases and
    // anchor the renderer whenever a finite audio timeline is available.
    if (endpointReady && prefetchedMs > 0.0 && std::isfinite(firstPts)) {
        if (!renderer->startAnchored(*this,pauseApplied)) recovering(renderer->lastError());
        else endpointRecovering_=false;
    }
    lastPrefillMs_.store(std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count() * 1000.0);
    veyra::log::info("audio", std::format("startup prefill done in {:.0f}ms firstPtsMs={:.1f} bufferedMs={:.0f}",
        lastPrefillMs_.load(), firstPts, bufferedMs()));

    while (!stopFlag_.load()) {
        const bool pauseNow = shouldPause();
        if(endpointReady){
            const double current=renderer->mediaTimeMs();
            if(std::isfinite(current))lastClockMs=current;
            if(ownEndpoint&&endpointLossForTest_.exchange(false)){
                log::info("audio-recovery-test","release own endpoint on its audio thread; no system device change");
                recovering(AUDCLNT_E_DEVICE_INVALIDATED);
            }else if(ownEndpoint&&FAILED(renderer->lastError()))recovering(renderer->lastError());
        }
        if (pauseNow != pauseApplied && endpointReady) renderer->setPaused(pauseNow);
        pauseApplied = pauseNow;
        publishVideoWait(pauseApplied);
        // Bounded per-second audio state: master clock, video coverage and
        // lead make steady under-rate observable without per-frame logging.
        if (videoSync_ && !pauseNow && std::chrono::steady_clock::now() >= nextContinuitySummary) {
            nextContinuitySummary = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
            const double coverage = videoLimitMs_.load();
            log::info("audio-continuity", std::format("event=summary clockMs={:.3f} coverageMs={:.3f} leadMs={:.3f} hold={} recovering={}",
                lastClockMs, std::isfinite(coverage) ? coverage : -1.0,
                std::isfinite(coverage) ? lastClockMs - coverage : -1.0,
                videoHold_.load(), endpointRecovering_.load()));
        }
        // Seek request? Atomic re-sequence.
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (seekRequested_) {
                seekRequested_ = false;
                const double target = seekTargetMs_;
                seekCount_.fetch_add(1);
                lock.unlock();
                if (endpointReady) renderer->fadeAndReset(*this,stopFlag_);
                lastClockMs=recoveryTargetMs=target;
                const bool seekOk=rewind(target);
                double startPts = seekOk?headPtsMs():std::numeric_limits<double>::quiet_NaN();
                if (seekOk&&startPts < 0.0) startPts = target;
                firstPtsAfterSeek_.store(startPts);
                if(!seekOk&&renderer)recovering(E_FAIL);
                if (seekOk&&endpointReady && !clockExhausted_) {
                    pauseApplied=shouldPause();
                    if (!renderer->startAnchored(*this,pauseApplied)) recovering(renderer->lastError());
                    // A seek can interrupt the first prefill before it clears
                    // startup recovery. Publish the newly anchored endpoint.
                    else endpointRecovering_=false;
                }
                if(seekOk&&endpointReady&&clockExhausted_)endpointRecovering_=false;
                {
                    std::lock_guard<std::mutex> l2(mutex_);
                    discardUntilPtsMs_ = -1.0;
                    seekDone_ = true;
                }
                seekDoneCv_.notify_all();
                veyra::log::info("audio", std::format("seek done targetMs={:.0f} startPtsMs={:.1f} bufferedMs={:.0f}",
                    target, startPts, bufferedMs()));
                continue;
            }
        }
        if(renderer&&ownEndpoint&&!endpointReady){
            if(clockExhausted_){endpointRecovering_=false;}
            else if(std::chrono::steady_clock::now()>=retryAt){
                if(renderer->start(pcmFormat_)){
                    endpointReady=true;
                    const bool rewound=rewind(recoveryTargetMs);
                    pauseApplied=shouldPause();
                    if(rewound&&(clockExhausted_||renderer->startAnchored(*this,pauseApplied))){
                        endpointRecovering_=false;++endpointRecoveries_;
                        log::info("audio-recovery",std::format("restored mediaPtsMs={:.3f} paused={} recoveries={}",recoveryTargetMs,pauseNow,endpointRecoveries_.load()));
                    }else recovering(renderer->lastError());
                }else recovering(renderer->lastError());
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock,std::chrono::milliseconds(5),[&]{return stopFlag_||seekRequested_;});
            continue;
        }
        if (pauseNow) { std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(2));continue; }
        // Regular cycle: pump the endpoint, then top up below high watermark.
        if (renderer != nullptr) {
            double firstPts = -1.0;
            // Re-check after the endpoint wait: a video hold/seek can arrive
            // during it. Do not submit another block before applying the hold.
            if(!renderer->waitForEvent()){
                if(!ownEndpoint)break;
                recovering(renderer->lastError());continue;
            }
            if(shouldPause())continue;
            if (!renderer->pumpOnce(*this, &firstPts,false)) {
                if(!ownEndpoint){log::error("audio","pumpOnce failed");break;}
                recovering(renderer->lastError());continue;
            }
        }
        if (bufferedMs() < kAudioHighWatermarkMs) {
            decodeBlock();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (demuxEof_ && bufferedMs() < 1.0) {
            // End of media: park (engine loops the clip via its own seek).
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if(renderer&&ownEndpoint)renderer->shutdown();
}

void AudioPipeline::startThread(AudioRenderer* renderer, bool ownEndpoint, double initialMs)
{
    if(thread_.joinable())return;
    stopFlag_=false;seekRequested_=false;seekDone_=true;discardUntilPtsMs_=-1;
    endpointRecovering_=renderer&&ownEndpoint;endpointError_=S_OK;endpointRecoveries_=0;clockExhausted_=false;
    thread_ = std::thread(&AudioPipeline::runOnAudioThread, this, renderer, ownEndpoint, initialMs);
}

} // namespace veyra::sink
