#include "veyra/ngx/DlssSrBackend.h"

#include <windows.h>

// NGX SDK helper headers trigger /W4 warnings; suppress them for this TU.
#pragma warning(push, 0)
#include <nvsdk_ngx_helpers.h>
#pragma warning(pop)

#include <format>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/ngx/NgxCoreHost.h"

namespace veyra::ngx {

namespace {

__declspec(noinline) NVSDK_NGX_Result CallReleaseDlss(NVSDK_NGX_Handle* handle,uint32_t& sehCode){
    sehCode=0;
    __try{return NVSDK_NGX_D3D12_ReleaseFeature(handle);}
    __except(EXCEPTION_EXECUTE_HANDLER){sehCode=uint32_t(GetExceptionCode());return NVSDK_NGX_Result_FAIL_PlatformError;}
}

__declspec(noinline) NVSDK_NGX_Result CallCreateDlss(
    ID3D12GraphicsCommandList* cmdList,
    unsigned int creationNodeMask,
    unsigned int visibilityNodeMask,
    NVSDK_NGX_Handle** handle,
    NVSDK_NGX_Parameter* params,
    NVSDK_NGX_DLSS_Create_Params* createParams,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NGX_D3D12_CREATE_DLSS_EXT(cmdList, creationNodeMask,
            visibilityNodeMask, handle, params, createParams);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallEvaluateDlss(
    ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Handle* handle,
    NVSDK_NGX_Parameter* params,
    NVSDK_NGX_D3D12_DLSS_Eval_Params* evalParams,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NGX_D3D12_EVALUATE_DLSS_EXT(cmdList, handle, params, evalParams);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

} // namespace

DlssSrBackend::~DlssSrBackend()
{
    release();
}

bool DlssSrBackend::create(NgxCoreHost& coreHost,
                           ID3D12GraphicsCommandList* cmdList,
                           NVSDK_NGX_Parameter* params,
                           const CreateDesc& desc,
                           Status& status)
{
    (void)coreHost; // NGX core already initialized; SR creates through the core API
    if (handle_ != nullptr) {
        release();
    }
    inputWidth_ = desc.inputWidth;
    inputHeight_ = desc.inputHeight;
    outputWidth_ = desc.outputWidth;
    outputHeight_ = desc.outputHeight;

    // 1:1 bypass: never create SR for same-resolution (Playbook section 11).
    // Check BOTH dimensions for non-uniform extent correctness.
    if (shouldBypass(desc.inputWidth, desc.inputHeight, desc.outputWidth, desc.outputHeight)) {
        log::info("ngx", std::format("sr-backend: bypass ({}x{} == {}x{})",
            desc.inputWidth, desc.inputHeight, desc.outputWidth, desc.outputHeight));
        return true;
    }

    NVSDK_NGX_DLSS_Create_Params createParams{};
    createParams.Feature.InWidth = desc.inputWidth;
    createParams.Feature.InHeight = desc.inputHeight;
    createParams.Feature.InTargetWidth = desc.outputWidth;
    createParams.Feature.InTargetHeight = desc.outputHeight;
    createParams.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(desc.perfQuality);
    // Linear RGBA16F requires IsHDR even for SDR media (NGX guide 3.1).
    // Video has no renderer exposure texture; use the documented GPU estimator.
    createParams.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
        NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    createParams.InEnableOutputSubrects = desc.enableOutputSubrects ? 1 : 0;

    log::info("ngx",std::format("sr-backend: linearInput=true flags=0x{:X} exposure=auto mvUnits=output-pixels jitter=source-sampling",createParams.InFeatureCreateFlags));
    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallCreateDlss(cmdList, 1, 1, &handle_,
        params, &createParams, sehCode);
    createResult_ = static_cast<uint64_t>(result);

    log::info("ngx", std::format("sr-backend: Create DLSS {}x{} -> {}x{} result={} handle={} seh={}",
        desc.inputWidth, desc.inputHeight, desc.outputWidth, desc.outputHeight,
        ngxResultString(createResult_), handle_ != nullptr ? "non-null" : "null", sehCode));

    if (result != NVSDK_NGX_Result_Success) {
        log::error("ngx", std::format("sr-backend failed result=0x{:X} seh=0x{:X}", static_cast<unsigned>(result), sehCode));
        // Query FeatureInitResult for diagnostic detail (user directive).
        int featureInitResult = 0;
        const NVSDK_NGX_Result gir = params->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &featureInitResult);
        log::info("ngx", std::format("sr-backend: FeatureInitResult Get=0x{:X} value={} (0x{:X})",
            static_cast<uint64_t>(gir), featureInitResult, featureInitResult));
        status = Status::DeviceFailure;
        return false;
    }
    return true;
}

bool DlssSrBackend::release()
{
    bool ok=true;
    if (handle_ != nullptr) {
        // SR is a core NGX feature; release through the core API.
        uint32_t seh=0;const auto result=CallReleaseDlss(handle_,seh);
        ok=result==NVSDK_NGX_Result_Success&&seh==0;
        (ok ? log::info : log::error)("ngx", std::format("sr-backend: ReleaseFeature result={} seh={}", ngxResultString(static_cast<uint64_t>(result)),seh));
        handle_ = nullptr;
    }
    return ok;
}

bool DlssSrBackend::evaluate(ID3D12GraphicsCommandList* cmdList,
                             NVSDK_NGX_Parameter* params,
                             const EvalDesc& desc,
                             Status& status)
{
    // 1:1 bypass: skip Evaluate entirely (Playbook section 11).
    if (shouldBypass(inputWidth_, inputHeight_, outputWidth_, outputHeight_)) {
        ++bypassCount_;
        return true;
    }

    if (handle_ == nullptr) {
        status = Status::InvalidArgument;
        return false;
    }

    NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams{};
    evalParams.Feature.pInColor = desc.color;
    evalParams.Feature.pInOutput = desc.output;
    evalParams.pInDepth = desc.depth;
    evalParams.pInMotionVectors = desc.motionVectors;
    evalParams.InJitterOffsetX = desc.jitterOffsetX;
    evalParams.InJitterOffsetY = desc.jitterOffsetY;
    evalParams.Feature.InSharpness = desc.sharpness;
    evalParams.InReset = desc.reset ? 1 : 0;
    evalParams.InMVScaleX = 1.0f;
    evalParams.InMVScaleY = 1.0f;
    evalParams.InPreExposure = 1.0f;
    evalParams.InExposureScale = 1.0f;
    evalParams.InRenderSubrectDimensions.Width = inputWidth_;
    evalParams.InRenderSubrectDimensions.Height = inputHeight_;

    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallEvaluateDlss(cmdList, handle_, params, &evalParams, sehCode);
    if(result != NVSDK_NGX_Result_Success || log::verboseFrameLogs())
        log::info("ngx", std::format("sr-backend: Evaluate #{} result={} seh={}",
            evaluateCount_ + 1, ngxResultString(static_cast<uint64_t>(result)), sehCode));

    if (result != NVSDK_NGX_Result_Success) {
        log::error("ngx", std::format("sr-backend failed result=0x{:X} seh=0x{:X}", static_cast<unsigned>(result), sehCode));
        status = Status::DeviceFailure;
        return false;
    }
    ++evaluateCount_;
    return true;
}

bool DlssSrBackend::isBypass() const
{
    return shouldBypass(inputWidth_, inputHeight_, outputWidth_, outputHeight_);
}

} // namespace veyra::ngx
