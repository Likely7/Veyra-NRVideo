#include "veyra/source/AverMediaVendorAudio.h"
#include "veyra/source/AverMediaAudioSwitch.h"
#include "veyra/Log.h"
#include <windows.h>
#include <objbase.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

namespace veyra::source {
namespace {
double nowSeconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string narrow(const std::wstring& text){
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size>0?size_t(size):0,'\0');
    if(size>0)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);return out;
}
// Local x64 mirror of the fixed public headers in THIRD_PARTY_NOTICES. The
// wrapper owns one opaque pointer; never inspect its private implementation.
using ForeignFn=uintptr_t(*)(void*,void*,void*);
struct ForeignCall {ForeignFn fn=nullptr;void* self=nullptr;void* a=nullptr;void* b=nullptr;uintptr_t result=0;DWORD fault=0;};
void invoke(ForeignCall* call){
    __try {call->result=call->fn(call->self,call->a,call->b);}
    __except(GetExceptionCode()==0xE06D7363?EXCEPTION_CONTINUE_SEARCH:(call->fault=GetExceptionCode(),EXCEPTION_EXECUTE_HANDLER)){}
}
void formatLog(char* out,size_t size,const char* format,char* args) noexcept {
    __try {if(args)std::vsnprintf(out,size,format,args);else std::snprintf(out,size,"%s",format);}
    __except(EXCEPTION_EXECUTE_HANDLER){std::snprintf(out,size,"vendor log formatting fault");}
}
void vendorLog(const char* format,char* args) noexcept {
    if(!format)return;char text[1024]{};formatLog(text,sizeof(text),format,args);
    try{log::info("capture-audio-vendor",text);}catch(...){}
}
constexpr const char* exports[]={
    "??0AudioDevice@AVerMedia@@QEAA@XZ",
    "??1AudioDevice@AVerMedia@@QEAA@XZ",
    "?ConnectFilters@AudioDevice@AVerMedia@@QEAA_NXZ",
    "?GetDeviceList@AudioDevice@AVerMedia@@SAXAEAV?$vector@UDeviceInfo@AVerMedia@@V?$allocator@UDeviceInfo@AVerMedia@@@std@@@std@@@Z",
    "?ResetGraph@AudioDevice@AVerMedia@@QEAA_NXZ",
    "?SetCallback@AudioDevice@AVerMedia@@QEAA_NPEAVAudioDealer@2@@Z",
    "?Start@AudioDevice@AVerMedia@@QEAA_NXZ",
    "?Stop@AudioDevice@AVerMedia@@QEAA_NXZ",
    "?UpdateDevice@AudioDevice@AVerMedia@@QEAA_NAEBV?$basic_string@_WU?$char_traits@_W@std@@V?$allocator@_W@2@@std@@0@Z"
};
enum Entry {Ctor,Dtor,Connect,DeviceList,Reset,SetCallback,Start,Stop,Update,EntryCount};
}

