#include "veyra/gfx/XessPresenter.h"
#include "veyra/diagnostics/CpuStallTrace.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/XessMfgUnlock.h"
#include "veyra/gfx/XessPacing.h"
#include <atomic>
#ifdef VEYRA_HAS_XESS
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xell/xell_d3d12.h>
#endif

namespace {
std::atomic<uint64_t> nextTraceInstance{0};
int64_t traceHost(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/100;}
// Local wide->narrow for log lines (the presenter's detail strings are wide).
std::string narrowDetail(const std::wstring& value){
    if(value.empty())return {};
    const int length=WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(length<=0)return {};
    std::string text(size_t(length),'\0');
    WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),text.data(),length,nullptr,nullptr);
    return text;
}
}

namespace veyra::gfx {
struct XessPresenter::Impl {
    const bool traceEnabled=GetEnvironmentVariableW(L"VEYRA_TEST_TRACE_XESS",nullptr,0)>0;
    const uint64_t traceInstance=++nextTraceInstance;
    pipeline::FrameIdentity traceIdentity{};
    uint32_t preparationCycle=0;
    int64_t presentBegin=0;
    uint64_t generated=0,presented=0;
    uint32_t requestedGenerated=0;   // generated frames requested by the user (0 = 2X stock)
    uint32_t maxInterpolations=1;    // runtime-reported ceiling after the unlock
#ifdef VEYRA_HAS_XESS
    HMODULE fgDll=nullptr,llDll=nullptr;
    xefg_swapchain_handle_t fg=nullptr;
    xell_context_handle_t ll=nullptr;
    uint32_t id=0,nextId=0;
    void record(diagnostics::TraceKind kind,uint32_t cycle,int64_t begin,int64_t end,uint32_t count=0){
        if(!traceEnabled)return;
        diagnostics::FrameTraceEvent event;
        event.kind=kind;event.host100ns=end;
        if(kind!=diagnostics::TraceKind::XessSleep)event.identity=traceIdentity;
        event.providerInstance=traceInstance;event.providerCycle=cycle;
        event.preparationCycle=kind==diagnostics::TraceKind::XessSleep?0:preparationCycle;
        event.presentBeginHost=begin;event.presentEndHost=end;
        event.milliseconds=double(end-begin)/10000;event.count=count;
        Logger::instance().recordFrame(event);
    }
    bool cycleOpen=false,renderSubmitStarted=false,renderSubmitEnded=false;
#define XESS_PROC(name) decltype(&name) name##Fn=nullptr
    XESS_PROC(xefgSwapChainD3D12CreateContext);
    XESS_PROC(xefgSwapChainD3D12GetProperties);
    XESS_PROC(xefgSwapChainGetProperties);
    XESS_PROC(xefgSwapChainSetNumInterpolatedFrames);
    XESS_PROC(xefgSwapChainD3D12InitFromSwapChain);
    XESS_PROC(xefgSwapChainD3D12InitFromSwapChainDesc);
    XESS_PROC(xefgSwapChainD3D12GetSwapChainPtr);
    XESS_PROC(xefgSwapChainD3D12TagFrameResource);
    XESS_PROC(xefgSwapChainTagFrameConstants);
    XESS_PROC(xefgSwapChainSetEnabled);
    XESS_PROC(xefgSwapChainSetPresentId);
    XESS_PROC(xefgSwapChainGetLastPresentStatus);
    XESS_PROC(xefgSwapChainSetLatencyReduction);
    XESS_PROC(xefgSwapChainDestroy);
    XESS_PROC(xellD3D12CreateContext);
    XESS_PROC(xellDestroyContext);
    XESS_PROC(xellSetSleepMode);
    XESS_PROC(xellSleep);
    XESS_PROC(xellAddMarkerData);
#undef XESS_PROC
    bool check(int result,const char* operation,bool always=false){
        if(result!=0||always||log::verboseFrameLogs())log::info("xess-fg",std::format("{} result={} presentId={}",operation,result,id));
        return result>=0;
    }
    bool marker(xell_latency_marker_type_t m,uint32_t frameId){
        const auto result=xellAddMarkerDataFn(ll,frameId,m);
        if(result!=0||log::verboseFrameLogs())log::info("xess-fg",std::format("XeLL marker type={} frameId={} result={} presentId={}",int(m),frameId,int(result),id));
        return result>=0;
    }
    bool marker(xell_latency_marker_type_t m){return marker(m,id);}
    ~Impl(){
        if(fg&&xefgSwapChainDestroyFn)check(xefgSwapChainDestroyFn(fg),"Destroy",true);
        if(ll&&xellDestroyContextFn)check(xellDestroyContextFn(ll),"XeLL destroy",true);
        // Restore the provider bytes only after every XeFG/XeLL context is gone.
        XessMfgUnlock::release();
        // Same rule for the pacing thunk: the present hook is removed before the
        // provider module is released.
        XessPacing::release();
        if(fgDll)FreeLibrary(fgDll);if(llDll)FreeLibrary(llDll);
    }
#endif
};
XessPresenter::XessPresenter():p_(std::make_unique<Impl>()){}
XessPresenter::~XessPresenter()=default;
bool XessPresenter::initialize(ID3D12Device* device,ID3D12CommandQueue* queue,IDXGIFactory2* factory,HWND window,const DXGI_SWAP_CHAIN_DESC1& desc,IDXGISwapChain3** swapchain,uint32_t fgMultiplier,bool lowLatencySleep){
    if(GetEnvironmentVariableW(L"VEYRA_TEST_XESS_INIT_FAILURE",nullptr,0)){
        log::warn("xess-fg","test-only initialization rejection; runtime not loaded");return false;
    }
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;const auto root=runtime::localDataDirectory()/"intel"/"experimental";
    auto load=[&](const wchar_t* name){auto path=std::filesystem::absolute(root/name);
        auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        log::info("xess-fg",std::format("load {} result={} win32={}",path.string(),module!=nullptr,module?0:GetLastError()));return module;};
    p.llDll=load(L"libxell.dll");if(!p.llDll)return false;
    p.fgDll=load(L"libxess_fg.dll");if(!p.fgDll)return false;
#define LOAD(module,name) p.name##Fn=reinterpret_cast<decltype(p.name##Fn)>(GetProcAddress(p.module,#name));if(!p.name##Fn){log::error("xess-fg","missing export " #name);return false;}
    LOAD(fgDll,xefgSwapChainD3D12CreateContext)
    LOAD(fgDll,xefgSwapChainD3D12GetProperties)
    LOAD(fgDll,xefgSwapChainGetProperties)
    LOAD(fgDll,xefgSwapChainSetNumInterpolatedFrames)
    LOAD(fgDll,xefgSwapChainD3D12InitFromSwapChain)
    LOAD(fgDll,xefgSwapChainD3D12InitFromSwapChainDesc)
    LOAD(fgDll,xefgSwapChainD3D12GetSwapChainPtr)
    LOAD(fgDll,xefgSwapChainD3D12TagFrameResource)
    LOAD(fgDll,xefgSwapChainTagFrameConstants)
    LOAD(fgDll,xefgSwapChainSetEnabled)
    LOAD(fgDll,xefgSwapChainSetPresentId)
    LOAD(fgDll,xefgSwapChainGetLastPresentStatus)
    LOAD(fgDll,xefgSwapChainSetLatencyReduction)
    LOAD(fgDll,xefgSwapChainDestroy)
    LOAD(llDll,xellD3D12CreateContext)
    LOAD(llDll,xellDestroyContext)
    LOAD(llDll,xellSetSleepMode)
    LOAD(llDll,xellSleep)
    LOAD(llDll,xellAddMarkerData)
#undef LOAD
    // Multi-frame generation needs the audited OptiScaler unlock before the
    // first XeFG context exists; 2X deliberately leaves the provider untouched.
    p.requestedGenerated=fgMultiplier>1?fgMultiplier-1:0;
    // 2X is the provider's stock behaviour: no unlock, no pacing, and the
    // presenter must not treat "nothing to unlock" as a failure (that used to
    // drop the whole XeSS session back to native presentation).
    if(p.requestedGenerated>1){
        const auto unlock=XessMfgUnlock::apply(p.fgDll,p.requestedGenerated);
        p.maxInterpolations=unlock.maxInterpolations;
        veyra::log::info("xess-mfg",std::format("unlock requestedGenerated={} applied={} identityVerified={} recognisedBuild={}",
            p.requestedGenerated,unlock.applied,unlock.identityVerified,unlock.recognisedBuild));
        log::info("xess-mfg",std::format("unlock detail={}",narrowDetail(unlock.detail)));
        if(!unlock.applied){
            log::error("xess-mfg",std::format("unlock unavailable for {}X request; keeping stock presentation",p.requestedGenerated+1));
            return false;
        }
        // Above 2X the provider presents a burst of generated frames back to
        // back; hand its intermediate frames to its own frame scheduler.
        if(GetEnvironmentVariableW(L"VEYRA_DISABLE_XESS_PACING",nullptr,0)==0){
            const auto pacing=XessPacing::install(p.fgDll,p.requestedGenerated);
            log::info("xess-pacing",std::format("install requestedGenerated={} installed={} structureVerified={} detail={}",
                p.requestedGenerated,pacing.installed,pacing.structureVerified,
                narrowDetail(pacing.detail)));
        }else{
            log::warn("xess-pacing","disabled by VEYRA_DISABLE_XESS_PACING (diagnostic only)");
        }
    }
    if(!p.check(p.xellD3D12CreateContextFn(device,&p.ll),"XeLL create",true)||!p.check(p.xefgSwapChainD3D12CreateContextFn(device,&p.fg),"Create",true))return false;
    if(!p.check(p.xefgSwapChainSetLatencyReductionFn(p.fg,p.ll),"Attach XeLL",true))return false;
    xell_sleep_params_t sleep{};sleep.bLowLatencyMode=lowLatencySleep?1:0;
    if(!p.check(p.xellSetSleepModeFn(p.ll,&sleep),"XeLL sleep mode",true))return false;
    log::info("xess-fg",std::format("XeLL bLowLatencyMode={} (0 = pass-through; sleep/markers still issued per XeLL contract)",unsigned(sleep.bLowLatencyMode)));
    // Query the real ceiling before deciding what to request. The unlock (U5)
    // is what makes this report more than 1 on a non-Intel GPU.
    {
        xefg_swapchain_properties_t runtimeProperties{};
        if(p.check(p.xefgSwapChainGetPropertiesFn(p.fg,&runtimeProperties),"GetProperties(runtime)",true)){
            p.maxInterpolations=runtimeProperties.maxSupportedInterpolations>0?runtimeProperties.maxSupportedInterpolations:1;
            XessMfgUnlock::reportRuntimeCeiling(p.maxInterpolations);
        }
        if(p.requestedGenerated>0&&p.maxInterpolations<p.requestedGenerated){
            log::error("xess-mfg",std::format("requested {}X but the runtime reports maxInterpolatedFrames={}",p.requestedGenerated+1,p.maxInterpolations));
            return false;
        }
    }
    xefg_swapchain_d3d12_init_params_t init{};init.maxInterpolatedFrames=p.requestedGenerated>0?p.requestedGenerated:1;init.uiMode=XEFG_SWAPCHAIN_UI_MODE_AUTO;
    xefg_swapchain_properties_t properties{};
    // The API can wrap a caller-owned native swapchain if one is supplied.
    // Veyra's sink supplies an empty pointer after releasing the old backend;
    // independent FSR dispatch never creates or retains an HWND swapchain.
    DXGI_SWAP_CHAIN_DESC1 existingDesc{};
    IDXGISwapChain3* existing=swapchain!=nullptr?*swapchain:nullptr;
    const bool wrapExisting=existing!=nullptr&&SUCCEEDED(existing->GetDesc1(&existingDesc));
    if(wrapExisting){
        init.pApplicationSwapChain=existing;
        if(!p.check(p.xefgSwapChainD3D12GetPropertiesFn(p.fg,&init,existingDesc.Width,existingDesc.Height,existingDesc.Format,&properties),"Properties(wrap)",true)||properties.maxSupportedInterpolations<1)return false;
        log::info("xess-fg",std::format("wrapping retained swapchain {}x{} fmt={} maxInterpolations={} bufferHeap={} textureHeap={}",existingDesc.Width,existingDesc.Height,int(existingDesc.Format),properties.maxSupportedInterpolations,properties.tempBufferHeapSize,properties.tempTextureHeapSize));
        if(!p.check(p.xefgSwapChainD3D12InitFromSwapChainFn(p.fg,queue,&init),"Init(wrap)",true))return false;
    }else{
    if(!p.check(p.xefgSwapChainD3D12GetPropertiesFn(p.fg,&init,desc.Width,desc.Height,desc.Format,&properties),"Properties",true)||properties.maxSupportedInterpolations<1)return false;
    log::info("xess-fg",std::format("swapchain={}x{} maxInterpolations={} bufferHeap={} textureHeap={} estimated-motion constant-depth experimental",desc.Width,desc.Height,properties.maxSupportedInterpolations,properties.tempBufferHeapSize,properties.tempTextureHeapSize));
    if(!p.check(p.xefgSwapChainD3D12InitFromSwapChainDescFn(p.fg,window,&desc,nullptr,queue,factory,&init),"Init",true))return false;
    }
    if(p.requestedGenerated>0&&!p.check(p.xefgSwapChainSetNumInterpolatedFramesFn(p.fg,p.requestedGenerated),"SetNumInterpolatedFrames",true))return false;
    return p.check(p.xefgSwapChainD3D12GetSwapChainPtrFn(p.fg,__uuidof(IDXGISwapChain3),reinterpret_cast<void**>(swapchain)),"GetSwapChain",true);
#else
    (void)device;(void)queue;(void)factory;(void)window;(void)desc;(void)swapchain;(void)fgMultiplier;(void)lowLatencySleep;
    log::error("xess-fg","Intel XeSS SDK unavailable at build time");return false;
#endif
}
uint32_t XessPresenter::beginInput(){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;
    // XeLL tracks presentation cycles, not every queued source. Complete each
    // cycle before allocating another ID; the source queue stays asynchronous.
    if(p.cycleOpen)return p.nextId;
    const auto frameId=++p.nextId;
    diagnostics::CpuStallTrace trace("xess-begin-stall",frameId);
    const auto sleepBegin=p.traceEnabled?traceHost():0;
    const bool slept=p.check(p.xellSleepFn(p.ll,frameId),"XeLL sleep");trace.mark("sleep");
    if(p.traceEnabled)p.record(diagnostics::TraceKind::XessSleep,frameId,sleepBegin,traceHost(),slept?1:0);
    p.cycleOpen=slept&&p.marker(XELL_SIMULATION_START,frameId);
    p.renderSubmitStarted=p.renderSubmitEnded=false;
    return p.cycleOpen?frameId:0;
#else
    return 0;
#endif
}
bool XessPresenter::beginProcessing(uint32_t frameId){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;
    if(!frameId)return false;
    if(!p.cycleOpen||frameId!=p.nextId||p.renderSubmitStarted)return true;
    p.renderSubmitStarted=p.marker(XELL_SIMULATION_END,frameId)&&p.marker(XELL_RENDERSUBMIT_START,frameId);
    return p.renderSubmitStarted;
#else
    (void)frameId;return false;
#endif
}
bool XessPresenter::endProcessing(uint32_t frameId){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;
    if(!frameId)return false;
    if(!p.cycleOpen||frameId!=p.nextId||p.renderSubmitEnded)return true;
    p.renderSubmitEnded=beginProcessing(frameId)&&p.marker(XELL_RENDERSUBMIT_END,frameId);
    return p.renderSubmitEnded;
#else
    (void)frameId;return false;
#endif
}
bool XessPresenter::beginFrame(uint32_t preparedFrameId,pipeline::FrameIdentity identity){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;
    p.id=beginInput();
    p.traceIdentity=identity;p.preparationCycle=preparedFrameId;
    if(p.traceEnabled){const auto now=traceHost();p.record(diagnostics::TraceKind::XessBind,p.id,now,now);}
    if(log::verboseFrameLogs()&&preparedFrameId!=p.id)
        log::info("xess-fg",std::format("presentation cycle id={} preparationCycle={}; cycle timing is not source-frame latency",p.id,preparedFrameId));
    return p.id&&beginProcessing(p.id);
#else
    (void)preparedFrameId;(void)identity;
    return false;
#endif
}
bool XessPresenter::tag(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* motion,ID3D12Resource* depth,RECT region,bool enabled,bool reset,float elapsedMs){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;
    if(!p.check(p.xefgSwapChainSetEnabledFn(p.fg,enabled?1:0),"SetEnabled"))return false;
    if(!enabled)return true;
    if(!color||!motion||!depth||!list||region.right<=region.left||region.bottom<=region.top)return false;
    // The presenter owns these inputs until Present returns and submits on the
    // same queue. Let the provider defer its reads to Present instead of making
    // immediate staging copies while our blit list is still being recorded.
    auto resource=[&](xefg_swapchain_resource_type_t type,ID3D12Resource* tex,bool regionColor){
        xefg_swapchain_d3d12_resource_data_t data{};data.type=type;data.validity=XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
        data.pResource=tex;data.incomingState=D3D12_RESOURCE_STATE_COMMON;
        const auto d=tex->GetDesc();data.resourceSize={uint32_t(d.Width),d.Height};
        if(regionColor){data.resourceBase={uint32_t(region.left),uint32_t(region.top)};data.resourceSize={uint32_t(region.right-region.left),uint32_t(region.bottom-region.top)};}
        return p.check(p.xefgSwapChainD3D12TagFrameResourceFn(p.fg,nullptr,p.id,&data),"Tag resource");
    };
    if(!resource(XEFG_SWAPCHAIN_RES_BACKBUFFER,color,true)||!resource(XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,color,true)||!resource(XEFG_SWAPCHAIN_RES_MOTION_VECTOR,motion,false)||!resource(XEFG_SWAPCHAIN_RES_DEPTH,depth,false))return false;
    xefg_swapchain_frame_constant_data_t constants{};
    for(unsigned i=0;i<4;++i){constants.viewMatrix[i*5]=1;constants.projectionMatrix[i*5]=1;}
    constants.motionVectorScaleX=constants.motionVectorScaleY=1;
    constants.resetHistory=reset?1:0;constants.frameRenderTime=elapsedMs;
    return p.check(p.xefgSwapChainTagFrameConstantsFn(p.fg,p.id,&constants),"Tag constants");
#else
    (void)list;(void)color;(void)motion;(void)depth;(void)region;(void)enabled;(void)reset;(void)elapsedMs;return false;
#endif
}
bool XessPresenter::beforePresent(){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;const bool ok=endProcessing(p.id)&&p.check(p.xefgSwapChainSetPresentIdFn(p.fg,p.id),"SetPresentId")&&p.marker(XELL_PRESENT_START);
    p.presentBegin=ok&&p.traceEnabled?traceHost():0;return ok;
#else
    return false;
#endif
}
bool XessPresenter::afterPresent(){
#ifdef VEYRA_HAS_XESS
    auto& p=*p_;const auto presentEnd=p.traceEnabled?traceHost():0;
    if(p.traceEnabled&&p.presentBegin)p.record(diagnostics::TraceKind::XessPresent,p.id,p.presentBegin,presentEnd);
    if(!p.marker(XELL_PRESENT_END))return false;
    p.cycleOpen=false;
    xefg_swapchain_present_status_t status{};
    if(!p.check(p.xefgSwapChainGetLastPresentStatusFn(p.fg,&status),"PresentStatus"))return false;
    if(status.isFrameGenEnabled&&status.frameGenResult==XEFG_SWAPCHAIN_RESULT_SUCCESS&&status.framesPresented>1)p.generated+=status.framesPresented-1;
    if(status.frameGenResult>=0)p.presented+=status.framesPresented;
    if(log::verboseFrameLogs()||p.id%60==0||status.frameGenResult<0)log::info("xess-fg",std::format("presentId={} enabled={} result={} framesPresented={} generatedTotal={} (SDK submissions, not measured scanout)",p.id,status.isFrameGenEnabled,int(status.frameGenResult),status.framesPresented,p.generated));
    return status.frameGenResult>=0;
#else
    return false;
#endif
}
uint64_t XessPresenter::generatedCount()const{return p_->generated;}
uint64_t XessPresenter::presentedCount()const{return p_->presented;}
uint32_t XessPresenter::maxInterpolatedFrames()const{return p_->maxInterpolations;}
}
