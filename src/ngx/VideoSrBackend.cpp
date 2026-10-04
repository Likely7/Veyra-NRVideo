#include "veyra/ngx/VideoSrBackend.h"
#include "veyra/Log.h"
#include <windows.h>
#include <format>
namespace veyra::ngx {
namespace {
NVSDK_NGX_Result invoke(int op,ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h,unsigned& seh){
    seh=0;
    __try {
        if(op==0)return NVSDK_NGX_D3D12_CreateFeature(list,NVSDK_NGX_Feature_Reserved16,p,h);
        if(op==1)return NVSDK_NGX_D3D12_EvaluateFeature(list,*h,p);
        return NVSDK_NGX_D3D12_ReleaseFeature(*h);
    } __except(EXCEPTION_EXECUTE_HANDLER){seh=GetExceptionCode();return NVSDK_NGX_Result_FAIL_PlatformError;}
}
bool result(int op,NVSDK_NGX_Result r,unsigned seh){
    const bool ok=NVSDK_NGX_SUCCEED(r)&&!seh;
    if(!ok)log::error("video-sr",std::format("op={} result=0x{:X} seh=0x{:X}",op,unsigned(r),seh));
    else if(op!=1||log::verboseFrameLogs())log::info("video-sr",std::format("op={} result=0x{:X} seh=0x{:X}",op,unsigned(r),seh));
    return ok;
}
}
bool VideoSrBackend::create(NgxCoreHost& core,ID3D12GraphicsCommandList* list){
    release();core_=&core;Status status=Status::Ok;params_=core.allocateParameters(status);if(!params_)return false;
    params_->Set("CreationNodeMask",1u);params_->Set("VisibilityNodeMask",1u);
    unsigned seh=0;auto r=invoke(0,list,params_,&handle_,seh);return result(0,r,seh)&&handle_;
}
bool VideoSrBackend::evaluate(ID3D12GraphicsCommandList* list,ID3D12Resource* input,ID3D12Resource* output,unsigned quality){
    if(!handle_||!input||!output||quality<1||quality>4)return false;
    const auto a=input->GetDesc(),b=output->GetDesc();
    if(a.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||b.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return false;
    params_->Set("Input1",input);params_->Set("Output",output);
    params_->Set("Rect.X",0u);params_->Set("Rect.Y",0u);params_->Set("Rect.W",unsigned(a.Width));params_->Set("Rect.H",a.Height);
    params_->Set("OutRect.X",0u);params_->Set("OutRect.Y",0u);params_->Set("OutRect.W",unsigned(b.Width));params_->Set("OutRect.H",b.Height);
    params_->Set("VSR.QualityLevel",quality);
    unsigned seh=0;auto r=invoke(1,list,params_,&handle_,seh);return result(1,r,seh);
}
bool VideoSrBackend::release(){
    bool ok=true;
    if(handle_){unsigned seh=0;auto r=invoke(2,nullptr,params_,&handle_,seh);ok=result(2,r,seh);handle_=nullptr;}
    if(params_&&core_)core_->destroyParameters(params_);params_=nullptr;core_=nullptr;
    return ok;
}
}
