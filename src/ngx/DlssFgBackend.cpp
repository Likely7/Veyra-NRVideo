#include "veyra/ngx/DlssFgBackend.h"

#include <windows.h>

// NGX SDK helper headers trigger /W4 warnings; suppress for this TU.
#pragma warning(push, 0)
#include <nvsdk_ngx_defs_dlssg.h>
#include <nvsdk_ngx_helpers_dlssg.h>
#pragma warning(pop)

#include <format>
#include <filesystem>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/FgCompatibilitySession.h"

namespace veyra::ngx {

namespace {

int LogDlssgException(EXCEPTION_POINTERS* exception)
{
    const auto* record = exception->ExceptionRecord;
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(record->ExceptionAddress, &memory, sizeof(memory));
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), module, MAX_PATH);
    log::error("ngx", std::format("DLSSG exception code=0x{:X} module={} rva=0x{:X} access={} address=0x{:X} rcx=0x{:X} rdx=0x{:X} r8=0x{:X} r9=0x{:X}",
        record->ExceptionCode, std::filesystem::path(module).filename().string(),
        uintptr_t(record->ExceptionAddress) - uintptr_t(memory.AllocationBase),
        record->NumberParameters ? record->ExceptionInformation[0] : 0,
        record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0,
        exception->ContextRecord->Rcx, exception->ContextRecord->Rdx,
        exception->ContextRecord->R8, exception->ContextRecord->R9));
    void* frames[24]{};
    const auto count = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        VirtualQuery(frames[i], &memory, sizeof(memory));
        GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), module, MAX_PATH);
        log::error("ngx", std::format("DLSSG exception stack={} module={} rva=0x{:X}",
            i, std::filesystem::path(module).filename().string(), uintptr_t(frames[i]) - uintptr_t(memory.AllocationBase)));
    }
    Logger::instance().flush();
    return EXCEPTION_EXECUTE_HANDLER;
}

// SEH-isolated calls into the proprietary runtime (Playbook discipline:
// every NGX call site must survive a runtime access violation).
__declspec(noinline) NVSDK_NGX_Result CallCreateDlssg(
    ID3D12GraphicsCommandList* cmdList,
    unsigned int creationNodeMask,
    unsigned int visibilityNodeMask,
    NVSDK_NGX_Handle** handle,
    NVSDK_NGX_Parameter* params,
    NVSDK_NGX_DLSSG_Create_Params* createParams,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NGX_D3D12_CREATE_DLSSG(cmdList, creationNodeMask,
            visibilityNodeMask, handle, params, createParams);
    }
    __except (LogDlssgException(GetExceptionInformation())) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallEvaluateDlssg(
    ID3D12GraphicsCommandList* cmdList,
    NVSDK_NGX_Handle* handle,
    NVSDK_NGX_Parameter* params,
    NVSDK_NGX_D3D12_DLSSG_Eval_Params* evalParams,
    NVSDK_NGX_DLSSG_Opt_Eval_Params* optEvalParams,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NGX_D3D12_EVALUATE_DLSSG(cmdList, handle, params, evalParams, optEvalParams);
    }
    __except (LogDlssgException(GetExceptionInformation())) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallReleaseDlssg(
    NVSDK_NGX_Handle* handle, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NVSDK_NGX_D3D12_ReleaseFeature(handle);
    }
    __except (LogDlssgException(GetExceptionInformation())) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

} // namespace

DlssFgBackend::~DlssFgBackend()
{
    release();
}