struct AverMediaVendorAudio::Impl {
    mutable std::mutex mutex;
    std::condition_variable changed;
    AverMediaVendorAudioState state;
    std::thread worker;
    std::atomic<bool> stopRequested=false,restartRequested=false;
    std::atomic<double> lastCallbackAt=0;
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    unsigned stallMs=6000,maxRestarts=3;
    std::wstring preferName,preferPath;
    std::array<ForeignFn,EntryCount> api{};
    alignas(16) std::array<unsigned char,64> storage{};
    bool constructed=false;
    bool quarantine=false;
    HMODULE module=nullptr;
    std::vector<DLL_DIRECTORY_COOKIE> directories;
    std::mutex decodeMutex;
    bool tearingDown=true;
    std::unique_ptr<AverMediaAudioCarrier> carrier;
    AverMediaAudioSwitch nonPcmSwitch;
    PcmCallback onPcm;
    struct Dealer final:avermedia::AudioDealer {
        Impl* owner=nullptr;
        int onAudioData(avermedia::SampleInfo info,unsigned char* data,long length) override {
            if(!owner||!data||length<=0)return TRUE;
            try{return owner->onCarrier(info,data,size_t(length))?TRUE:FALSE;}
            catch(...){owner->recordError("vendor callback exception");owner->restartRequested=true;return FALSE;}
        }
    } dealer;
    Impl(){dealer.owner=this;}
    ~Impl(){if(stopEvent)CloseHandle(stopEvent);}
    void recordError(const std::string& why){
        {std::lock_guard lock(mutex);state.error.assign(why.begin(),why.end());}
        changed.notify_all();log::warn("capture-audio-vendor",why);
    }
    bool call(Entry entry,void* a=nullptr,void* b=nullptr,bool boolean=true){
        if(!api[entry])return false;
        ForeignCall c{api[entry],entry==DeviceList?a:storage.data(),entry==DeviceList?nullptr:a,b};
        try{invoke(&c);}catch(...){recordError(std::string("C++ exception calling ")+exports[entry]);return false;}
        if(c.fault){recordError(std::format("vendor call fault entry={} seh=0x{:08X}",exports[entry],c.fault));return false;}
        if(entry==Ctor)return c.result==reinterpret_cast<uintptr_t>(storage.data());
        return !boolean||(c.result&0xff)!=0;
    }
    bool load(){
        auto root=AverMediaAudioSwitch::locateComponentRoot();
        if(root.empty()){recordError("AVerMedia Multichannel Audio component not installed");return false;}
        wchar_t absolute[32768]{};const DWORD size=GetFullPathNameW(root.c_str(),32768,absolute,nullptr);
        if(!size||size>=32768){recordError("invalid vendor component path");return false;}root=absolute;
        const auto coreDir=root+L"\\obs-plugins\\64bit",dataDir=root+L"\\data\\obs-plugins\\AVerMediaMultichannelAudio";
        for(const auto& dir:{coreDir,dataDir})if(GetFileAttributesW(dir.c_str())!=INVALID_FILE_ATTRIBUTES){
            auto cookie=AddDllDirectory(dir.c_str());if(cookie)directories.push_back(cookie);
        }
        module=LoadLibraryExW((coreDir+L"\\avt_dshow_core.dll").c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS|LOAD_LIBRARY_SEARCH_USER_DIRS);
        if(!module){recordError(std::format("LoadLibraryExW vendor audio failed error={}",GetLastError()));return false;}
        for(size_t i=0;i<api.size();++i){
            api[i]=reinterpret_cast<ForeignFn>(GetProcAddress(module,exports[i]));
            if(!api[i]){recordError(std::string("missing vendor export ")+exports[i]);return false;}
        }
        // LogHelper supplies a va_list, not an extra printable string.
        for(auto level:{"Debug","Info","Warning","Error"}){
            const auto name=std::format("?set{}Handler@LogHelper@@YAXP6AXPEBDPEAD@Z@Z",level);
            auto fn=reinterpret_cast<ForeignFn>(GetProcAddress(module,name.c_str()));
            if(fn){ForeignCall c{fn,reinterpret_cast<void*>(vendorLog)};invoke(&c);if(c.fault){recordError("vendor log handler installation fault");return false;}}
        }
        log::info("capture-audio-vendor",std::format("component root=\"{}\" required exports={}",narrow(root),api.size()));return true;
    }
    bool setup(){
        {std::lock_guard lock(mutex);state.starting=true;state.error.clear();}
        {std::lock_guard lock(decodeMutex);tearingDown=false;carrier=std::make_unique<AverMediaAudioCarrier>(onPcm);}
        storage.fill(0);constructed=call(Ctor,nullptr,nullptr,false);
        if(!constructed){recordError("vendor AudioDevice constructor failed");return false;}
        std::vector<avermedia::DeviceInfo> devices;if(!call(DeviceList,&devices,nullptr,false))return false;
        const auto selected=avermedia::selectDevice(devices,preferName,preferPath);
        if(!selected){recordError("selected AVerMedia audio device not found or ambiguous; refusing another card");return false;}
        const auto& chosen=devices[*selected];
        // AudioDevice only drives the capture graph. As in the public OBS
        // Activate path, DeviceOpener must also arm/monitor non-PCM on this
        // exact card. Keep its potentially slow SDK initialization on our STA.
        if(!nonPcmSwitch.apply(chosen.friendlyName,chosen.devicePath,false))
            log::warn("capture-audio-vendor",std::string("non-PCM switch unavailable; PCM capture remains possible: ")+nonPcmSwitch.status().detail);
        else nonPcmSwitch.alignPassthroughOnce();
        if(stopRequested)return false;
        if(!call(Reset)){recordError("vendor ResetGraph failed");return false;}
        if(!call(Update,const_cast<std::wstring*>(&chosen.friendlyName),const_cast<std::wstring*>(&chosen.devicePath))
            ||!call(Connect)||!call(SetCallback,&dealer)||!call(Start)){
            recordError("vendor audio graph setup/start failed");return false;
        }
        nonPcmSwitch.startMonitoring();
        // The watchdog begins after Start, not during its 5-13 second preroll.
        lastCallbackAt=nowSeconds();
        {std::lock_guard lock(mutex);state.device=chosen.friendlyName;state.starting=false;state.started=state.running=true;}
        changed.notify_all();log::info("capture-audio-vendor",std::format("vendor graph started device=\"{}\"",narrow(chosen.friendlyName)));return true;
    }
    void teardown(){
        // Stop joins the vendor callback; never hold decodeMutex across it.
        {std::lock_guard lock(decodeMutex);tearingDown=true;carrier.reset();}
        if(constructed){
            // A failed external Stop cannot prove its callback thread joined.
            // Retain this one opaque graph/module until process exit instead
            // of unloading code underneath that thread or reusing its storage.
            if(!call(Stop)){
                quarantine=true;recordError("vendor Stop failed; graph quarantined until process exit");
            }else{
                call(SetCallback,nullptr);
                if(!call(Dtor,nullptr,nullptr,false))quarantine=true;
            }
            constructed=false;
        }
        {std::lock_guard lock(mutex);state.started=state.running=false;}
    }
    bool onCarrier(avermedia::SampleInfo info,const uint8_t* data,size_t bytes){
        std::lock_guard lock(decodeMutex);if(tearingDown||stopRequested)return true;
        const double now=nowSeconds();lastCallbackAt=now;
        const bool ok=carrier->feed({data,bytes},info,now);const auto& decoded=carrier->state();
        {std::lock_guard guard(mutex);++state.callbacks;state.carrierBytes+=bytes;
            state.bitstream=decoded.bitstream;state.channels=decoded.channels;state.sampleRate=decoded.rate;
            state.carrier=decoded.codec;state.bursts=decoded.bursts;state.decodedFrames=decoded.decodedFrames;state.pcmFrames=decoded.pcmFrames;
            if(!decoded.error.empty())state.error.assign(decoded.error.begin(),decoded.error.end());else state.error.clear();
        }
        if(!ok)restartRequested=true;return ok;
    }
    void run(){
        const HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool ok=false;
        try{
            if(FAILED(co))recordError(std::format("vendor STA init failed hr=0x{:08X}",unsigned(co)));
            else if(load()&&setup()){
                ok=true;unsigned attempts=0;
                while(!stopRequested){
                    if(stopEvent)MsgWaitForMultipleObjects(1,&stopEvent,FALSE,20,QS_ALLINPUT);
                    else std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                    if(stopRequested)break;
                    const bool stalled=(nowSeconds()-lastCallbackAt.load())*1000.0>stallMs;
                    if(stalled||restartRequested.exchange(false)){
                        if(attempts>=maxRestarts){recordError("vendor audio recovery limit reached; falling back to DirectShow");ok=false;break;}
                        ++attempts;{std::lock_guard lock(mutex);++state.losses;state.starting=true;}
                        teardown();if(quarantine){ok=false;break;}if(stopRequested)break;if(!setup()){ok=false;break;}
                        {std::lock_guard lock(mutex);++state.restarts;}
                    }
                }
            }
        }catch(...){recordError("vendor audio worker exception");ok=false;}
        try{teardown();}catch(...){ok=false;}
        nonPcmSwitch.stop();
        if(module&&!quarantine){FreeLibrary(module);module=nullptr;}
        for(auto cookie:directories)RemoveDllDirectory(cookie);directories.clear();api.fill(nullptr);
        {std::lock_guard lock(mutex);state.starting=state.started=state.running=false;state.failed=!ok&&!stopRequested;
            if(state.failed&&state.error.empty())state.error=L"vendor audio graph unavailable";}
        changed.notify_all();if(SUCCEEDED(co))CoUninitialize();
    }
};

