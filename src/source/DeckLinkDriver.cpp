// Capture lifecycle adapted from OBS Studio plugins/decklink,
// e7f0b0d43c538f5d108aa4cbea473d57b3012a1a (GPL-2.0-or-later).
// Veyra uses owner-thread negotiation, stable IDs, explicit metadata and its
// existing upload mailbox. SDK headers are generated externally, never vendored.
#include "veyra/source/DeckLinkCapture.h"
#include "veyra/source/DeckLinkInputConnection.h"
#include "veyra/Log.h"
#include <algorithm>
#include <format>
#include <optional>

#if VEYRA_HAS_DECKLINK
#include <windows.h>
#include <wrl/client.h>
#include <DeckLinkAPI_h.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>

namespace veyra::source::decklink {
using Microsoft::WRL::ComPtr;
namespace {
bool checked(HRESULT hr,const char* operation,std::wstring* error=nullptr){
    log::info("decklink-api",std::format("{} hr=0x{:08X}",operation,uint32_t(hr)));
    if(FAILED(hr)&&error)*error=std::format(L"BMD 原生接口 {} 失败（0x{:08X}），请检查驱动、输入制式和设备占用",std::wstring(operation,operation+strlen(operation)),uint32_t(hr));
    return SUCCEEDED(hr);
}
std::wstring take(BSTR value){std::wstring s=value?std::wstring(value,SysStringLen(value)):L"";SysFreeString(value);return s;}
static_assert(bmdVideoConnectionSDI==1&&bmdVideoConnectionHDMI==2&&bmdVideoConnectionSVideo==32);
struct Row {ComPtr<IDeckLink> device;ComPtr<IDeckLinkInput> input;Device details;std::wstring name;uint32_t supportedConnections=0,requestedConnection=0;};
std::vector<Row> enumerate(){
    std::vector<Row> out;ComPtr<IDeckLinkIterator> iterator;
    const HRESULT hr=CoCreateInstance(__uuidof(CDeckLinkIterator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&iterator));
    // A missing Desktop Video driver is normal on ordinary UVC systems.
    if(FAILED(hr)){if(hr!=REGDB_E_CLASSNOTREG)checked(hr,"CreateIterator");return out;}
    ComPtr<IDeckLink> device;
    while(iterator->Next(&device)==S_OK){
        Row row;row.device=device;
        ComPtr<IDeckLinkProfileAttributes> attributes;
        if(FAILED(device.As(&row.input))||FAILED(device.As(&attributes))){device.Reset();continue;}
        LONGLONG id=0,sub=0;bool persistent=SUCCEEDED(attributes->GetInt(BMDDeckLinkPersistentID,&id));
        if(!persistent&&FAILED(attributes->GetInt(BMDDeckLinkTopologicalID,&id))){log::warn("decklink-device","No stable ID; refusing ordinal mapping");device.Reset();continue;}
        const auto subHr=attributes->GetInt(BMDDeckLinkSubDeviceIndex,&sub);
        BSTR name=nullptr;const auto nameHr=device->GetDisplayName(&name);
        row.details.name=take(name);if(FAILED(nameHr)||row.details.name.empty())row.details.name=L"Blackmagic DeckLink";
        row.name=row.details.name;
        row.details.path=std::format(L"decklink-native:{}:{:016x}:{:x}",persistent?L"p":L"t",uint64_t(id),uint64_t(sub));
        BOOL automatic=FALSE;const auto detectHr=attributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection,&automatic);
        row.details.autoDetection=SUCCEEDED(detectHr)&&automatic;
        ComPtr<IDeckLinkConfiguration> config;LONGLONG port=0;
        if(SUCCEEDED(device.As(&config)))checked(config->GetInt(bmdDeckLinkConfigVideoInputConnection,&port),"GetInputConnection");
        LONGLONG supported=0;const auto portsHr=attributes->GetInt(BMDDeckLinkVideoInputConnections,&supported);
        if(portsHr==S_OK&&supported>=0&&supported<=UINT32_MAX)row.supportedConnections=uint32_t(supported);
        row.details.name=std::format(L"[原生] {} · Driver ({})",row.name,inputConnectionName(uint32_t(port)));
        log::info("decklink-device",std::format("stableKind={} id=0x{:016X} sub={} subHr=0x{:08X} autoDetect={} detectHr=0x{:08X} connection=0x{:X} supportedConnections=0x{:X} portsHr=0x{:08X}",persistent?"persistent":"topological",uint64_t(id),sub,uint32_t(subHr),row.details.autoDetection,uint32_t(detectHr),uint64_t(port),row.supportedConnections,uint32_t(portsHr)));
        out.push_back(std::move(row));device.Reset();
    }
    // A driver without unique sub-device IDs must never make two physical
    // inputs share a saved identity. Leave those endpoints unselectable.
    std::vector<std::wstring> ambiguous;
    for(const auto& row:out)if(std::count_if(out.begin(),out.end(),[&](const Row& other){return other.details.path==row.details.path;})>1)ambiguous.push_back(row.details.path);
    if(!ambiguous.empty())log::warn("decklink-device","Duplicate stable input IDs; refusing ambiguous endpoints");
    std::erase_if(out,[&](const Row& row){return std::find(ambiguous.begin(),ambiguous.end(),row.details.path)!=ambiguous.end();});
    std::vector<Row> choices;
    for(const auto& row:out){
        choices.push_back(row); // preserve saved legacy identities
        for(auto connection:inputConnections)if(row.supportedConnections&connection){
            auto choice=row;choice.requestedConnection=connection;
            choice.details.path=inputConnectionPath(row.details.path,connection);
            choice.details.name=std::format(L"[原生] {} · {}",row.name,inputConnectionName(connection));
            choices.push_back(std::move(choice));
        }
    }
    return choices;
}
Mode describe(IDeckLinkDisplayMode* mode){
    Mode out;out.id=mode->GetDisplayMode();out.width=mode->GetWidth();out.height=mode->GetHeight();
    BMDTimeValue duration=0;BMDTimeScale scale=0;
    if(FAILED(mode->GetFrameRate(&duration,&scale)))return {};
    out.duration=duration;out.scale=scale;
    const auto field=mode->GetFieldDominance();out.interlaced=field==bmdUpperFieldFirst||field==bmdLowerFieldFirst;
    BSTR name=nullptr;const HRESULT nameHr=mode->GetName(&name);out.name=take(name);if(FAILED(nameHr))out.name=L"BMD";
    using namespace pipeline;
    auto& c=out.color;c.range=ColorRange::Limited;c.transfer=TransferFunction::BT709;
    c.transferAssumed=true;c.preserveSdrCodeValues=true;
    const auto flags=mode->GetFlags();
    if(flags&bmdDisplayModeColorspaceRec2020){c.matrix=YuvMatrix::BT2020NCL;c.primaries=ColorPrimaries::BT2020;}
    else if(flags&bmdDisplayModeColorspaceRec709){c.matrix=YuvMatrix::BT709;c.primaries=ColorPrimaries::BT709;}
    else{c.matrix=out.height>576?YuvMatrix::BT709:YuvMatrix::BT601;c.primaries=out.height>576?ColorPrimaries::BT709:out.height==576?ColorPrimaries::BT601_625:ColorPrimaries::BT601_525;c.matrixAssumed=c.primariesAssumed=(flags&bmdDisplayModeColorspaceRec601)==0;}
    return out;
}
std::vector<Mode> readModes(IDeckLinkInput* input,uint32_t connection=0){
    std::vector<Mode> out;ComPtr<IDeckLinkDisplayModeIterator> iterator;
    if(!checked(input->GetDisplayModeIterator(&iterator),"GetDisplayModeIterator"))return out;
    ComPtr<IDeckLinkDisplayMode> mode;
    while(iterator->Next(&mode)==S_OK){auto m=describe(mode.Get());
        if(m.width>=2&&m.height&&m.duration>0&&m.scale>0){
            bool usable=false;
            for(auto format:{bmdFormat10BitYUV,bmdFormat8BitYUV,bmdFormat10BitRGBXLE,bmdFormat8BitBGRA}){
                BOOL supported=FALSE;BMDDisplayMode actual=bmdModeUnknown;
                const HRESULT hr=input->DoesSupportVideoMode(BMDVideoConnection(connection),BMDDisplayMode(m.id),format,bmdNoVideoInputConversion,bmdSupportedVideoModeDefault,&actual,&supported);
                if(FAILED(hr))checked(hr,"DoesSupportVideoMode(enumerate)");
                if(hr==S_OK&&supported&&uint32_t(actual)==m.id){usable=true;break;}
            }
            if(usable)out.push_back(std::move(m));
        }
        mode.Reset();}
    // A conservative seed only. The format-change callback, not this order,
    // selects the real automatic mode before any frame can be published.
    std::stable_sort(out.begin(),out.end(),[](const Mode& a,const Mode& b){const auto seed=[](const Mode& m){return m.width==1920&&m.height==1080&&!m.interlaced&&m.scale==m.duration*60;};return seed(a)&&!seed(b);});
    return out;
}
BMDPixelFormat sdkPixel(Pixel p){switch(p){case Pixel::V210:return bmdFormat10BitYUV;case Pixel::Rgb10xLE:return bmdFormat10BitRGBXLE;case Pixel::Bgra8:return bmdFormat8BitBGRA;default:return bmdFormat8BitYUV;}}
std::optional<Pixel> pixel(BMDPixelFormat p){switch(p){case bmdFormat10BitYUV:return Pixel::V210;case bmdFormat10BitRGBXLE:return Pixel::Rgb10xLE;case bmdFormat8BitBGRA:return Pixel::Bgra8;case bmdFormat8BitYUV:return Pixel::Uyvy8;default:return {};}}
class Callback final:public IDeckLinkInputCallback {
    std::atomic<ULONG> refs_{1};std::mutex mutex_;std::condition_variable idle_;
    DriverSink* sink_;unsigned active_=0;bool accepting_=true;
    pipeline::ColorDescription color_;std::atomic<bool> loggedFrame_{false},loggedMetadata_{false};std::atomic<int> noInput_{-1};
    struct Entry {
        Callback& owner;DriverSink* sink=nullptr;pipeline::ColorDescription color;
        explicit Entry(Callback& o):owner(o){std::lock_guard lock(o.mutex_);if(o.accepting_){++o.active_;sink=o.sink_;color=o.color_;}}
        ~Entry(){if(sink){std::lock_guard lock(owner.mutex_);if(--owner.active_==0)owner.idle_.notify_all();}}
    };
public:
    explicit Callback(DriverSink& sink):sink_(&sink){}
    void color(const pipeline::ColorDescription& c){std::lock_guard lock(mutex_);color_=c;loggedFrame_=loggedMetadata_=false;noInput_=-1;}
    void detach(){std::lock_guard lock(mutex_);accepting_=false;}
    void wait(){std::unique_lock lock(mutex_);idle_.wait(lock,[&]{return active_==0;});sink_=nullptr;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=__uuidof(IDeckLinkInputCallback))return E_NOINTERFACE;*out=static_cast<IDeckLinkInputCallback*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events,IDeckLinkDisplayMode* display,BMDDetectedVideoInputFormatFlags flags)override{
        Entry entry(*this);if(!entry.sink)return S_OK;
        try{
            if(!display)return E_POINTER;
            Signal signal;signal.mode=describe(display);signal.rgb=(flags&bmdDetectedVideoInputRGB444)!=0;
            signal.bits=flags&bmdDetectedVideoInput12BitDepth?12:flags&bmdDetectedVideoInput10BitDepth?10:flags&bmdDetectedVideoInput8BitDepth?8:0;
            log::info("decklink-format",std::format("events=0x{:X} mode=0x{:08X} extent={}x{} duration={}/{} interlaced={} detectedFlags=0x{:X} rgb={} bits={}",events,signal.mode.id,signal.mode.width,signal.mode.height,signal.mode.duration,signal.mode.scale,signal.mode.interlaced,flags,signal.rgb,signal.bits));
            entry.sink->formatChanged(signal);return S_OK;
        }catch(...){entry.sink->failure(L"BMD 制式回调异常");return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* video,IDeckLinkAudioInputPacket* audio)override{
        Entry entry(*this);if(!entry.sink)return S_OK;
        try{
            if(video){
                Frame frame;const auto flags=video->GetFlags();frame.noInput=(flags&bmdFrameHasNoInputSource)!=0;
                if(noInput_.exchange(frame.noInput?1:0)!=(frame.noInput?1:0))log::info("decklink-input",std::format("noInput={} frameFlags=0x{:X}",frame.noInput,flags));
                if(frame.noInput){entry.sink->video(frame);return S_OK;}
                const auto format=pixel(video->GetPixelFormat());
                if(!format){entry.sink->failure(L"BMD 返回未支持的像素格式");return E_FAIL;}
                frame.pixel=*format;frame.width=video->GetWidth();frame.height=video->GetHeight();frame.stride=video->GetRowBytes();frame.bottomUp=(flags&bmdFrameFlagFlipVertical)!=0;
                void* data=nullptr;const HRESULT bytesHr=video->GetBytes(&data);
                const HRESULT timeHr=video->GetStreamTime(&frame.pts100ns,&frame.duration100ns,10000000);
                if(FAILED(bytesHr)||FAILED(timeHr)){checked(bytesHr,"VideoGetBytes");checked(timeHr,"VideoGetStreamTime");entry.sink->failure(L"BMD 无法读取有效画面或源时间戳");return E_FAIL;}
                frame.data=static_cast<const uint8_t*>(data);frame.bytes=size_t(frame.stride)*frame.height;frame.color=entry.color;
                using namespace pipeline;
                frame.color.pixelFormat=frame.pixel==Pixel::V210?SourcePixelFormat::V210:frame.pixel==Pixel::Rgb10xLE?SourcePixelFormat::Rgb10xLE:frame.pixel==Pixel::Uyvy8?SourcePixelFormat::Uyvy:SourcePixelFormat::Bgra8;
                frame.color.range=frame.pixel==Pixel::Bgra8?ColorRange::Full:ColorRange::Limited;
                ComPtr<IDeckLinkVideoFrameMetadataExtensions> metadata;const HRESULT metaHr=video->QueryInterface(IID_PPV_ARGS(&metadata));
                LONGLONG colorspace=0,eotf=0;HRESULT colorHr=E_NOINTERFACE,eotfHr=E_NOINTERFACE;
                if(metadata){
                    colorHr=metadata->GetInt(bmdDeckLinkFrameMetadataColorspace,&colorspace);
                    if(SUCCEEDED(colorHr)){
                        if(colorspace==bmdColorspaceRec2020){frame.color.matrix=YuvMatrix::BT2020NCL;frame.color.primaries=ColorPrimaries::BT2020;}
                        else if(colorspace==bmdColorspaceRec709){frame.color.matrix=YuvMatrix::BT709;frame.color.primaries=ColorPrimaries::BT709;}
                        else if(colorspace==bmdColorspaceRec601){frame.color.matrix=YuvMatrix::BT601;frame.color.primaries=frame.height==576?ColorPrimaries::BT601_625:ColorPrimaries::BT601_525;}
                        else {entry.sink->failure(L"BMD 返回未知颜色空间");return E_FAIL;}
                        frame.color.matrixAssumed=frame.color.primariesAssumed=false;
                    }
                    if(flags&bmdFrameContainsHDRMetadata){
                        eotfHr=metadata->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc,&eotf);
                        if(FAILED(eotfHr)||(eotf!=0&&eotf!=2&&eotf!=3)){checked(eotfHr,"GetHDRTransfer");entry.sink->failure(L"BMD HDR 帧的 EOTF 不受支持；仅接受 SDR、PQ 或 HLG，拒绝猜测颜色传递");return E_FAIL;}
                        frame.color.transfer=eotf==2?TransferFunction::PQ:eotf==3?TransferFunction::HLG:TransferFunction::BT709;
                        frame.color.transferAssumed=false;frame.color.preserveSdrCodeValues=!frame.color.isHdrPath();
                        auto light=[&](BMDDeckLinkFrameMetadataID id){double value=0;const HRESULT h=metadata->GetFloat(id,&value);return SUCCEEDED(h)&&std::isfinite(value)&&value>0&&value<=65535?float(value):0.f;};
                        frame.color.hdrMaxCllNits=light(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel);
                        frame.color.hdrMaxFallNits=light(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel);
                        frame.color.hdrMasteringPeakNits=light(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance);
                    }
                }else if(flags&bmdFrameContainsHDRMetadata){entry.sink->failure(L"BMD 标记 HDR，但驱动未提供 HDR 元数据接口");return E_FAIL;}
                if(!loggedMetadata_.exchange(true)){log::info("decklink-metadata",std::format("QueryInterfaceHr=0x{:08X} colorspaceHr=0x{:08X} colorspace=0x{:X} hdr={} eotfHr=0x{:08X} eotf={}",uint32_t(metaHr),uint32_t(colorHr),uint64_t(colorspace),(flags&bmdFrameContainsHDRMetadata)!=0,uint32_t(eotfHr),eotf));}
                if(!loggedFrame_.exchange(true)){log::info("decklink-frame",std::format("flags=0x{:X} extent={}x{} rowBytes={} pixel=0x{:X} bytesHr=0x{:08X} timeHr=0x{:08X} pts={} duration={}",flags,frame.width,frame.height,frame.stride,unsigned(video->GetPixelFormat()),uint32_t(bytesHr),uint32_t(timeHr),frame.pts100ns,frame.duration100ns));}
                entry.sink->video(frame);
            }
            if(audio){void* data=nullptr;BMDTimeValue pts=0;const auto bytesHr=audio->GetBytes(&data);const auto timeHr=audio->GetPacketTime(&pts,10000000);const auto samples=audio->GetSampleFrameCount();
                if(SUCCEEDED(bytesHr)&&SUCCEEDED(timeHr)&&data&&samples>0)entry.sink->audio(data,size_t(samples)*4,pts); // negotiated 16-bit stereo
                else{checked(bytesHr,"AudioGetBytes");checked(timeHr,"AudioGetPacketTime");}
            }
            return S_OK;
        }catch(...){entry.sink->failure(L"BMD 帧回调异常");return E_FAIL;}
    }
};
class ComDriver final:public Driver {
    ComPtr<IDeckLink> device_;ComPtr<IDeckLinkInput> input_;ComPtr<Callback> callback_;
    ComPtr<IDeckLinkConfiguration> configuration_;
    InputConnectionLease connectionLease_;
    uint32_t requestedConnection_=0,supportedConnections_=0;
    bool connectionConfigured_=false;
    bool auto_=false,videoEnabled_=false,audioEnabled_=false;
    bool readConnection(uint32_t& value){
        if(!configuration_)return false;LONGLONG actual=0;
        const auto hr=configuration_->GetInt(bmdDeckLinkConfigVideoInputConnection,&actual);
        checked(hr,"ReadInputConnection");
        if(hr!=S_OK||actual<0||actual>UINT32_MAX)return false;
        value=uint32_t(actual);return true;
    }
    bool writeConnection(uint32_t value){
        if(!configuration_)return false;
        const auto hr=configuration_->SetInt(bmdDeckLinkConfigVideoInputConnection,value);
        checked(hr,"SetInputConnection");return hr==S_OK;
    }
    void logStatus(){ComPtr<IDeckLinkStatus> status;if(FAILED(device_.As(&status)))return;
        BOOL locked=FALSE;LONGLONG detected=0,flags=0,current=0;
        const auto a=status->GetFlag(bmdDeckLinkStatusVideoInputSignalLocked,&locked),b=status->GetInt(bmdDeckLinkStatusDetectedVideoInputMode,&detected),c=status->GetInt(bmdDeckLinkStatusDetectedVideoInputFormatFlags,&flags),d=status->GetInt(bmdDeckLinkStatusCurrentVideoInputMode,&current);
        log::info("decklink-status",std::format("lockHr=0x{:08X} locked={} detectedHr=0x{:08X} detected=0x{:08X} flagsHr=0x{:08X} detectedFlags=0x{:X} currentHr=0x{:08X} current=0x{:08X}",uint32_t(a),locked,uint32_t(b),uint32_t(detected),uint32_t(c),uint64_t(flags),uint32_t(d),uint32_t(current)));}
public:
    ~ComDriver(){stop();}
    bool open(std::wstring_view path,DriverSink& sink,std::wstring& error)override{
        stop();const auto selection=inputConnectionSelection(path);
        if(!selection.valid){error=L"保存的 BMD 输入口无效，请重新选择设备和 HDMI/SDI 输入";return false;}
        bool physicalFound=false;
        for(auto& row:enumerate()){
            if(row.details.path==selection.device)physicalFound=true;
            if(row.details.path==path){device_=row.device;input_=row.input;auto_=row.details.autoDetection;requestedConnection_=row.requestedConnection;supportedConnections_=row.supportedConnections;break;}
        }
        if(!input_){error=physicalFound?L"这张 BMD 卡当前不提供所选输入口，请刷新并重新选择设备输入":L"BMD 原生设备当前不可用，请确认设备连接和 Desktop Video 状态，然后刷新设备列表";return false;}
        device_.As(&configuration_);
        callback_.Attach(new Callback(sink));
        if(!checked(input_->SetCallback(callback_.Get()),"SetCallback",&error)){stop();return false;}
        logStatus();return true;
    }
    std::vector<Mode> modes()override{return input_?readModes(input_.Get(),requestedConnection_):std::vector<Mode>{};}
    bool autoDetection()const override{return auto_;}
    bool enable(const Mode& mode,Pixel p,bool automatic,bool audio,std::wstring& error)override{
        if(!input_)return false;BOOL supported=FALSE;BMDDisplayMode actual=bmdModeUnknown;
        if(!connectionConfigured_){
            const bool selected=connectionLease_.apply(requestedConnection_,supportedConnections_,[&](uint32_t& value){return readConnection(value);},[&](uint32_t value){return writeConnection(value);});
            uint32_t actualConnection=0;const bool read=readConnection(actualConnection);
            log::info("decklink-connection",std::format("requested=0x{:X} actual=0x{:X} verified={} read={} supported=0x{:X} persistentWrite=0",requestedConnection_,actualConnection,selected,read,supportedConnections_));
            if(!selected){error=L"BMD 所选输入口不可用，或驱动未应用该输入口；请重新选择 HDMI/SDI 输入";return false;}
            connectionConfigured_=true;
        }
        const auto hr=input_->DoesSupportVideoMode(BMDVideoConnection(requestedConnection_),BMDDisplayMode(mode.id),sdkPixel(p),bmdNoVideoInputConversion,bmdSupportedVideoModeDefault,&actual,&supported);
        checked(hr,"DoesSupportVideoMode",&error);
        log::info("decklink-negotiate",std::format("mode=0x{:08X} actual=0x{:08X} pixel=0x{:08X} supported={} automatic={}",mode.id,unsigned(actual),unsigned(sdkPixel(p)),supported,automatic));
        if(FAILED(hr)||!supported||uint32_t(actual)!=mode.id){if(error.empty())error=L"BMD 输入不支持所选制式/像素格式";return false;}
        if(!checked(input_->EnableVideoInput(BMDDisplayMode(mode.id),sdkPixel(p),automatic?bmdVideoInputEnableFormatDetection:bmdVideoInputFlagDefault),"EnableVideoInput",&error))return false;
        videoEnabled_=true;callback_->color(mode.color);
        if(audio&&!audioEnabled_){if(!checked(input_->EnableAudioInput(bmdAudioSampleRate48kHz,bmdAudioSampleType16bitInteger,2),"EnableAudioInput",&error))return false;audioEnabled_=true;}
        logStatus();return true;
    }
    bool start(std::wstring& error)override{return input_&&checked(input_->StartStreams(),"StartStreams",&error);}
    bool pause(std::wstring& error)override{return input_&&checked(input_->PauseStreams(),"PauseStreams",&error);}
    bool flush(std::wstring& error)override{return input_&&checked(input_->FlushStreams(),"FlushStreams",&error);}
    void stop()noexcept override{
        if(callback_)callback_->detach();
        if(input_){checked(input_->StopStreams(),"StopStreams");checked(input_->SetCallback(nullptr),"DetachCallback");}
        if(callback_)callback_->wait();
        if(input_&&audioEnabled_)checked(input_->DisableAudioInput(),"DisableAudioInput");
        if(input_&&videoEnabled_)checked(input_->DisableVideoInput(),"DisableVideoInput");
        if(configuration_&&!connectionLease_.restore([&](uint32_t& value){return readConnection(value);},[&](uint32_t value){return writeConnection(value);}))log::warn("decklink-connection","Unable to restore the prior input connection; no persistent preferences were written");
        connectionLease_={};configuration_.Reset();requestedConnection_=supportedConnections_=0;connectionConfigured_=false;
        callback_.Reset();input_.Reset();device_.Reset();videoEnabled_=audioEnabled_=false;
    }
};
}
std::vector<Device> sdkDevices(){std::vector<Device> out;for(auto& row:enumerate())out.push_back(std::move(row.details));return out;}
std::vector<Mode> sdkModes(std::wstring_view path){for(auto& row:enumerate())if(row.details.path==path)return readModes(row.input.Get(),row.requestedConnection);return {};}
std::unique_ptr<Driver> makeSdkDriver(){return std::make_unique<ComDriver>();}
}
#else
namespace veyra::source::decklink {
std::vector<Device> sdkDevices(){return {};}
std::vector<Mode> sdkModes(std::wstring_view){return {};}
std::unique_ptr<Driver> makeSdkDriver(){return {};}
}
#endif