bool DlssFgBackend::queryCapability(NgxCoreHost& coreHost, Capability& caps, Status& status)
{
    (void)coreHost; // capability parameters come from the already-initialized core
    caps = Capability{};

    NVSDK_NGX_Parameter* capParams = nullptr;
    if(compatibility_&&!compatibility_->beginCapabilities()){status=Status::DeviceFailure;return false;}
    const NVSDK_NGX_Result capResult = NVSDK_NGX_D3D12_GetCapabilityParameters(&capParams);
    const bool restored=!compatibility_||compatibility_->endCapabilities();
    log::info("ngx", std::format("fg-backend: capability query result={} params={}",
        ngxResultString(static_cast<uint64_t>(capResult)), capParams != nullptr ? "non-null" : "null"));
    if (capParams == nullptr || capResult != NVSDK_NGX_Result_Success || !restored) {
        log::error("ngx",std::format("fg-backend capability failed result=0x{:X} nullParams={}",unsigned(capResult),capParams==nullptr));
        status = Status::DeviceFailure;
        if(capParams)NVSDK_NGX_D3D12_DestroyParameters(capParams);
        return false;
    }

    unsigned long long fgAvailableULL = 0;
    const NVSDK_NGX_Result gAvail = capParams->Get(NVSDK_NGX_Parameter_FrameGeneration_Available, &fgAvailableULL);
    int fgAvailableI = 0;
    const NVSDK_NGX_Result gAvailI = capParams->Get(NVSDK_NGX_Parameter_FrameGeneration_Available, &fgAvailableI);
    caps.availableGetResult = static_cast<uint64_t>(gAvail);
    caps.available = (gAvail == NVSDK_NGX_Result_Success && fgAvailableULL != 0) ||
                     (gAvailI == NVSDK_NGX_Result_Success && fgAvailableI != 0);

    unsigned long long initResultULL = 0;
    const NVSDK_NGX_Result gInit = capParams->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &initResultULL);
    caps.featureInitResult = (gInit == NVSDK_NGX_Result_Success) ? static_cast<int64_t>(initResultULL) : INT64_MIN;

    int needsDriver = 0;
    const NVSDK_NGX_Result gDriver = capParams->Get(NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver, &needsDriver);
    caps.needsUpdatedDriver = (gDriver == NVSDK_NGX_Result_Success) && (needsDriver != 0);

    int maj = 0, min = 0;
    const NVSDK_NGX_Result gMaj = capParams->Get(NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMajor, &maj);
    const NVSDK_NGX_Result gMin = capParams->Get(NVSDK_NGX_Parameter_FrameGeneration_MinDriverVersionMinor, &min);
    caps.minDriverVersionMajor = (gMaj == NVSDK_NGX_Result_Success) ? maj : -1;
    caps.minDriverVersionMinor = (gMin == NVSDK_NGX_Result_Success) ? min : -1;

    unsigned long long mfMax = 0;
    int mfMaxI = 0;
    const NVSDK_NGX_Result gMfU = capParams->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &mfMax);
    const NVSDK_NGX_Result gMfI = capParams->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax, &mfMaxI);
    caps.multiFrameCountMax = static_cast<uint32_t>(
        (gMfU == NVSDK_NGX_Result_Success) ? mfMax : ((gMfI == NVSDK_NGX_Result_Success) ? static_cast<unsigned>(mfMaxI) : 0));

    // Common identity parameters, best-effort for the evidence log.
    // (SDK 310.7 exposes no generic DriverVersion/RequiredVersion capability
    // parameter names; GPU/driver identity is logged by the caller via DXGI.)

    // HAGS evidence from the OS (registry HwSchMode: 2=on, 1=off, absent=default).
    HKEY key = nullptr;
    caps.hagsRegistryMode = -1;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD value = 0, size = sizeof(value), type = 0;
        if (RegQueryValueExW(key, L"HwSchMode", nullptr, &type,
                reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS && type == REG_DWORD) {
            caps.hagsRegistryMode = static_cast<int>(value);
        }
        RegCloseKey(key);
    }

    log::info("ngx", std::format("fg-backend: FG.Available Get(ull)=0x{:X} Get(i)=0x{:X} value={} | FeatureInitResult=0x{:X} value={} | NeedsUpdatedDriver=0x{:X} value={} | MinDriver {}.{} | MultiFrameCountMax={} (ull 0x{:X}, i 0x{:X}) | HwSchMode={}",
        static_cast<uint64_t>(gAvail), static_cast<uint64_t>(gAvailI), caps.available,
        static_cast<uint64_t>(gInit), static_cast<uint64_t>(caps.featureInitResult),
        static_cast<uint64_t>(gDriver), caps.needsUpdatedDriver,
        caps.minDriverVersionMajor, caps.minDriverVersionMinor,
        caps.multiFrameCountMax, static_cast<uint64_t>(gMfU), static_cast<uint64_t>(gMfI),
        caps.hagsRegistryMode));

    if(compatibility_){
        if(!compatibility_->publishStartup(capParams)){
            NVSDK_NGX_D3D12_DestroyParameters(capParams);status=Status::DeviceFailure;return false;
        }
        caps.available=true;caps.multiFrameCountMax=5;
    }
    NVSDK_NGX_D3D12_DestroyParameters(capParams);

    // Capability query itself succeeded (parameters were readable). Whether FG
    // is available is reported honestly; the caller decides fail-closed.
    status = Status::Ok;
    return caps.available;
}

