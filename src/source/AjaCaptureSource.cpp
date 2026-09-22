#include "veyra/source/AjaCaptureSource.h"
#include "veyra/Log.h"
#include "veyra/sink/AudioFormat.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <deque>
#include <thread>
#include <cstring>
#include <cmath>
#include <cwchar>
extern "C" {
#include <libavutil/frame.h>
}
#if defined(VEYRA_ENABLE_AJA)
#include "ntv2card.h"
#include "ntv2devicescanner.h"
#include "ntv2devicefeatures.h"
#include "ntv2formatdescriptor.h"
#include "ntv2signalrouter.h"
#include "ntv2utils.h"
#include "ntv2virtualregisters.h"
#endif
namespace veyra::source {
bool AjaCaptureSource::isDevice(std::wstring_view p){return p.starts_with(L"aja:");}
#if defined(VEYRA_ENABLE_AJA)
namespace {
constexpr ULWord signature=0x56595241; // VYRA
using Clock=std::chrono::steady_clock;
int64_t hostNow(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()/100;}
bool parse(std::wstring_view path,uint64_t& serial,unsigned& port){
    if(!AjaCaptureSource::isDevice(path))return false;
    const auto split=path.find(L':',4);if(split==path.npos)return false;
    try {size_t a=0,b=0;auto s=std::wstring(path.substr(4,split-4)),p=std::wstring(path.substr(split+1));
        serial=std::stoull(s,&a,16);port=unsigned(std::stoul(p,&b));return a==s.size()&&b==p.size()&&port>=1&&port<=4;
    }catch(...){return false;}
}
bool openDevice(CNTV2Card& card,std::wstring_view path,unsigned& port){
    uint64_t serial=0;if(!parse(path,serial,port))return false;
    CNTV2DeviceScanner scan;
    for(const auto& d:scan.GetDeviceInfoList())if(d.deviceID==DEVICE_ID_KONAHDMI&&d.deviceSerialNumber==serial)return card.Open(UWord(d.deviceIndex));
    return false;
}
bool isSdr(CNTV2Card& card,NTV2Channel ch){
    if(ch>NTV2_CHANNEL2)return true; // KONA HDMI HDR metadata receivers are ports 1/2.
    ULWord drm=0;if(!card.ReadRegister(ch==NTV2_CHANNEL1?kVRegHDMIInDrmInfo1:kVRegHDMIInDrmInfo2,drm))return false;
    return !(drm&kVRegMaskHDMIInPresent)||!(drm&kVRegMaskHDMIInEOTF);
}
NTV2InputSource inputFor(unsigned port){return NTV2ChannelToInputSource(NTV2Channel(port-1),NTV2_INPUTSOURCES_HDMI);}
}
struct AjaCaptureSource::Impl {
    CNTV2Card card; SourceInfo info;std::wstring error;
    NTV2Channel channel=NTV2_CHANNEL1,inputChannel=NTV2_CHANNEL1;
    NTV2InputSource input=NTV2_INPUTSOURCE_HDMI1;
    NTV2VideoFormat format=NTV2_FORMAT_UNKNOWN;
    NTV2EveryFrameTaskMode oldService=NTV2_STANDARD_TASKS;
    NTV2ReferenceSource oldReference=NTV2_REFERENCE_FREERUN;
    CNTV2SignalRouter oldRouting;bool oldMulti=false,owned=false,saved=false,configured=false,acInitialized=false;
    struct ChannelState {NTV2VideoFormat format;NTV2FrameBufferFormat pixel;NTV2Mode mode;NTV2VANCMode vanc;bool enabled,tsi;};
    std::vector<ChannelState> oldChannels;
    NTV2AudioSource oldAudioSource=NTV2_AUDIO_EMBEDDED;NTV2EmbeddedAudioInput oldAudioInput=NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1;
    NTV2AudioRate oldAudioRate=NTV2_AUDIO_48K;NTV2AudioBufferSize oldAudioBuffer=NTV2_AUDIO_BUFFER_SIZE_4MB;
    ULWord oldAudioChannels=0;bool audioSaved=false;
    std::unique_ptr<sink::CaptureAudioSession> audio;
    NTV2_POINTER dmaVideo,dmaAudio;NTV2FormatDescriptor layout;
    AVFrame *pendingFrame=nullptr,*readFrame=nullptr;
    std::deque<int64_t> arrivals;
    std::thread worker;std::atomic<bool> stop{false},failed{false},flip{false};
    mutable std::mutex mutex;std::condition_variable wake;
    bool pending=false;uint64_t received=0,delivered=0,dropped=0,lastDrop=0,sequence=0,pendingSequence=0;
    int64_t pendingPts=0,pendingArrival=0,readArrival=0,firstArrival=0,lastArrival=0;
    double readAge=0;ULWord rateNum=0,rateDen=1;
    bool check(bool ok,const wchar_t* operation){if(!ok){error=std::format(L"AJA: {} 失败，请检查设备状态。",operation);log::error("aja-capture","NTV2 configuration API returned false");}return ok;}
    bool route(bool uhd){
        NTV2LHIHDMIColorSpace color=NTV2_LHIHDMIColorSpaceYCbCr;
        if(!card.GetHDMIInputColor(color,inputChannel))return false;
        const bool rgb=color==NTV2_LHIHDMIColorSpaceRGB;
        if(!uhd){
            auto src=GetInputSourceOutputXpt(input,false,rgb,0);
            if(rgb){if(!card.Connect(GetCSCInputXptFromChannel(channel),src))return false;src=GetCSCOutputXptFromChannel(channel,false,false);}
            return card.Connect(GetFrameBufferInputXptFromChannel(channel),src);
        }
        // KONA HDMI UHD uses four HDMI quadrants through two TSI muxes.
        // See MIT AJA ntv2capture4k RouteInputSignal; no desktop capture involved.
        const NTV2InputXptID muxIn[]={NTV2_Xpt425Mux1AInput,NTV2_Xpt425Mux1BInput,NTV2_Xpt425Mux2AInput,NTV2_Xpt425Mux2BInput,
            NTV2_Xpt425Mux3AInput,NTV2_Xpt425Mux3BInput,NTV2_Xpt425Mux4AInput,NTV2_Xpt425Mux4BInput};
        const NTV2OutputXptID muxOut[]={NTV2_Xpt425Mux1AYUV,NTV2_Xpt425Mux1BYUV,NTV2_Xpt425Mux2AYUV,NTV2_Xpt425Mux2BYUV,
            NTV2_Xpt425Mux3AYUV,NTV2_Xpt425Mux3BYUV,NTV2_Xpt425Mux4AYUV,NTV2_Xpt425Mux4BYUV};
        unsigned base=unsigned(channel)*2;
        for(unsigned q=0;q<4;++q){
            auto src=GetInputSourceOutputXpt(input,false,rgb,UWord(q));
            if(rgb){auto c=NTV2Channel(q);if(!card.Connect(GetCSCInputXptFromChannel(c),src))return false;src=GetCSCOutputXptFromChannel(c,false,false);}
            if(!card.Connect(muxIn[base+q],src)||!card.Connect(GetFrameBufferInputXptFromChannel(NTV2Channel(unsigned(channel)+q/2),q%2!=0),muxOut[base+q]))return false;
        }
        return true;
    }
    void run(){
        int64_t checkAt=0,transferTicks=0,copyTicks=0,queryTicks=0;uint64_t transfers=0;
        while(!stop){
            const auto queryStart=hostNow();
            AUTOCIRCULATE_STATUS status;
            if(!card.AutoCirculateGetStatus(channel,status)){failed=true;break;}
            if(!status.HasAvailableInputFrame()){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            if(hostNow()>=checkAt){
                if(card.GetInputVideoFormat(input)!=format||!isSdr(card,inputChannel)){failed=true;log::error("aja-capture","Input format changed or signal lost; reconnect required");break;}
                checkAt=hostNow()+5000000;
            }
            queryTicks+=hostNow()-queryStart;
            const auto transferStart=hostNow();
            AUTOCIRCULATE_TRANSFER transfer;transfer.SetVideoBuffer(dmaVideo,dmaVideo.GetByteCount());
            if(audio)transfer.SetAudioBuffer(dmaAudio,dmaAudio.GetByteCount());
            if(!card.AutoCirculateTransfer(channel,transfer)){failed=true;break;}
            const int64_t arrival=hostNow();transferTicks+=arrival-transferStart;++transfers;
            // Board audio clock is a documented 10 MHz capture timestamp.
            const auto ticks=transfer.acTransferStatus.acFrameStamp.acAudioClockTimeStamp;
            const int64_t pts=ticks?int64_t(ticks):arrival;
            if(audio){
                const auto bytes=transfer.GetCapturedAudioByteCount();
                if(bytes>dmaAudio.GetByteCount()||bytes%32){failed=true;break;}
                // Never send known IEC61937 compressed bursts to a PCM endpoint.
                const auto* samples=static_cast<const uint32_t*>(dmaAudio.GetHostPointer());bool burst=false;
                for(unsigned i=0;i+1<bytes/4;i+=2)if((samples[i]>>16)==0xF872&&(samples[i+1]>>16)==0x4E1F){burst=true;break;}
                if(burst){log::error("aja-capture","Compressed HDMI audio unsupported; select linear PCM at the source");failed=true;break;}
                if(!audio->push(dmaAudio,bytes,double(pts)/10000.0,false)){failed=true;break;}
            }
            {
                std::lock_guard lock(mutex);
                if(pending)++dropped;
                auto* src=static_cast<const uint8_t*>(dmaVideo.GetHostPointer());
                const unsigned row=info.width*2;
                for(unsigned y=0;y<info.height;++y){unsigned sy=flip?info.height-1-y:y;std::memcpy(pendingFrame->data[0]+size_t(y)*pendingFrame->linesize[0],src+size_t(sy)*layout.GetBytesPerRow(),row);}
                ++received;arrivals.push_back(arrival);while(arrivals.size()>1&&arrival-arrivals.front()>20000000)arrivals.pop_front();pendingSequence=uint64_t(transfer.acTransferStatus.GetProcessedFrameCount())+transfer.acTransferStatus.GetDroppedFrameCount();
                pendingPts=pts;pendingArrival=arrival;lastArrival=arrival;if(!firstArrival)firstArrival=arrival;pending=true;
            }
            copyTicks+=hostNow()-arrival;wake.notify_one();
        }
        if(transfers)log::info("aja-capture-timing",std::format("frames={} queryMs={:.3f} dmaMs={:.3f} copyMs={:.3f}",transfers,double(queryTicks)/transfers/10000,double(transferTicks)/transfers/10000,double(copyTicks)/transfers/10000));
        wake.notify_all();
    }
};
AjaCaptureSource::AjaCaptureSource():p_(std::make_unique<Impl>()){}
AjaCaptureSource::~AjaCaptureSource(){close();}
std::vector<CaptureDevice> AjaCaptureSource::devices(){
    std::vector<CaptureDevice> out;CNTV2DeviceScanner scan;
    for(const auto& d:scan.GetDeviceInfoList())if(d.deviceID==DEVICE_ID_KONAHDMI){
        for(unsigned port=1;port<=4;++port)out.push_back({std::format(L"[AJA] KONA HDMI · HDMI {} ({:x})",port,d.deviceSerialNumber),std::format(L"aja:{:x}:{}",d.deviceSerialNumber,port),true,false});
    }return out;
}
std::vector<CaptureFormat> AjaCaptureSource::formats(std::wstring_view path){
    CNTV2Card card;unsigned port=0;if(!openDevice(card,path,port))return {};
    auto f=card.GetInputVideoFormat(inputFor(port));if(!NTV2_IS_VALID_VIDEO_FORMAT(f))return {{0,0,0,0,L"自动检测输入（当前无信号）",L"aja-auto",0,0}};
    NTV2FormatDescriptor d(f,NTV2_FBF_8BIT_YCBCR);double fps=GetFramesPerSecond(GetNTV2FrameRateFromVideoFormat(f));
    return {{0,d.GetRasterWidth(),d.GetVisibleRasterHeight(),fps,std::format(L"{} × {} @ {:.2f} · AJA UYVY 8-bit SDR · 跟随输入",d.GetRasterWidth(),d.GetVisibleRasterHeight(),fps),L"aja-auto",0,0}};
}
bool AjaCaptureSource::configure(std::wstring_view path,int audioMode,double requestedFps){
    close();auto& p=*p_;p.error.clear();unsigned port=0;
    if(audioMode!=kCaptureAudioDisabled&&audioMode!=kCaptureAudioFromVideoDevice){p.error=L"AJA 当前支持关闭音频或设备内嵌 PCM 音频。";return false;}
    if(!openDevice(p.card,path,port)){p.error=L"AJA 设备不存在或驱动不可用。";return false;}
    p.inputChannel=NTV2Channel(port-1);p.input=inputFor(port);p.format=p.card.GetInputVideoFormat(p.input);
    if(!NTV2_IS_VALID_VIDEO_FORMAT(p.format)){p.error=L"AJA 所选 HDMI 输入无信号；检查端口和 PS5 HDCP。";return false;}
    p.layout=NTV2FormatDescriptor(p.format,NTV2_FBF_8BIT_YCBCR);
    if(!p.layout.IsValid()||p.layout.GetRasterWidth()>3840||p.layout.GetVisibleRasterHeight()>2160||!NTV2_VIDEO_FORMAT_HAS_PROGRESSIVE_PICTURE(p.format)){p.error=L"AJA 当前支持最高 UHD60 的逐行输入。";return false;}
    if(!isSdr(p.card,p.inputChannel)){p.error=L"AJA 当前接入为 SDR；请先关闭源端 HDR。";return false;}
    GetFramesPerSecond(GetNTV2FrameRateFromVideoFormat(p.format),p.rateNum,p.rateDen);
    double fps=double(p.rateNum)/p.rateDen;if(fps>60.01||(requestedFps>0&&std::abs(requestedFps-fps)>0.01)){p.error=L"AJA 跟随 HDMI 实际帧率，请将设备帧率设为 0。";return false;}
    const bool uhd=p.layout.GetRasterWidth()>2048;
    if(uhd&&port>2){p.error=L"UHD 输入请选择 HDMI 1 或 2。";return false;}
    p.channel=uhd?NTV2Channel((port-1)*2):p.inputChannel;
    if(!p.card.AcquireStreamForApplication(signature,int32_t(GetCurrentProcessId()))){p.error=L"AJA 被其他程序占用，请关闭 Control Room 等采集程序后重试。";return false;}p.owned=true;
    if(!p.card.GetEveryFrameServices(p.oldService)||!p.card.GetReference(p.oldReference)||!p.card.GetRouting(p.oldRouting)||!p.card.GetMultiFormatMode(p.oldMulti)){p.error=L"无法保存 AJA 原配置，未开始采集。";return false;}
    for(unsigned i=0;i<4;++i){Impl::ChannelState s{};auto ch=NTV2Channel(i);
        if(!p.card.GetVideoFormat(s.format,ch)||!p.card.GetFrameBufferFormat(ch,s.pixel)||!p.card.GetMode(ch,s.mode)||!p.card.GetVANCMode(s.vanc,ch)||!p.card.IsChannelEnabled(ch,s.enabled)||!p.card.GetTsiFrameEnable(s.tsi,ch)){p.error=L"无法保存 AJA 通道配置。";return false;}p.oldChannels.push_back(s);
    }p.saved=true;
    auto check=[&](bool v){return p.check(v,L"配置采集");};
    if(!check(p.card.SetEveryFrameServices(NTV2_OEM_TASKS))||!check(p.card.SetMultiFormatMode(false))||!check(p.card.SetReference(NTV2_REFERENCE_FREERUN))||!check(p.card.SetVideoFormat(p.format,false,false,p.channel)))return false;
    for(unsigned i=0;i<(uhd?2u:1u);++i){auto ch=NTV2Channel(unsigned(p.channel)+i);if(!check(p.card.EnableChannel(ch))||!check(p.card.SetMode(ch,NTV2_MODE_CAPTURE))||!check(p.card.SetFrameBufferFormat(ch,NTV2_FBF_8BIT_YCBCR))||!check(p.card.SetVANCMode(NTV2_VANCMODE_OFF,ch)))return false;}
    if(!check(p.card.SetTsiFrameEnable(uhd,p.channel))||!check(p.route(uhd)))return false;
    if(!p.dmaVideo.Allocate(p.layout.GetVideoWriteSize())){p.error=L"AJA DMA 缓冲区分配失败。";return false;}
    p.info={};p.info.kind=pipeline::SourceKind::CaptureCard;p.info.width=p.layout.GetRasterWidth();p.info.height=p.layout.GetVisibleRasterHeight();p.info.averageFps=fps;p.info.nominalRateNum=int(p.rateNum);p.info.nominalRateDen=int(p.rateDen);p.info.videoDecodePath="aja-dma";p.info.videoPixelFormatName="uyvy422";
    auto& c=p.info.color;c.pixelFormat=pipeline::SourcePixelFormat::Uyvy;c.range=pipeline::ColorRange::Limited;c.matrix=pipeline::YuvMatrix::BT709;c.transfer=pipeline::TransferFunction::BT709;c.primaries=pipeline::ColorPrimaries::BT709;c.matrixAssumed=c.transferAssumed=c.primariesAssumed=true;c.preserveSdrCodeValues=true;
    for(auto ptr:{&p.pendingFrame,&p.readFrame}){*ptr=av_frame_alloc();if(!*ptr)return false;(*ptr)->format=AV_PIX_FMT_UYVY422;(*ptr)->width=int(p.info.width);(*ptr)->height=int(p.info.height);if(av_frame_get_buffer(*ptr,64)<0)return false;}
    if(audioMode==kCaptureAudioFromVideoDevice){
        auto sys=NTV2_AUDIOSYSTEM_1;
        if(!p.card.GetAudioSystemInputSource(sys,p.oldAudioSource,p.oldAudioInput)||!p.card.GetEmbeddedAudioInput(p.oldAudioInput,sys)||!p.card.GetNumberAudioChannels(p.oldAudioChannels,sys)||!p.card.GetAudioRate(p.oldAudioRate,sys)||!p.card.GetAudioBufferSize(p.oldAudioBuffer,sys)){p.error=L"无法保存 AJA 音频配置。";return false;}p.audioSaved=true;
        if(!check(p.card.SetAudioSystemInputSource(sys,NTV2_AUDIO_HDMI,NTV2InputSourceToEmbeddedAudioInput(p.input)))||!check(p.card.SetNumberAudioChannels(8,sys))||!check(p.card.SetAudioRate(NTV2_AUDIO_48K,sys))||!check(p.card.SetAudioBufferSize(NTV2_AUDIO_BUFFER_SIZE_4MB,sys)))return false;
        if(!p.dmaAudio.Allocate((401u * 1024u))){p.error=L"AJA 音频缓冲分配失败。";return false;}p.audio=std::make_unique<sink::CaptureAudioSession>();
        WAVEFORMATEXTENSIBLE wave{};wave.Format={WAVE_FORMAT_EXTENSIBLE,8,48000,1536000,32,32,22};
        wave.Samples.wValidBitsPerSample=24;wave.dwChannelMask=KSAUDIO_SPEAKER_7POINT1_SURROUND;wave.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;
        if(!p.audio->configure(wave.Format,sizeof(wave))){p.error=L"AJA PCM 音频输出初始化失败。";return false;}
    }
    if(!check(p.card.AutoCirculateStop(p.channel))||!check(p.card.AutoCirculateInitForInput(p.channel,7,p.audio?NTV2_AUDIOSYSTEM_1:NTV2_AUDIOSYSTEM_INVALID)))return false;
    p.acInitialized=true;p.configured=true;log::info("aja-capture",std::format("configured {}x{} rate={}/{} port={} SDR UYVY",p.info.width,p.info.height,p.rateNum,p.rateDen,port));return true;
}
bool AjaCaptureSource::start(){auto& p=*p_;if(p.info.opened)return true;if(!p.configured)return false;if(p.audio&&!p.audio->start()){p.error=L"AJA 音频播放启动失败。";return false;}if(!p.card.AutoCirculateStart(p.channel)){p.error=L"AJA AutoCirculate 启动失败。";return false;}p.stop=false;p.failed=false;p.info.opened=true;p.lastArrival=hostNow();try{p.worker=std::thread([&p]{try{p.run();}catch(...){p.failed=true;p.wake.notify_all();}});}catch(...){p.info.opened=false;p.card.AutoCirculateStop(p.channel);if(p.audio)p.audio->stop();p.error=L"无法创建 AJA 采集线程。";return false;}return true;}
SourceReadStatus AjaCaptureSource::read(pipeline::FramePacket& packet,const AVFrame** frame,unsigned waitMs){
    auto& p=*p_;*frame=nullptr;if(!p.info.opened)return SourceReadStatus::Error;std::unique_lock lock(p.mutex);
    if(waitMs)p.wake.wait_for(lock,std::chrono::milliseconds(waitMs),[&]{return p.pending||p.failed.load();});
    if(p.failed){p.error=L"AJA 传输失败、输入格式变化或音频不受支持，请检查信号后重新连接。";return SourceReadStatus::Error;}if(!p.pending){if(hostNow()-p.lastArrival>30000000){p.error=L"AJA 超过三秒未收到视频帧，请检查输入信号。";return SourceReadStatus::Error;}return SourceReadStatus::Waiting;}
    std::swap(p.readFrame,p.pendingFrame);p.pending=false;p.readArrival=p.pendingArrival;p.readAge=double(hostNow()-p.readArrival)/10000;
    packet={};packet.pts={p.pendingPts,10000000};packet.duration={p.rateDen,int32_t(p.rateNum)};packet.arrivalHost100ns=p.readArrival;packet.sourceKind=pipeline::SourceKind::CaptureCard;packet.colorInfo=p.info.color;packet.sequence=p.pendingSequence;packet.sourceEpoch=1;
    if(!p.delivered)packet.flags|=uint32_t(pipeline::FrameFlagBits::Open);
    if(p.dropped!=p.lastDrop||(p.sequence&&p.pendingSequence!=p.sequence+1))packet.flags|=uint32_t(pipeline::FrameFlagBits::Drop);
    p.lastDrop=p.dropped;p.sequence=p.pendingSequence;++p.delivered;p.readFrame->pts=p.pendingPts;p.readFrame->duration=packet.duration.to100ns();p.readFrame->time_base={1,10000000};*frame=p.readFrame;return SourceReadStatus::Frame;
}
void AjaCaptureSource::close() noexcept {
    auto& p=*p_;p.stop=true;if(p.worker.joinable())p.worker.join();if(p.acInitialized)p.card.AutoCirculateStop(p.channel);p.acInitialized=false;
    if(p.audio)p.audio->stop();p.audio.reset();
    if(p.audioSaved){p.card.SetAudioSystemInputSource(NTV2_AUDIOSYSTEM_1,p.oldAudioSource,p.oldAudioInput);p.card.SetNumberAudioChannels(p.oldAudioChannels);p.card.SetAudioRate(p.oldAudioRate);p.card.SetAudioBufferSize(p.oldAudioBuffer);}p.audioSaved=false;
    if(p.saved){for(unsigned i=0;i<p.oldChannels.size();++i){auto& s=p.oldChannels[i];auto ch=NTV2Channel(i);p.card.SetVideoFormat(s.format,false,false,ch);p.card.SetFrameBufferFormat(ch,s.pixel);p.card.SetMode(ch,s.mode);p.card.SetVANCMode(s.vanc,ch);p.card.SetTsiFrameEnable(s.tsi,ch);if(s.enabled)p.card.EnableChannel(ch);else p.card.DisableChannel(ch);}p.card.ApplySignalRoute(p.oldRouting,true);p.card.SetReference(p.oldReference);p.card.SetMultiFormatMode(p.oldMulti);p.card.SetEveryFrameServices(p.oldService);}p.saved=false;p.oldChannels.clear();
    if(p.owned)p.card.ReleaseStreamForApplication(signature,int32_t(GetCurrentProcessId()));p.owned=false;p.card.Close();
    av_frame_free(&p.pendingFrame);av_frame_free(&p.readFrame);p.dmaVideo.Deallocate();p.dmaAudio.Deallocate();p.info={};p.configured=p.pending=false;p.received=p.delivered=p.dropped=p.lastDrop=p.sequence=0;p.firstArrival=0;p.arrivals.clear();p.lastArrival=p.readArrival=0;p.readAge=0;
}
CaptureMetrics AjaCaptureSource::metrics()const{auto& p=*p_;std::lock_guard lock(p.mutex);CaptureMetrics m;m.received=p.received;m.delivered=p.delivered;m.dropped=p.dropped;m.readAgeMs=p.readAge;if(p.arrivals.size()>1&&hostNow()-p.lastArrival<10000000)m.callbackFps=double(p.arrivals.size()-1)*1e7/(p.arrivals.back()-p.arrivals.front());if(p.readArrival)m.frameAgeMs=double(hostNow()-p.readArrival)/10000;return m;}
void AjaCaptureSource::setVerticalFlip(bool v){p_->flip=v;}
bool AjaCaptureSource::setAudioGain(float v){if(!p_->audio)return false;p_->audio->setGain(v);return p_->audio->snapshot().available;}
void AjaCaptureSource::setAudioSync(unsigned m,int o){if(p_->audio)p_->audio->setSync(m,o);}
void AjaCaptureSource::videoPresented(double pts,int64_t h,int64_t a){if(p_->audio)p_->audio->videoPresented(pts,h,a);}
void AjaCaptureSource::videoReset(bool v){if(p_->audio)p_->audio->videoReset(v);}
sink::CaptureAudioState AjaCaptureSource::audioState()const{return p_->audio?p_->audio->snapshot():sink::CaptureAudioState{};}
#else
struct AjaCaptureSource::Impl{SourceInfo info;std::wstring error=L"此构建未启用 AJA NTV2 支持。";};
AjaCaptureSource::AjaCaptureSource():p_(std::make_unique<Impl>()){} AjaCaptureSource::~AjaCaptureSource()=default;
std::vector<CaptureDevice> AjaCaptureSource::devices(){return {};}
std::vector<CaptureFormat> AjaCaptureSource::formats(std::wstring_view){return {};}
bool AjaCaptureSource::configure(std::wstring_view,int,double){return false;} bool AjaCaptureSource::start(){return false;}
void AjaCaptureSource::close()noexcept{} SourceReadStatus AjaCaptureSource::read(pipeline::FramePacket&,const AVFrame** f,unsigned){*f=nullptr;return SourceReadStatus::Error;}
CaptureMetrics AjaCaptureSource::metrics()const{return {};}
void AjaCaptureSource::setVerticalFlip(bool){} bool AjaCaptureSource::setAudioGain(float){return false;}
void AjaCaptureSource::setAudioSync(unsigned,int){} void AjaCaptureSource::videoPresented(double,int64_t,int64_t){} void AjaCaptureSource::videoReset(bool){}
sink::CaptureAudioState AjaCaptureSource::audioState()const{return {};}
#endif
const SourceInfo& AjaCaptureSource::info()const{return p_->info;}
const std::wstring& AjaCaptureSource::error()const{return p_->error;}
}
