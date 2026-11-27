#include "veyra/gfx/XessSrBackend.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

#ifdef VEYRA_HAS_XESS_SR
#include <xess/xess_d3d12.h>
#endif

namespace veyra::gfx {

#ifdef VEYRA_HAS_XESS_SR
namespace {
auto xessSrRuntimePath(){
    return runtime::localDataDirectory()/"intel"/"experimental"/"libxess.dll";
}
}
#endif

struct XessSrBackend::Impl {
#ifdef VEYRA_HAS_XESS_SR
    HMODULE dll=nullptr;
    xess_context_handle_t context=nullptr;
    xess_2d_t input{};
    decltype(&xessD3D12Execute) evaluate=nullptr;
    decltype(&xessDestroyContext) destroy=nullptr;

    ~Impl(){
        if(context&&destroy){
            const auto result=destroy(context);
            if(result!=XESS_RESULT_SUCCESS)log::warn("xess-sr",std::format("DestroyContext result={}",int(result)));
        }
        if(dll)FreeLibrary(dll);
    }
#endif
};

XessSrBackend::XessSrBackend()=default;
XessSrBackend::~XessSrBackend()=default;

bool XessSrBackend::runtimeAvailable(){
#ifdef VEYRA_HAS_XESS_SR
    std::error_code ec;
    return std::filesystem::is_regular_file(xessSrRuntimePath(),ec);
#else
    return false;
#endif
}

bool XessSrBackend::initialize(ID3D12Device* device,uint32_t inputWidth,uint32_t inputHeight,
                               uint32_t outputWidth,uint32_t outputHeight) {
#ifdef VEYRA_HAS_XESS_SR
    if(!device||!inputWidth||!inputHeight||!outputWidth||!outputHeight||p_)return false;
    auto pending=std::make_unique<Impl>();
    const auto path=std::filesystem::absolute(xessSrRuntimePath());
    pending->dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!pending->dll){
        const auto error=GetLastError();
        log::warn("xess-sr",std::format("LoadLibrary {} failed win32={}",path.string(),error));
        return false;
    }

    const auto module=pending->dll;
    const auto create=reinterpret_cast<decltype(&xessD3D12CreateContext)>(GetProcAddress(module,"xessD3D12CreateContext"));
    const auto getRange=reinterpret_cast<decltype(&xessGetOptimalInputResolution)>(GetProcAddress(module,"xessGetOptimalInputResolution"));
    const auto init=reinterpret_cast<decltype(&xessD3D12Init)>(GetProcAddress(module,"xessD3D12Init"));
    const auto getVersion=reinterpret_cast<decltype(&xessGetVersion)>(GetProcAddress(module,"xessGetVersion"));
    const auto getIntelVersion=reinterpret_cast<decltype(&xessGetIntelXeFXVersion)>(GetProcAddress(module,"xessGetIntelXeFXVersion"));
    pending->evaluate=reinterpret_cast<decltype(pending->evaluate)>(GetProcAddress(module,"xessD3D12Execute"));
    pending->destroy=reinterpret_cast<decltype(pending->destroy)>(GetProcAddress(module,"xessDestroyContext"));
    if(!create||!getRange||!init||!getVersion||!pending->evaluate||!pending->destroy){
        log::error("xess-sr","missing a required XeSS SR export");
        return false;
    }
    const auto created=create(device,&pending->context);
    if(created<0||!pending->context){
        log::error("xess-sr",std::format("CreateContext result={}",int(created)));
        return false;
    }
    pending->input={inputWidth,inputHeight};
    const xess_2d_t output{outputWidth,outputHeight};
    // Start at the highest-quality preset whose runtime-reported range fits
    // the source in both dimensions. The runtime owns the scale limits.
    constexpr xess_quality_settings_t qualities[]{
        XESS_QUALITY_SETTING_AA, XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS,
        XESS_QUALITY_SETTING_ULTRA_QUALITY, XESS_QUALITY_SETTING_QUALITY,
        XESS_QUALITY_SETTING_BALANCED, XESS_QUALITY_SETTING_PERFORMANCE,
        XESS_QUALITY_SETTING_ULTRA_PERFORMANCE
    };
    xess_quality_settings_t selected{};
    for(auto quality:qualities){
        xess_2d_t optimal{},min{},max{};
        if(getRange(pending->context,&output,quality,&optimal,&min,&max)<0)continue;
        if(inputWidth>=min.x&&inputHeight>=min.y&&inputWidth<=max.x&&inputHeight<=max.y){
            selected=quality;
            break;
        }
    }
    if(!selected){
        log::warn("xess-sr",std::format("unsupported extent {}x{} -> {}x{}",inputWidth,inputHeight,outputWidth,outputHeight));
        return false;
    }

    xess_d3d12_init_params_t params{};
    params.outputResolution=output;
    params.qualitySetting=selected;
    // Both SDR and HDR use the graph's linear FP16 working color.
    // The graph supplies render-resolution vectors and constant far depth.
    params.initFlags=XESS_INIT_FLAG_NONE;
    const auto result=init(pending->context,&params);
    if(result<0){
        log::error("xess-sr",std::format("Init result={}",int(result)));
        return false;
    }
    xess_version_t api{},intel{};
    getVersion(&api);
    if(getIntelVersion)getIntelVersion(pending->context,&intel);
    log::info("xess-sr",std::format("SDK={}.{}.{} IntelXeFX={}.{}.{} input={}x{} output={}x{} preset={} init={}",
        api.major,api.minor,api.patch,intel.major,intel.minor,intel.patch,inputWidth,inputHeight,outputWidth,outputHeight,int(selected),int(result)));
    p_=std::move(pending);
    return true;
#else
    (void)device;(void)inputWidth;(void)inputHeight;(void)outputWidth;(void)outputHeight;
    log::warn("xess-sr","Intel XeSS SR SDK unavailable at build time");
    return false;
#endif
}

bool XessSrBackend::evaluate(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* motion,
                             ID3D12Resource* depth,ID3D12Resource* output,bool reset) {
#ifdef VEYRA_HAS_XESS_SR
    if(!p_||!list||!color||!motion||!depth||!output)return false;
    xess_d3d12_execute_params_t frame{};
    frame.pColorTexture=color;
    frame.pVelocityTexture=motion;
    frame.pDepthTexture=depth;
    frame.pOutputTexture=output;
    // A decoded video frame has no camera-projection jitter to pass through.
    frame.exposureScale=1.0f;
    frame.resetHistory=reset?1:0;
    frame.inputWidth=p_->input.x;
    frame.inputHeight=p_->input.y;
    const auto result=p_->evaluate(p_->context,list,&frame);
    if(result<0)log::error("xess-sr",std::format("Execute result={}",int(result)));
    return result>=0;
#else
    (void)list;(void)color;(void)motion;(void)depth;(void)output;(void)reset;
    return false;
#endif
}

} // namespace veyra::gfx