bool DlssFgBackend::create(NgxCoreHost& coreHost,
                           ID3D12GraphicsCommandList* cmdList,
                           NVSDK_NGX_Parameter* params,
                           const CreateDesc& desc,
                           Status& status)
{
    (void)coreHost;
    if (handle_ != nullptr) {
        release();
    }
    fatal_ = false;
    width_ = desc.width;
    height_ = desc.height;

    // Create-time resource flags: backbuffer/mvecs/depth/output always
    // provided; HUD/UI/UIAlpha/BidirectionalDistortionField never provided
    // (Playbook 14.2 - declare, never pass dangling textures).
    const unsigned int alwaysFlags =
        NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer |
        NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
        NVSDK_NGX_DLSSG_ResourceFlags_Depth;
    const unsigned int neverFlags =
        NVSDK_NGX_DLSSG_ResourceFlags_HUDLess |
        NVSDK_NGX_DLSSG_ResourceFlags_UI |
        NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
        NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField;
    params->Set(NVSDK_NGX_DLSSG_Parameter_ResourceAlwaysProvided_Flags, alwaysFlags);
    params->Set(NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags, neverFlags);
    log::info("ngx", std::format("fg-backend: ResourceAlwaysProvided=0x{:X} ResourceNeverProvided=0x{:X}",
        alwaysFlags, neverFlags));

    NVSDK_NGX_DLSSG_Create_Params createParams{};
    createParams.Width = desc.width;
    createParams.Height = desc.height;
    createParams.NativeBackbufferFormat = desc.backbufferFormat;
    createParams.RenderWidth = desc.renderWidth;
    createParams.RenderHeight = desc.renderHeight;
    createParams.DynamicResolutionScaling = desc.dynamicResolution ? 1u : 0u;

    uint32_t sehCode = 0;
    if(compatibility_&&!compatibility_->beginCreate(cmdList)){status=Status::DeviceFailure;return false;}
    const NVSDK_NGX_Result result = CallCreateDlssg(cmdList, 1, 1, &handle_,
        params, &createParams, sehCode);
    const bool restored=!compatibility_||compatibility_->endCreate(result==NVSDK_NGX_Result_Success);
    createResult_ = static_cast<uint64_t>(result);

    log::info("ngx", std::format("fg-backend: Create DLSSG {}x{} (internal {}x{}) fmt={} result={} handle={} seh={}",
        desc.width, desc.height, desc.renderWidth, desc.renderHeight, desc.backbufferFormat,
        ngxResultString(createResult_), handle_ != nullptr ? "non-null" : "null", sehCode));

    if (result != NVSDK_NGX_Result_Success || !restored || handle_ == nullptr) {
        log::error("ngx", std::format("fg-backend failed result=0x{:X} seh=0x{:X}", static_cast<unsigned>(result), sehCode));
        // Query FeatureInitResult for diagnostic detail (user directive).
        unsigned long long initResult = 0;
        const NVSDK_NGX_Result gir = params->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &initResult);
        int initResultI = 0;
        const NVSDK_NGX_Result girI = params->Get(NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &initResultI);
        log::info("ngx", std::format("fg-backend: FeatureInitResult Get(ull)=0x{:X} value=0x{:X} | Get(i)=0x{:X} value=0x{:X}",
            static_cast<uint64_t>(gir), initResult,
            static_cast<uint64_t>(girI), static_cast<unsigned>(initResultI)));
        status = Status::DeviceFailure;
        fatal_ = true;
        release();
        return false;
    }
    return true;
}

bool DlssFgBackend::release()
{
    bool ok=true;
    if (handle_ != nullptr) {
        uint32_t sehCode = 0;
        const NVSDK_NGX_Result result = CallReleaseDlssg(handle_, sehCode);
        ok=result==NVSDK_NGX_Result_Success&&sehCode==0;
        (result == NVSDK_NGX_Result_Success ? log::info : log::error)("ngx", std::format("fg-backend: ReleaseFeature result={} evaluates={} resets={}",
            ngxResultString(static_cast<uint64_t>(result)), evaluateCount_, resetCount_));
        if (sehCode != 0) {
            log::error("ngx", std::format("fg-backend: ReleaseFeature SEH=0x{:X}", sehCode));
        }
        handle_ = nullptr;
    }
    return ok;
}

