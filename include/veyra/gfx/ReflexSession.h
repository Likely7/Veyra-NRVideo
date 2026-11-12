#pragma once
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include "veyra/Log.h"
namespace veyra::gfx {
// Renamed public NVAPI ABI subset: NVIDIA/nvapi
// 87dca625e83fd89a983e19b904e5f3a580da90d2 (MIT), see THIRD_PARTY_NOTICES.
// Owner-thread only. One Sleep per SOURCE input, never per generated Present.
class ReflexSession {
public:
    struct SleepParams {uint32_t version;uint8_t low,boost;uint32_t interval;uint8_t markers,minQueue,reserved[30];};
    struct MarkerParams {uint32_t version;uint64_t frame;uint32_t type;uint64_t reserved0;uint8_t reserved[56];};
    static_assert(sizeof(SleepParams)==44&&sizeof(MarkerParams)==88);
    using Set=int(__cdecl*)(IUnknown*,SleepParams*);
    using Sleep=int(__cdecl*)(IUnknown*);
    using Marker=int(__cdecl*)(IUnknown*,MarkerParams*);
    using Notify=int(__cdecl*)(ID3D12CommandQueue*,uint32_t);
    // Explicit injection for lifecycle/failed-driver unit tests; production uses System32 NVAPI.
    struct DriverApi {Set set=nullptr;Sleep sleep=nullptr;Marker marker=nullptr;Notify notify=nullptr;};
    ReflexSession()=default;
    explicit ReflexSession(DriverApi api):api_(api),initialized_(true){}
    ReflexSession(const ReflexSession&)=delete;
    ReflexSession& operator=(const ReflexSession&)=delete;
    ~ReflexSession(){close();}
    bool active()const{return active_;}
    bool disablePending()const{return driverEnabled_&&!active_;}
    bool failed()const{return failed_;}
    uint64_t sleepCount()const{return sleeps_;}
    uint64_t generatedPresents()const{return generatedPresents_;}
    void close(){
        if(!disable())log::warn("reflex","device close while driver disable failed; device teardown must clear state");
        device_=nullptr;queue_=nullptr;driverEnabled_=false;
    }
    bool disable(){
        active_=false;
        if(driverEnabled_&&api_.set){
            SleepParams p{};p.version=sizeof(p)|(1u<<16);const int r=api_.set(device_,&p);
            log::info("reflex",std::format("disable status={} successfulSleeps={} sleepMeanMs={:.3f} sleepMaxMs={:.3f} successfulMarkers={} oobMarkers={} realPresents={} generatedPresents={}",
                r,sleeps_,sleeps_?sleepUs_/1000.0/double(sleeps_):0.0,maxSleepUs_/1000.0,markers_,oobMarkers_,realPresents_,generatedPresents_));
            if(r){failed_=true;return false;}driverEnabled_=false;
        }
        return true;
    }
    bool enable(ID3D12Device* device,bool generated=false,ID3D12CommandQueue* queue=nullptr){
        if(active_&&device_==device&&generated_==generated&&queue_==queue)return true;
        if(!disable())return false;
        device_=device;queue_=queue;failed_=false;queueMarkers_=false;generated_=generated;
        if(!load()||!api_.set||!api_.sleep||!api_.marker){log::warn("reflex","required system-driver low-latency interfaces unavailable");return false;}
        if(generated&&(!api_.notify||!queue)){
            log::warn("reflex","frame generation needs out-of-band notification and an owned presentation queue");return false;
        }
        if(queue&&api_.notify){
            const int r=api_.notify(queue,3); // OUT_OF_BAND_RENDER_PRESENT
            log::info("reflex",std::format("present queue out-of-band status={}",r));
            // Only generated frames need the out-of-band queue. Source Sleep and
            // in-band markers ran without it before frame generation support.
            if(r&&generated)return false;
            queueMarkers_=r==0;
        }
        SleepParams p{};p.version=sizeof(p)|(1u<<16);p.low=1;p.boost=0;p.markers=1; // Boost is deliberately unavailable; keep the NVAPI ABI field zero.
        const int r=api_.set(device_,&p);
        log::info("reflex",std::format("enable status={} intervalUs=0 boost={} frameGeneration={} queueMarkers={}",r,false,generated,queueMarkers_));
        sleeps_=sleepUs_=maxSleepUs_=markers_=oobMarkers_=realPresents_=generatedPresents_=0;firstFrame_=next_+1;
        return active_=driverEnabled_=r==0;
    }
    void mark(uint64_t frame,uint32_t type){
        if(!eligible(frame))return;
        MarkerParams p{};p.version=sizeof(p)|(1u<<16);p.frame=frame;p.type=type;
        const int r=api_.marker(device_,&p);
        if(r)fail("marker",r);else{++markers_;if(frame<firstFrame_+2)log::info("reflex-marker",std::format("source={} type={} status=0",frame,type));}
    }
    uint64_t begin(){
        if(!active_)return 0;
        // How long the driver really holds the source read, for the disable log.
        const auto start=std::chrono::steady_clock::now();
        const int r=api_.sleep(device_);if(r){fail("sleep",r);return 0;}
        const auto us=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
        ++sleeps_;sleepUs_+=us;maxSleepUs_=std::max(maxSleepUs_,us);
        const auto frame=++next_;mark(frame,0);return active_?frame:0;
    }
    void renderStart(uint64_t frame){mark(frame,1);mark(frame,2);}
    void renderEnd(uint64_t frame){if(queueMarkers_)mark(frame,3);}
    void blitStart(uint64_t frame){if(eligible(frame))++presentId_;if(queueMarkers_)queueMark(frame,9);}
    void blitEnd(uint64_t frame){if(queueMarkers_)queueMark(frame,10);}
    void presentStart(uint64_t frame,bool generated){
        if(!eligible(frame))return;
        if(generated)queueMark(frame,11);
        else{if(!queueMarkers_)mark(frame,3);mark(frame,4);}
    }
    void presentEnd(uint64_t frame,bool generated){
        if(!eligible(frame))return;
        if(generated){queueMark(frame,12);if(active_)++generatedPresents_;}
        else{mark(frame,5);if(active_)++realPresents_;}
    }
private:
    bool eligible(uint64_t frame)const{return active_&&frame>=firstFrame_&&frame<=next_;}
    void fail(const char* op,int result){log::warn("reflex",std::format("{} status={}; low latency disabled",op,result));failed_=true;disable();}
    void queueMark(uint64_t frame,uint32_t type){
        if(!eligible(frame)||!queueMarkers_)return;
        // Matches NVIDIA Streamline sl.chi/d3d12.cpp::setLatencyMarker:
        // the public IUnknown target is the out-of-band command queue.
        MarkerParams p{};p.version=sizeof(p)|(1u<<16);p.frame=frame;p.type=type;
        const int r=api_.marker(queue_,&p);if(r)fail("queue-marker",r);else{++oobMarkers_;if(frame<firstFrame_+2)log::info("reflex-oob",std::format("source={} present={} type={} status=0",frame,presentId_,type));}
    }
    bool load(){
        if(initialized_)return true;
        // Keep NVAPI initialized/pinned through D3D12 device teardown. Unloading
        // it before the device has caused driver callbacks into unloaded code.
        wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
        const std::wstring path=std::wstring(system)+L"\\nvapi64.dll";
        if(!module_)module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE pinned=nullptr;
        if(module_)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module_),&pinned);
        using Query=void*(__cdecl*)(uint32_t);using Init=int(__cdecl*)();
        auto query=module_?reinterpret_cast<Query>(GetProcAddress(module_,"nvapi_QueryInterface")):nullptr;if(!query)return false;
        auto init=reinterpret_cast<Init>(query(0x0150E828));
        api_={reinterpret_cast<Set>(query(0xac1ca9e0)),reinterpret_cast<Sleep>(query(0x852cd1d2)),reinterpret_cast<Marker>(query(0xd9984c05)),reinterpret_cast<Notify>(query(0x03d6e8cb))};
        if(!init)return false;const int r=init();initialized_=r==0;if(r)log::warn("reflex",std::format("initialize status={}",r));return initialized_;
    }
    DriverApi api_{};HMODULE module_=nullptr;ID3D12Device* device_=nullptr;ID3D12CommandQueue* queue_=nullptr;
    bool initialized_=false,active_=false,driverEnabled_=false,generated_=false,queueMarkers_=false,failed_=false;
    uint64_t next_=0,firstFrame_=1,presentId_=0,sleeps_=0,sleepUs_=0,maxSleepUs_=0,markers_=0,oobMarkers_=0,realPresents_=0,generatedPresents_=0;
};
}