AverMediaVendorAudio::AverMediaVendorAudio(unsigned stallMs,unsigned maxRestarts):p_(std::make_unique<Impl>()){
    p_->stallMs=std::max(50u,stallMs);p_->maxRestarts=maxRestarts;
}
AverMediaVendorAudio::~AverMediaVendorAudio(){stop();if(p_->quarantine)p_.release();}
bool AverMediaVendorAudio::start(PcmCallback callback,const std::wstring& name,const std::wstring& path){
    if(p_->worker.joinable()){const auto s=state();return !s.failed&&(s.starting||s.running);}
    if(p_->quarantine)return false;
    if(!avermedia::eligible(name,path,0)){p_->recordError("selected input is not an AVerMedia capture device");return false;}
    if(AverMediaAudioSwitch::locateComponentRoot().empty()){p_->recordError("AVerMedia Multichannel Audio component not installed");return false;}
    p_->onPcm=std::move(callback);p_->preferName=name;p_->preferPath=path;p_->stopRequested=false;p_->restartRequested=false;
    if(p_->stopEvent)ResetEvent(p_->stopEvent);
    {std::lock_guard lock(p_->mutex);p_->state={};p_->state.starting=true;}
    try{p_->worker=std::thread([this]{p_->run();});}
    catch(...){p_->recordError("cannot start vendor audio worker");std::lock_guard lock(p_->mutex);p_->state.starting=false;p_->state.failed=true;return false;}
    std::unique_lock lock(p_->mutex);
    p_->changed.wait_for(lock,std::chrono::milliseconds(150),[this]{return !p_->state.starting;});
    // Pending Start is asynchronous startup, not proof that PCM exists.
    return !p_->state.failed&&(p_->state.starting||p_->state.running);
}
void AverMediaVendorAudio::stop(){
    if(!p_||!p_->worker.joinable())return;p_->stopRequested=true;if(p_->stopEvent)SetEvent(p_->stopEvent);p_->worker.join();
}
bool AverMediaVendorAudio::running()const{std::lock_guard lock(p_->mutex);return p_->state.running;}
AverMediaVendorAudioState AverMediaVendorAudio::state()const{std::lock_guard lock(p_->mutex);return p_->state;}
} // namespace veyra::source