bool DlssFgBackend::evaluate(ID3D12GraphicsCommandList* cmdList,
                             NVSDK_NGX_Parameter* params,
                             const EvalDesc& desc,
                             Status& status)
{
    if (fatal_) {
        status = Status::DeviceFailure;
        return false;
    }
    // Up to five generated frames (6X). The graph validates the requested
    // multiplier against the runtime's MultiFrameCountMax before creating the
    // feature; this is only the absolute API-level guard.
    if (handle_ == nullptr || desc.multiFrameCount<1 || desc.multiFrameCount>5 || desc.multiFrameIndex<1 || desc.multiFrameIndex>desc.multiFrameCount) {
        status = Status::InvalidArgument;
        return false;
    }

    if(desc.hdr&&(!desc.backbuffer||!desc.outputInterpolated||
       desc.backbuffer->GetDesc().Format!=DXGI_FORMAT_R10G10B10A2_UNORM||
       desc.outputInterpolated->GetDesc().Format!=DXGI_FORMAT_R10G10B10A2_UNORM)){
        log::error("fg-backend","HDR Evaluate requires RGB10/PQ input and output textures");
        status=Status::InvalidArgument;return false;
    }
    NVSDK_NGX_D3D12_DLSSG_Eval_Params evalParams{};
    evalParams.pBackbuffer = desc.backbuffer;
    evalParams.pDepth = desc.depth;
    evalParams.pMVecs = desc.mvecs;
    evalParams.pOutputInterpFrame = desc.outputInterpolated;
    evalParams.pOutputDisableInterpolation = desc.outputDisableInterpolation;
    // Never-provided optional resources stay null (declared at create time).

    NVSDK_NGX_DLSSG_Opt_Eval_Params optParams{};
    // Count excludes the real frame; indices are one-based.
    optParams.multiFrameCount = desc.multiFrameCount;
    optParams.multiFrameIndex = desc.multiFrameIndex;
    // A stationary synthetic camera. All temporal motion is in the mvec
    // buffer, but projection/inverse and the camera basis must still agree.
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float v = (r == c) ? 1.0f : 0.0f;
            optParams.cameraViewToClip[r][c] = v;
            optParams.clipToCameraView[r][c] = v;
            optParams.clipToLensClip[r][c] = v;
            optParams.clipToPrevClip[r][c] = v;
            optParams.prevClipToClip[r][c] = v;
        }
    }
    optParams.jitterOffset[0] = 0.0f;
    optParams.jitterOffset[1] = 0.0f;
    optParams.mvecScale[0] = desc.mvecScaleX;
    optParams.mvecScale[1] = desc.mvecScaleY;
    optParams.cameraNear = 0.1f;
    optParams.cameraFar = 1000.0f;
    optParams.cameraFOV = 1.5707963f; // pi/2
    optParams.cameraAspectRatio = static_cast<float>(width_) / static_cast<float>(height_);
    optParams.cameraUp[1] = 1.0f;
    optParams.cameraRight[0] = 1.0f;
    optParams.cameraFwd[2] = 1.0f;
    const float projectionX = 1.0f / optParams.cameraAspectRatio; // tan(FOV / 2) = 1
    const float projectionZ = optParams.cameraFar / (optParams.cameraFar - optParams.cameraNear);
    const float projectionW = -optParams.cameraNear * projectionZ;
    optParams.cameraViewToClip[0][0] = projectionX;
    optParams.cameraViewToClip[2][2] = projectionZ;
    optParams.cameraViewToClip[2][3] = 1.0f;
    optParams.cameraViewToClip[3][2] = projectionW;
    optParams.cameraViewToClip[3][3] = 0.0f;
    optParams.clipToCameraView[0][0] = 1.0f / projectionX;
    optParams.clipToCameraView[2][2] = 0.0f;
    optParams.clipToCameraView[2][3] = 1.0f / projectionW;
    optParams.clipToCameraView[3][2] = 1.0f;
    optParams.clipToCameraView[3][3] = -projectionZ / projectionW;
    optParams.colorBuffersHDR = desc.hdr ? 1u : 0u; // HDR10 / RGB10 contract
    optParams.depthInverted = 0;
    optParams.cameraMotionIncluded = 1;
    optParams.reset = desc.reset ? 1u : 0u;
    optParams.automodeOverrideReset = 0;
    optParams.notRenderingGameFrames = 0;
    optParams.orthoProjection = 0;
    optParams.motionVectorsDilated = 0;
    // Zero is valid stationary motion, including either axis of a pan.
    optParams.motionVectorsInvalidValue = 3.402823466e38f;
    optParams.menuDetectionEnabled = 0;
    optParams.backbufferSubrectSize={width_,height_};
    optParams.outputInterpSubrectSize={width_,height_};
    optParams.mvecsSubrectSize={width_,height_};
    optParams.depthSubrectSize={width_,height_};

    if (desc.reset) {
        ++resetCount_;
    }

    // Monotonic real-frame id (uint64 via the C++ Set overload).
    params->Set(NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID, static_cast<unsigned long long>(desc.frameId));

    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallEvaluateDlssg(cmdList, handle_, params,
        &evalParams, &optParams, sehCode);
    if(result != NVSDK_NGX_Result_Success || log::verboseFrameLogs())
        log::info("ngx", std::format("fg-backend: Evaluate #{} frameId={} reset={} generatedCount={} subframe={} result={} seh={}",
            evaluateCount_ + 1, desc.frameId, desc.reset ? 1 : 0,desc.multiFrameCount,desc.multiFrameIndex,
            ngxResultString(static_cast<uint64_t>(result)), sehCode));

    if (result != NVSDK_NGX_Result_Success) {
        log::error("ngx", std::format("fg-backend failed result=0x{:X} seh=0x{:X}", static_cast<unsigned>(result), sehCode));
        fatal_ = true;
        // Keep resources alive until the graph drains already submitted GPU
        // work. Teardown releases the handle through the protected call.
        status = Status::DeviceFailure;
        return false;
    }
    ++evaluateCount_;
    return true;
}

} // namespace veyra::ngx
