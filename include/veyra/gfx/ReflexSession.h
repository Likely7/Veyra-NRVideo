#pragma once
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <format>
#include "veyra/Log.h"
namespace veyra::gfx {
// Minimal ABI declarations checked against NVIDIA/nvapi public interfaces.
// Source: NVIDIA/nvapi 87dca625e83fd89a983e19b904e5f3a580da90d2 (MIT).
// Renamed subset and lifecycle integration; see THIRD_PARTY_NOTICES.md.
// No SDK header or runtime is distributed in the source tree.
class ReflexSession {
    struct SleepParams {uint32_t version;uint8_t low,boost;uint32_t interval;uint8_t markers,minQueue,reserved[30];};
    struct MarkerParams {uint32_t version;uint64_t frame;uint32_t type;uint64_t reserved0;uint8_t reserved[56];};
    static_assert(sizeof(SleepParams)==44&&sizeof(MarkerParams)==88);
    using Query=void*(__cdecl*)(uint32_t);
    using Init=int(__cdecl*)();
    using Set=int(__cdecl*)(IUnknown*,SleepParams*);
    using Sleep=int(__cdecl*)(IUnknown*);
    using Marker=int(__cdecl*)(IUnknown*,MarkerParams*);
    HMODULE module_=nullptr;ID3D12Device* device_=nullptr;
    Set set_=nullptr;Sleep sleep_=nullptr;Marker marker_=nullptr;
    bool initialized_=false,active_=false,driverEnabled_=false;uint64_t next_=0,sleeps_=0,markers_=0;
public:
    ~ReflexSession(){close();}   // the module stays pinned, see close()
    bool active()const{return active_;}
    bool disablePending()const{return driverEnabled_&&!active_;}
    // nvapi64.dll stays loaded and initialized for the life of the process: the driver keeps
    // per-device sleep-mode state that is called back when the D3D12 device is finally
    // released. NvAPI_Unload + FreeLibrary here (on every re-enable and at shutdown) could
    // leave that callback pointing into an unloaded module; a field dump of 2026-10-01
    // crashed in D3D12Core on exit, reading code just past the NVIDIA user-mode driver.
    void close(){disable();device_=nullptr;driverEnabled_=false;}
    bool disable(){
        active_=false;
        if(driverEnabled_&&set_){SleepParams p{};p.version=sizeof(p)|(1u<<16);const int r=set_(device_,&p);log::info("reflex",std::format("disable status={} successfulSleeps={} successfulMarkers={}",r,sleeps_,markers_));if(r)return false;driverEnabled_=false;}
        return true;
    }
    bool enable(ID3D12Device* device){
        if(active_&&device_==device)return true;
        if(!disable())return false;
        close();device_=device;
        if(!initialized_){
            if(!module_){
                wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
                const std::wstring path=std::wstring(system)+L"\\nvapi64.dll";
                module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                HMODULE pinned=nullptr;   // never unloaded, see close()
                if(module_)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module_),&pinned);
            }
            auto query=module_?reinterpret_cast<Query>(GetProcAddress(module_,"nvapi_QueryInterface")):nullptr;
            if(!query)return false;
            auto init=reinterpret_cast<Init>(query(0x0150E828));
            set_=reinterpret_cast<Set>(query(0xac1ca9e0));sleep_=reinterpret_cast<Sleep>(query(0x852cd1d2));marker_=reinterpret_cast<Marker>(query(0xd9984c05));
            if(!init||!set_||!sleep_||!marker_)return false;
            const int initialized=init();initialized_=initialized==0;
            if(!initialized_){log::warn("reflex",std::format("initialize status={}",initialized));return false;}
        }
        SleepParams p{};p.version=sizeof(p)|(1u<<16);p.low=1;p.markers=1;
        const int result=set_(device_,&p);log::info("reflex",std::format("enable status={} intervalUs=0 boost=0",result));
        sleeps_=markers_=0;return active_=driverEnabled_=result==0;
    }
    void mark(uint64_t frame,uint32_t type){if(!active_||!frame)return;MarkerParams p{};p.version=sizeof(p)|(1u<<16);p.frame=frame;p.type=type;const int r=marker_(device_,&p);if(r){log::warn("reflex",std::format("marker frame={} type={} status={}",frame,type,r));disable();}else ++markers_;}
    uint64_t begin(){if(!active_)return 0;const int r=sleep_(device_);if(r){log::warn("reflex",std::format("sleep status={}",r));disable();return 0;}++sleeps_;const auto frame=++next_;mark(frame,0);mark(frame,1);mark(frame,2);return frame;}
};
}
