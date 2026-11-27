#include "veyra/source/DeckLinkCapture.h"
#include "veyra/source/DeckLinkInputConnection.h"
#include "veyra/Log.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <optional>

namespace veyra::source::decklink {
size_t rowBytes(Pixel p,uint32_t width){
    if(p==Pixel::V210)return ((size_t(width)+5)/6)*16;
    return size_t(width)*(p==Pixel::Uyvy8?2:4);
}
bool sameColor(const pipeline::ColorDescription& a,const pipeline::ColorDescription& b){
    return a.pixelFormat==b.pixelFormat&&a.range==b.range&&a.matrix==b.matrix&&a.transfer==b.transfer&&
        a.primaries==b.primaries&&a.displayReferred709==b.displayReferred709&&a.preserveSdrCodeValues==b.preserveSdrCodeValues&&
        a.hdrMaxCllNits==b.hdrMaxCllNits&&a.hdrMaxFallNits==b.hdrMaxFallNits&&a.hdrMasteringPeakNits==b.hdrMasteringPeakNits;
}
bool isXcapPath(std::wstring_view path){return path.starts_with(L"xcap-native:");}
bool isPath(std::wstring_view path){return path.starts_with(L"decklink-native:")||isXcapPath(path);}
std::wstring modeKey(uint32_t id){return id?std::format(L"decklink:{:08x}",id):L"decklink:auto";}
struct Capture::Impl {
    std::unique_ptr<Driver> driver;
    mutable std::mutex mutex;std::condition_variable changed;
    std::optional<Signal> pending;
    Frame contract;Mode mode;
    bool automatic=false,embeddedAudio=false,probing=false,valid=false,running=false,reopen=false,xcap=false;
    Pixel pixel=Pixel::V210;
    std::wstring error;
    std::function<void(const Frame&)> onVideo;
    std::function<void(const void*,size_t,int64_t)> onAudio;
};
Capture::Capture(std::unique_ptr<Driver> driver):p_(std::make_unique<Impl>()){p_->driver=std::move(driver);}
Capture::~Capture(){stop();}
void Capture::failure(std::wstring error){
    {std::lock_guard lock(p_->mutex);p_->error=std::move(error);p_->reopen=true;}
    p_->changed.notify_all();
}
void Capture::formatChanged(const Signal& signal){
    {std::lock_guard lock(p_->mutex);
        const bool rgb=p_->pixel==Pixel::Bgra8||p_->pixel==Pixel::Rgb10xLE;
        if(signal.mode.id==p_->mode.id&&signal.rgb==rgb)return; // StartStreams may repeat the current detection.
        if(p_->running){p_->reopen=true;p_->error=L"BMD 输入制式/颜色格式已变化，正在重新检测信号";}
        else if(p_->probing&&p_->automatic){p_->pending=signal;p_->valid=false;}
    }
    p_->changed.notify_all();
}
void Capture::video(const Frame& frame){
    std::function<void(const Frame&)> callback;
    {
        std::lock_guard lock(p_->mutex);
        if(!p_->running&&!p_->probing)return;
        if(frame.noInput){
            if(p_->running){p_->error=L"BMD 输入信号丢失，正在等待 HDMI/SDI 信号锁定";p_->reopen=true;}
            p_->changed.notify_all();return;
        }
        if(p_->reopen||p_->pending)return;
        if(frame.width!=p_->mode.width||frame.height!=p_->mode.height||frame.pixel!=p_->pixel){
            if(p_->running){p_->error=L"BMD 帧布局已改变，正在重新协商采集";p_->reopen=true;}
            return; // Frames queued before Pause/Flush are never published.
        }
        const bool ten=frame.pixel==Pixel::V210||frame.pixel==Pixel::Rgb10xLE;
        if(!frame.data||frame.width<2||frame.width>3840||!frame.height||frame.height>2160||
            frame.stride<rowBytes(frame.pixel,frame.width)||frame.stride>frame.width*8+16384||frame.bytes<size_t(frame.stride)*frame.height||frame.duration100ns<=0||
            (frame.color.isHdrPath()&&!ten)){
            p_->error=L"BMD 返回无效帧布局、时间戳或不受支持的 HDR 位深";p_->reopen=true;p_->changed.notify_all();return;
        }
        if(p_->probing){
            p_->contract=frame;p_->contract.data=nullptr;p_->contract.bytes=0;p_->valid=true;p_->changed.notify_all();return;
        }
        // XCAP reports the console's own mastering data, which a game may change at any time: that is no new format.
        auto color=frame.color;
        if(p_->xcap){color.hdrMaxCllNits=p_->contract.color.hdrMaxCllNits;color.hdrMaxFallNits=p_->contract.color.hdrMaxFallNits;color.hdrMasteringPeakNits=p_->contract.color.hdrMasteringPeakNits;}
        if(frame.stride!=p_->contract.stride||frame.bottomUp!=p_->contract.bottomUp||!sameColor(color,p_->contract.color)){
            p_->error=L"BMD 输入颜色/行距合同已变化，正在重建处理链";p_->reopen=true;return;
        }
        callback=p_->onVideo;
    }
    // No source/control lock is held during the one ingest copy. The adapter
    // retains the SDK frame for this call only; there is no extra frame queue.
    if(callback)callback(frame);
}
void Capture::audio(const void* bytes,size_t size,int64_t pts){
    std::function<void(const void*,size_t,int64_t)> callback;
    {std::lock_guard lock(p_->mutex);if(p_->running&&!p_->reopen)callback=p_->onAudio;}
    if(callback)callback(bytes,size,pts);
}
bool Capture::configure(std::wstring_view path,std::wstring_view key,bool audio,unsigned timeoutMs){
    stop();auto& p=*p_;std::wstring why;
    {std::lock_guard lock(p.mutex);p.error.clear();p.reopen=false;p.valid=false;p.contract={};p.pending.reset();p.embeddedAudio=audio;}
    auto failed=[&](std::wstring message){p.driver->stop();std::lock_guard lock(p.mutex);p.probing=false;if(!message.empty())p.error=std::move(message);return false;};
    if(!isPath(path)||!p.driver){failure(L"BMD 原生接口不可用，请安装 Desktop Video 驱动或使用 DirectShow 入口");return false;}
    {std::lock_guard lock(p.mutex);p.xcap=isXcapPath(path);}
    if(!p.driver->open(path,*this,why))return failed(why);
    const auto available=p.driver->modes();
    const bool automatic=key==L"decklink:auto"||key.empty();
    {std::lock_guard lock(p.mutex);p.automatic=automatic;}
    if(automatic&&!p.driver->autoDetection())return failed(L"这张 BMD 卡不支持自动检测，请选择与信号一致的手动制式");
    const auto chosen=std::find_if(available.begin(),available.end(),[&](const Mode& m){return automatic?(m.width<=3840&&m.height<=2160):modeKey(m.id)==key;});
    if(chosen==available.end())return failed(L"保存的 BMD 制式不可用，请重新选择");
    if(chosen->width>3840||chosen->height>2160)return failed(L"设备提供此制式，但当前播放器的 BMD 采集最高支持 3840×2160");
    auto enable=[&](const Mode& mode,Pixel pixel){
        {std::lock_guard lock(p.mutex);p.mode=mode;p.pixel=pixel;p.valid=false;}
        const bool ok=p.driver->enable(mode,pixel,automatic,audio,why);if(ok)why.clear();return ok;
    };
    // Prefer ten-bit output even for eight-bit signals: this avoids throwing
    // away HDR precision before the first metadata-bearing frame arrives.
    // Some drivers announce the detected mode during EnableVideoInput,
    // before StartStreams. Do not lose that callback at the probe boundary.
    {std::lock_guard lock(p.mutex);p.probing=true;}
    if(!enable(*chosen,Pixel::V210)&&!enable(*chosen,Pixel::Uyvy8))return failed(why);
    why.clear();
    if(!p.driver->start(why))return failed(why);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::clamp(timeoutMs,1u,5000u));
    bool ok=false;
    for(;;){
        std::optional<Signal> signal;
        {std::unique_lock lock(p.mutex);
            if(p.reopen)break;
            if(p.pending){signal=p.pending;p.pending.reset();p.valid=false;}
            else if(p.valid){ok=true;break;}
            else if(p.changed.wait_until(lock,deadline)==std::cv_status::timeout)break;
        }
        if(signal){
            // OBS's Pause -> EnableVideoInput -> Flush -> Start sequence,
            // moved to the owner thread. Format callbacks never call control.
            if(!p.driver->pause(why))break;
            const Pixel preferred=signal->rgb?Pixel::Rgb10xLE:Pixel::V210;
            bool enabled=enable(signal->mode,preferred);
            if(!enabled&&signal->bits<=8)enabled=enable(signal->mode,signal->rgb?Pixel::Bgra8:Pixel::Uyvy8);
            if(!enabled||!p.driver->flush(why)||!p.driver->start(why))break;
        }
        if(std::chrono::steady_clock::now()>=deadline)break;
    }
    std::wstring pauseError;
    if(!p.driver->pause(pauseError)){if(why.empty())why=pauseError;ok=false;}
    {std::lock_guard lock(p.mutex);p.probing=false;if(p.reopen||p.pending)ok=false;}
    if(!ok){
        {std::lock_guard lock(p.mutex);if(why.empty())why=p.error;}
        if(why.empty())why=std::format(L"BMD 未在 {} 输入检测到有效信号。请在设备列表选择与 OBS 一致的 HDMI/SDI 输入口，并选择自动检测或匹配实际信号的手动制式。",inputConnectionName(inputConnectionSelection(path).connection));
        return failed(why);
    }
    if(!p.driver->flush(why))return failed(why);
    log::info("decklink-signal",std::format("valid=1 mode=0x{:08X} extent={}x{} rate={}/{} interlaced={} pixel={} stride={} transfer={} primaries={} mailbox=1 probeFramesRetained=0",
        p.mode.id,p.contract.width,p.contract.height,p.mode.scale,p.mode.duration,p.mode.interlaced,int(p.pixel),p.contract.stride,int(p.contract.color.transfer),int(p.contract.color.primaries)));
    return true;
}
bool Capture::start(std::function<void(const Frame&)> video,std::function<void(const void*,size_t,int64_t)> audio){
    {std::lock_guard lock(p_->mutex);if(!p_->valid||p_->reopen)return false;p_->onVideo=std::move(video);p_->onAudio=std::move(audio);p_->running=true;}
    std::wstring why;if(p_->driver->start(why))return true;
    {std::lock_guard lock(p_->mutex);p_->running=false;p_->error=std::move(why);}
    return false;
}
void Capture::stop()noexcept{
    {std::lock_guard lock(p_->mutex);p_->running=false;p_->probing=false;}
    if(p_->driver)p_->driver->stop();
    {std::lock_guard lock(p_->mutex);p_->onVideo={};p_->onAudio={};}
}
bool Capture::needsReopen()const{std::lock_guard lock(p_->mutex);return p_->reopen;}
std::wstring Capture::error()const{std::lock_guard lock(p_->mutex);return p_->error;}
Frame Capture::contract()const{std::lock_guard lock(p_->mutex);return p_->contract;}
unsigned Capture::audioChannels()const{return p_->driver?p_->driver->audioChannels():2;}
unsigned Capture::audioBits()const{return p_->driver?p_->driver->audioBits():16;}
Mode Capture::mode()const{std::lock_guard lock(p_->mutex);return p_->mode;}
}
