#include "veyra/ngx/NgxCoreHost.h"

#include <windows.h>

#include <format>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"

namespace veyra::ngx {

namespace {

void NVSDK_CONV runtimeLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
{
    if (message) log::info("ngx-runtime", message);
}

// SEH boundary: no C++ objects with destructors may live in a __try scope,
// so the external call sits in its own frame (Playbook section 7.2).
__declspec(noinline) NVSDK_NGX_Result CallInitWithProjectId(
    const char* projectId,
    NVSDK_NGX_EngineType engineType,
    const char* engineVersion,
    const wchar_t* applicationDataPath,
    ID3D12Device* device,
    const NVSDK_NGX_FeatureCommonInfo* featureInfo,
    NVSDK_NGX_Version sdkVersion,
    uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    if(GetEnvironmentVariableW(L"VEYRA_TEST_NGX_INIT_FAILURE",nullptr,0)>0)
        return NVSDK_NGX_Result_FAIL_UnableToInitializeFeature;
    __try {
        result = NVSDK_NGX_D3D12_Init_with_ProjectID(projectId, engineType, engineVersion,
            applicationDataPath, device, featureInfo, sdkVersion);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallShutdown1(ID3D12Device* device, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NVSDK_NGX_D3D12_Shutdown1(device);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

// Every NGX external entry point sits behind SEH (Playbook 7.2), including
// the parameter-block allocation calls.
__declspec(noinline) NVSDK_NGX_Result CallAllocateParameters(NVSDK_NGX_Parameter** outParameters, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NVSDK_NGX_D3D12_AllocateParameters(outParameters);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

__declspec(noinline) NVSDK_NGX_Result CallDestroyParameters(NVSDK_NGX_Parameter* parameters, uint32_t& sehCode)
{
    sehCode = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    __try {
        result = NVSDK_NGX_D3D12_DestroyParameters(parameters);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        sehCode = static_cast<uint32_t>(GetExceptionCode());
        result = NVSDK_NGX_Result_FAIL_PlatformError;
    }
    return result;
}

} // namespace

NgxCoreHost::~NgxCoreHost()
{
    shutdown();
}

bool NgxCoreHost::initialize(ID3D12Device* device,
                             const std::wstring& runtimeDir,
                             const char* projectId,
                             const char* engineVersion,
                             Status& status)
{
    if (initialized_) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "core-host: initialize called twice on one instance");
        return false;
    }
    if (device == nullptr || runtimeDir.empty() || projectId == nullptr || engineVersion == nullptr) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "core-host: invalid initialize arguments");
        return false;
    }

    const wchar_t* featurePaths[] = { runtimeDir.c_str() };
    NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo.Path = const_cast<const wchar_t**>(featurePaths);
    common.PathListInfo.Length = 1;
    if (GetEnvironmentVariableW(L"VEYRA_TEST_NGX_VERBOSE", nullptr, 0)) {
        common.LoggingInfo.LoggingCallback = runtimeLog;
        common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
        common.LoggingInfo.DisableOtherLoggingSinks = true;
    }

    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallInitWithProjectId(projectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        engineVersion, runtimeDir.c_str(), device, &common, NVSDK_NGX_Version_API, sehCode);
    initResult_ = static_cast<uint64_t>(result);

    veyra::log::info("ngx", std::format("core-host: Init_with_ProjectID result={} seh={} runtimeDirLen={} projectId={}",
        veyra::ngxResultString(initResult_), sehCode, runtimeDir.size(), projectId));

    if (result != NVSDK_NGX_Result_Success) {
        status = Status::DeviceFailure;
        return false;
    }
    device_ = device;
    initialized_ = true;
    healthy_ = true;
    veyra::log::info("ngx", "core-host: initialized (single core session for this device)");
    return true;
}

void NgxCoreHost::shutdown()
{
    if (!initialized_) {
        return;
    }

    // Reverse order: live parameter blocks first, then core shutdown.
    while (liveParameterBlockCount_ > 0) {
        NVSDK_NGX_Parameter* block = liveParameterBlocks_[--liveParameterBlockCount_];
        veyra::log::warn("ngx", "core-host: destroying leaked parameter block at shutdown");
        { uint32_t seh = 0; (void)CallDestroyParameters(block, seh); }
    }

    uint32_t sehCode = 0;
    const NVSDK_NGX_Result result = CallShutdown1(device_, sehCode);
    veyra::log::info("ngx", std::format("core-host: Shutdown1 result={} seh={}",
        veyra::ngxResultString(static_cast<uint64_t>(result)), sehCode));

    device_ = nullptr;
    initialized_ = false;
    healthy_ = false;
}

NVSDK_NGX_Parameter* NgxCoreHost::allocateParameters(Status& status)
{
    if (!initialized_) {
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "core-host: allocateParameters before initialize");
        return nullptr;
    }
    if (liveParameterBlockCount_ >= 64) {
        healthy_ = false;
        status = Status::InvalidArgument;
        veyra::log::error("ngx", "core-host: parameter block tracking table full");
        return nullptr;
    }
    NVSDK_NGX_Parameter* parameters = nullptr;
    uint32_t allocSeh = 0;
    const NVSDK_NGX_Result result = CallAllocateParameters(&parameters, allocSeh);
    if (result != NVSDK_NGX_Result_Success || parameters == nullptr || allocSeh != 0) {
        healthy_ = false;
        status = Status::DeviceFailure;
        veyra::log::error("ngx", std::format("core-host: AllocateParameters result={} seh={}",
            veyra::ngxResultString(static_cast<uint64_t>(result)),allocSeh));
        return nullptr;
    }
    liveParameterBlocks_[liveParameterBlockCount_++] = parameters;
    return parameters;
}

void NgxCoreHost::destroyParameters(NVSDK_NGX_Parameter* parameters)
{
    if (parameters == nullptr) {
        return;
    }
    bool tracked = false;
    for (uint32_t i = 0; i < liveParameterBlockCount_; ++i) {
        if (liveParameterBlocks_[i] == parameters) {
            liveParameterBlocks_[i] = liveParameterBlocks_[--liveParameterBlockCount_];
            tracked = true;
            break;
        }
    }
    if (!tracked) {
        healthy_ = false;
        veyra::log::warn("ngx", "core-host: destroyParameters for an untracked block");
    }
    uint32_t seh = 0;
    const auto result=CallDestroyParameters(parameters,seh);
    if(result!=NVSDK_NGX_Result_Success||seh!=0){
        healthy_=false;
        log::error("ngx",std::format("core-host: DestroyParameters result={} seh={}",ngxResultString(uint64_t(result)),seh));
    }
}

} // namespace veyra::ngx
