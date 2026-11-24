#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <mutex>
#include <cstring>
#include <string>
#include <vector>
namespace veyra::gfx {
// Variable refresh (G-SYNC / G-SYNC Compatible, or AMD FreeSync through ADL)
// state of the monitor showing a window. This query never changes driver
// settings. The user enables G-SYNC in the NVIDIA Control Panel (FreeSync in
// AMD Adrenalin); the separate explicit profile setup can adjust
// only Veyra's scope. The 可变刷新 switch only decides how Veyra presents when
// it is available (VideoPresenter::configurePresentation).
// Minimal ABI declarations checked against NVIDIA/nvapi public interfaces
// (nvapi_interface.h function IDs, NV_GET_VRR_INFO_V1 and
// NV_MONITOR_CAPABILITIES_V1 layouts), MIT; same source and loading rules as
// ReflexSession: System32 nvapi64.dll only, pinned for the life of the
// process. No SDK header or runtime is distributed.
struct VrrState {
    bool known=false;      // NVIDIA driver answered for this monitor
    bool possible=false;   // VRR mode is possible on the display (bIsVRRPossible)
    bool requested=false;  // VRR mode is requested for the display (bIsVRRRequested)
    bool enabled=false;    // VRR mode is currently active on the display (bIsVRREnabled)
    bool indicator=false;  // the driver's VRR indicator overlay is on
    bool inVrrMode=false;  // the display itself reports VRR mode
    bool capsKnown=false;  // generic monitor capabilities answered
    bool supportsVrr=false;      // the monitor supports VRR at all (supportVRR)
    bool currentlyCapable=false; // ... with the applied display settings
    bool trueGsync=false;        // G-SYNC module, not adaptive sync
    bool simulated=false;        // VEYRA_TEST_VRR_LEVEL override
    bool amd=false;              // answered by the AMD driver (ADL FreeSync), not NVAPI
    double minimumHz=0;    // current mode, when reported by the display driver
    int status=0;          // last NvAPI status (0 = OK)
    bool operator==(const VrrState&)const=default;
};
// A display may be capable before an application's VRR presentation starts.
// In particular G-SYNC Compatible can report all GetVRRInfo flags clear while
// generic monitor capabilities say that the applied mode supports VRR (#45).
// Permit the user's presentation request then, without inventing active flags
// or assuming that the NVIDIA Control Panel setting is off.
constexpr bool vrrPresentationAvailable(const VrrState& s){
    return s.enabled||s.inVrrMode||s.possible||s.requested
        ||(s.capsKnown&&s.supportsVrr&&s.currentlyCapable);
}
// 0 unknown, 1 unsupported, 2 supported but unavailable in the applied mode,
// 3 presentation may request VRR (activation unconfirmed), 4 driver reports active.
constexpr unsigned vrrLevel(const VrrState& s){
    if(!s.known)return 0;
    if(s.enabled||s.inVrrMode)return 4;
    if(vrrPresentationAvailable(s))return 3;
    if(s.supportsVrr||s.currentlyCapable)return 2;
    return 1;
}
// AMD FreeSync of one display from ADL2_Display_FreeSync_Cap (caps) and
// ADL2_Display_FreeSyncState_Get (current use-case bits, 0 = off in Adrenalin).
// ADL reports whether FreeSync is on, not whether a flip is in VRR mode, so the
// best AMD level is 3. Generic caps stay unknown: the driver answers the setting
// itself, so the #45 caps-only rule must not request VRR while it is off.
// The driver rejects the caps query for a display without FreeSync (seen as
// -1 on a non-FreeSync monitor), so that is an answer, not a failed query:
// status stays 0 and DisplayStateService does not retry it.
inline VrrState amdFreeSyncState(int capStatus,int caps,int stateStatus,int current,int minMicroHz=0,int maxMicroHz=0){
    constexpr int displaySupported=1<<2,currentModeSupported=1<<3; // ADL_FREESYNC_CAP_*
    VrrState s;s.known=true;s.amd=true;
    if(capStatus!=0)return s;
    s.supportsVrr=caps&displaySupported;s.currentlyCapable=caps&currentModeSupported;
    // vrrPresentationAvailable treats requested alone as enough, so it is set
    // only together with possible (FreeSync on and usable in this mode).
    s.possible=s.requested=s.supportsVrr&&s.currentlyCapable&&stateStatus==0&&current!=0;
    if(s.requested&&minMicroHz>=1000000&&maxMicroHz>minMicroHz)
        s.minimumHz=double(minMicroHz)/1000000.0;
    return s;
}
// Minimal ADL2 declarations checked against GPUOpen display-library (MIT,
// adl_structures.h AdapterInfo / ADLDisplayInfo / ADLFreeSyncCap, commit
// 84c881cb). Same loading rules as NVAPI: System32 atiadlxx.dll only, pinned,
// one context for the life of the process. Read-only.
inline VrrState queryAmdFreeSync(const wchar_t* gdiDevice){
    struct AdapterInfo{int size,index;char udid[256];int bus,device,function,vendor;char adapterName[256],displayName[256];int present,exist;char driverPath[256],driverPathExt[256],pnp[256];int osDisplayIndex;};
    struct DisplayInfo{int logical,physical,logicalAdapter,physicalAdapter,controller;char name[256],manufacturer[256];int type,outputType,connector,mask,value;};
    struct Cap{int caps,minMicroHz,maxMicroHz;unsigned char label;char reserved[3];int reservedEx[4];};
    static_assert(sizeof(AdapterInfo)==1572&&sizeof(DisplayInfo)==552&&sizeof(Cap)==32);
    using Alloc=void*(__stdcall*)(int);
    using Create=int(__cdecl*)(Alloc,int,void**);
    using Count=int(__cdecl*)(void*,int*);
    using Adapters=int(__cdecl*)(void*,AdapterInfo*,int);
    using Displays=int(__cdecl*)(void*,int,int*,DisplayInfo**,int);
    using GetCap=int(__cdecl*)(void*,int,int,Cap*);
    using GetState=int(__cdecl*)(void*,int,int,int*,int*,int*,int*);
    static std::once_flag once;static void* context=nullptr;static Count count=nullptr;static Adapters adapters=nullptr;
    static Displays displays=nullptr;static GetCap getCap=nullptr;static GetState getState=nullptr;
    std::call_once(once,[]{
        wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
        const std::wstring path=std::wstring(system)+L"\\atiadlxx.dll";
        HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)return;
        HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module),&pinned);
        auto create=reinterpret_cast<Create>(GetProcAddress(module,"ADL2_Main_Control_Create"));
        count=reinterpret_cast<Count>(GetProcAddress(module,"ADL2_Adapter_NumberOfAdapters_Get"));
        adapters=reinterpret_cast<Adapters>(GetProcAddress(module,"ADL2_Adapter_AdapterInfo_Get"));
        displays=reinterpret_cast<Displays>(GetProcAddress(module,"ADL2_Display_DisplayInfo_Get"));
        getCap=reinterpret_cast<GetCap>(GetProcAddress(module,"ADL2_Display_FreeSync_Cap"));
        getState=reinterpret_cast<GetState>(GetProcAddress(module,"ADL2_Display_FreeSyncState_Get"));
        if(!create||!count||!adapters||!displays||!getCap||!getState)return;
        if(create([](int size)->void*{return std::malloc(size_t(size));},1,&context)!=0)context=nullptr;
    });
    if(!context||!gdiDevice)return {};
    char device[CCHDEVICENAME*2]{};
    if(!WideCharToMultiByte(CP_ACP,0,gdiDevice,-1,device,int(sizeof(device)),nullptr,nullptr))return {};
    int adapterCount=0;
    if(count(context,&adapterCount)!=0||adapterCount<=0||adapterCount>64)return {};
    std::vector<AdapterInfo> list(static_cast<size_t>(adapterCount));for(auto& a:list)a.size=int(sizeof(AdapterInfo));
    if(adapters(context,list.data(),int(sizeof(AdapterInfo))*adapterCount)!=0)return {};
    for(int a=0;a<adapterCount;++a){
        if(std::strcmp(list[a].displayName,device)!=0)continue;
        int n=0;DisplayInfo* info=nullptr;
        if(displays(context,list[a].index,&n,&info,0)!=0){std::free(info);continue;}
        constexpr int connectedMapped=0x3; // ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | DISPLAYMAPPED
        int display=-1;
        for(int i=0;i<n;++i)if(info[i].logicalAdapter==list[a].index&&(info[i].value&connectedMapped)==connectedMapped){display=info[i].logical;break;}
        std::free(info);
        if(display<0)continue;
        Cap cap{};int current=0,defaults=0,minMicroHz=0,maxMicroHz=0;
        const int capStatus=getCap(context,list[a].index,display,&cap);
        const int stateStatus=capStatus==0?getState(context,list[a].index,display,&current,&defaults,&minMicroHz,&maxMicroHz):capStatus;
        return amdFreeSyncState(capStatus,cap.caps,stateStatus,current,minMicroHz,maxMicroHz);
    }
    return {};
}
inline VrrState queryNvidiaVrr(HWND window);
inline VrrState queryVrr(HWND window){
    // Test hook (VEYRA_TEST_VRR_LEVEL=0..4): this rig's monitor reports no VRR
    // support, so UI and presentation-rule tests simulate a level.
    static const int testLevel=[]{wchar_t v[4]{};return GetEnvironmentVariableW(L"VEYRA_TEST_VRR_LEVEL",v,4)?std::clamp(_wtoi(v),0,4):-1;}();
    if(testLevel>=0){
        VrrState s;s.simulated=true;s.known=testLevel>0;s.capsKnown=s.known;
        s.supportsVrr=testLevel>=2;s.currentlyCapable=testLevel>=3;s.possible=s.requested=testLevel>=3;s.enabled=s.inVrrMode=testLevel>=4;
        return s;
    }
    VrrState state=queryNvidiaVrr(window);
    if(state.known||!window)return state;
    // Not an NVIDIA display (or no NVAPI): ask the AMD driver.
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return state;
    VrrState amd=queryAmdFreeSync(monitor.szDevice);
    return amd.known?amd:state;
}
inline VrrState queryNvidiaVrr(HWND window){
    struct Info {uint32_t version;uint32_t flags;uint32_t reservedEx[4];};
    static_assert(sizeof(Info)==24);
    // NV_MONITOR_CAPABILITIES_V1: the union starts right after the one-byte
    // bIsValidInfo; NV_MONITOR_CAPS_VSDB (49 bytes) is its largest member.
    struct Caps {uint32_t version;uint16_t size;uint32_t infoType;uint32_t connectorType;uint8_t valid;uint8_t data[49];};
    static_assert(sizeof(Caps)==68);
    using Query=void*(__cdecl*)(uint32_t);
    using Init=int(__cdecl*)();
    using IdByName=int(__cdecl*)(const char*,uint32_t*);
    using GetVrr=int(__cdecl*)(uint32_t,Info*);
    using GetCaps=int(__cdecl*)(uint32_t,Caps*);
    static std::once_flag once;static IdByName idByName=nullptr;static GetVrr getVrr=nullptr;static GetCaps getCaps=nullptr;static bool ready=false;
    std::call_once(once,[]{
        wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
        const std::wstring path=std::wstring(system)+L"\\nvapi64.dll";
        HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)return;
        HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(module),&pinned);
        auto query=reinterpret_cast<Query>(GetProcAddress(module,"nvapi_QueryInterface"));
        if(!query)return;
        auto init=reinterpret_cast<Init>(query(0x0150E828));
        idByName=reinterpret_cast<IdByName>(query(0xAE457190));
        getVrr=reinterpret_cast<GetVrr>(query(0xDF8FDA57));
        getCaps=reinterpret_cast<GetCaps>(query(0x3B05C7E1)); // NvAPI_DISP_GetMonitorCapabilities, optional
        ready=init&&idByName&&getVrr&&init()==0;
    });
    VrrState state;
    if(!ready||!window)return state;
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return state;
    char name[CCHDEVICENAME*2]{};
    if(!WideCharToMultiByte(CP_ACP,0,monitor.szDevice,-1,name,int(sizeof(name)),nullptr,nullptr))return state;
    uint32_t displayId=0;
    state.status=idByName(name,&displayId);
    if(state.status!=0)return state;
    Info info{};info.version=uint32_t(sizeof(Info))|(1u<<16);
    state.status=getVrr(displayId,&info);
    if(state.status!=0)return state;
    state.known=true;
    state.enabled=info.flags&1u;state.possible=info.flags&2u;state.requested=info.flags&4u;
    state.indicator=info.flags&8u;state.inVrrMode=info.flags&16u;
    if(getCaps){
        Caps caps{};caps.version=uint32_t(sizeof(Caps))|(1u<<16);caps.size=uint16_t(sizeof(Caps));caps.infoType=0x1002; // NV_MONITOR_CAPS_TYPE_GENERIC
        if(getCaps(displayId,&caps)==0&&(caps.valid&1u)){
            state.capsKnown=true;
            state.supportsVrr=caps.data[0]&1u;state.trueGsync=caps.data[0]&4u;state.currentlyCapable=caps.data[0]&16u;
        }
    }
    return state;
}
}
