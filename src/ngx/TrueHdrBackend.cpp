#include "veyra/ngx/TrueHdrBackend.h"
#include "veyra/Log.h"
#include <filesystem>
#include <format>

namespace veyra::ngx {
namespace {
NVSDK_NGX_Result invoke(int op,ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h,unsigned& seh){
    seh=0;
    __try {
        if(op==0)return NVSDK_NGX_D3D12_CreateFeature(list,NVSDK_NGX_Feature_Reserved14,p,h);
        if(op==1)return NVSDK_NGX_D3D12_EvaluateFeature(list,*h,p);
        return NVSDK_NGX_D3D12_ReleaseFeature(*h);
    } __except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
bool report(int op,NVSDK_NGX_Result r,unsigned seh,bool always=true){
    const bool ok=NVSDK_NGX_SUCCEED(r)&&!seh;
    const auto message=std::format("op={} result=0x{:X} seh=0x{:X}",op,unsigned(r),seh);
    if(!ok)log::error("video-hdr",message);
    else if(always||log::verboseFrameLogs())log::info("video-hdr",message);
    return ok;
}
NVSDK_NGX_Result getCapabilities(NVSDK_NGX_Parameter** caps,unsigned& seh){
    __try {
        return NVSDK_NGX_D3D12_GetCapabilityParameters(caps);
    } __except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
// GetCapabilityParameters hands out a block that NgxCoreHost never allocated,
// so it must not go through the core's allocate/destroy tracking table.
NVSDK_NGX_Result destroyCapabilities(NVSDK_NGX_Parameter* caps,unsigned& seh){
    seh=0;
    __try {return NVSDK_NGX_D3D12_DestroyParameters(caps);}
    __except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
NVSDK_NGX_Result getCapability(NVSDK_NGX_Parameter* caps,const char* key,int* value,unsigned& seh){
    __try {return caps->Get(key,value);}
    __except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
void capability(){
    NVSDK_NGX_Parameter* caps=nullptr;unsigned seh=0;
    const auto r=getCapabilities(&caps,seh);
    report(3,r,seh);
    if(!caps||seh)return;
    // Query results are logged individually: missing keys are not driver errors.
    for(const auto* key:{"TrueHDR.Available","TrueHDR.NeedsUpdatedDriver","TrueHDR.MinDriverVersionMajor","TrueHDR.MinDriverVersionMinor","TrueHDR.FeatureInitResult"}){
        int value=0;const auto result=getCapability(caps,key,&value,seh);
        log::info("video-hdr",std::format("capability {}={} result=0x{:X} seh=0x{:X}",key,value,unsigned(result),seh));
        if(seh)break;
    }
    unsigned destroySeh=0;
    const auto destroyed=destroyCapabilities(caps,destroySeh);
    const auto message=std::format("capability parameters destroyed result=0x{:X} seh=0x{:X}",unsigned(destroyed),destroySeh);
    if(NVSDK_NGX_SUCCEED(destroyed)&&!destroySeh)log::info("video-hdr",message);else log::error("video-hdr",message);
}
}
bool TrueHdrBackend::create(NgxCoreHost& core,ID3D12GraphicsCommandList* list,const std::wstring& directory){
    release();
    const auto path=std::filesystem::path(directory)/L"nvngx_truehdr.dll";
    if(!path.is_absolute())return false;
    module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module_){log::error("video-hdr",std::format("absolute runtime load failed win32={}",GetLastError()));return false;}
    log::info("video-hdr","loaded nvngx_truehdr.dll from explicit runtime directory; input=sRGB/RGBA8 output=linear-BT709/scRGB-FP16 (1=80nits)");
    capability();
    core_=&core;Status st=Status::Ok;params_=core.allocateParameters(st);if(!params_)return false;
    params_->Set("CreationNodeMask",1u);params_->Set("VisibilityNodeMask",1u);
    unsigned seh=0;const auto result=invoke(0,list,params_,&handle_,seh);
    return report(0,result,seh)&&handle_;
}
bool TrueHdrBackend::evaluate(ID3D12GraphicsCommandList* list,ID3D12Resource* input,ID3D12Resource* output,const engine::VideoHdrSettings& s){
    if(!handle_||!input||!output||!s.valid())return false;
    const auto a=input->GetDesc(),b=output->GetDesc();
    if(a.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||b.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||a.Width!=b.Width||a.Height!=b.Height){
        log::error("video-hdr",std::format("invalid resource contract input={}x{}/{} output={}x{}/{}",a.Width,a.Height,unsigned(a.Format),b.Width,b.Height,unsigned(b.Format)));return false;
    }
    params_->Set("Input1",input);params_->Set("Output",output);
    params_->Set("TrueHDR.InLeft",0u);params_->Set("TrueHDR.InTop",0u);
    params_->Set("TrueHDR.InRight",unsigned(a.Width));params_->Set("TrueHDR.InBottom",a.Height);
    params_->Set("TrueHDR.OutLeft",0u);params_->Set("TrueHDR.OutTop",0u);
    params_->Set("TrueHDR.OutRight",unsigned(b.Width));params_->Set("TrueHDR.OutBottom",b.Height);
    params_->Set("TrueHDR.Contrast",s.contrast);params_->Set("TrueHDR.Saturation",s.saturation);
    params_->Set("TrueHDR.MiddleGray",s.middleGray);params_->Set("TrueHDR.MaxLuminance",s.peakNits);
    if(first_||log::verboseFrameLogs())log::info("video-hdr",std::format("evaluate {}x{} contrast={} saturation={} middleGray={} peakNits={}",a.Width,a.Height,s.contrast,s.saturation,s.middleGray,s.peakNits));
    unsigned seh=0;const auto result=invoke(1,list,params_,&handle_,seh);
    const bool ok=report(1,result,seh,first_);first_=false;return ok;
}
void TrueHdrBackend::release(){
    if(handle_){unsigned seh=0;const auto r=invoke(2,nullptr,params_,&handle_,seh);report(2,r,seh);handle_=nullptr;}
    if(params_&&core_)core_->destroyParameters(params_);params_=nullptr;core_=nullptr;
    if(module_)FreeLibrary(module_);module_=nullptr;first_=true;
}
}
