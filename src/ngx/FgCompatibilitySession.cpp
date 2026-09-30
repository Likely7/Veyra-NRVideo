// Startup capability and scoped driver gates adapted from
// dashdogy/RTX40MFG-Unlock ampere_backend.cpp (MIT), commit
// 33b41835dc39c5d8ab1ef93efb2449be31139c09. Veyra directly owns the
// D3D12 buffers and NGX calls; no wrapper hooks or Streamline are used.
#include "veyra/ngx/FgCompatibilitySession.h"
#include "veyra/ngx/AdaMfgUnlock.h"
#include "veyra/ngx/AmpereMfgUnlock.h"
#include "veyra/ngx/DlssgTransfusion.h"
#include "veyra/ngx/NvapiArchSpoof.h"
#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "compat/NgxDiscovery.h"
#include "compat/protected_pointer.h"
#include "compat/RestoreMemory.h"
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>
#include <filesystem>
#include <format>
#include <mutex>
#include <atomic>
#include <vector>

namespace veyra::ngx {
namespace {
std::mutex sessionMutex;
std::atomic<uint64_t> generation{0};
std::atomic<bool> poisoned{false};
bool env(const wchar_t* name) { return GetEnvironmentVariableW(name,nullptr,0)>0; }
bool sameLuid(LUID a,LUID b) { return a.LowPart==b.LowPart&&a.HighPart==b.HighPart; }
struct Patch {
    uintptr_t address=0;
    uint8_t original=0;
    bool active=false, privatePage=false, uncertain=false;
    bool set(bool enable) {
        if(!address)return false;
        if(active==enable)return *reinterpret_cast<const uint8_t*>(address)==(enable?0xeb:original);
        const uint8_t expected=enable?original:0xeb, replacement=enable?0xeb:original;
        PSAPI_WORKING_SET_EX_INFORMATION working{};
        working.VirtualAddress=reinterpret_cast<void*>(address);
        const bool privateNow=QueryWorkingSetEx(GetCurrentProcess(),&working,sizeof(working))&&working.VirtualAttributes.Valid&&!working.VirtualAttributes.Shared;
        const auto result=protected_pointer::ReplaceProtectedBytes(address,&expected,&replacement,1,PAGE_EXECUTE_READ,
            &VirtualProtect,&FlushInstructionCache,privatePage||privateNow?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_WRITECOPY);
        privatePage|=result.replacementWasPublished;
        if(result.disposition==protected_pointer::PublishDisposition::ePublishedRestored)active=enable;
        else if(result.disposition==protected_pointer::PublishDisposition::eIndeterminate)uncertain=true;
        if(result.disposition!=protected_pointer::PublishDisposition::ePublishedRestored){
            log::error("fg-compat","driver gate publication/restore failed");return false;
        }
        active=enable;return true;
    }
};
NVSDK_NGX_Result requirementsCall(IDXGIAdapter* adapter,const NVSDK_NGX_FeatureDiscoveryInfo* discovery,NVSDK_NGX_FeatureRequirement* result) {
    __try{return NVSDK_NGX_D3D12_GetFeatureRequirements(adapter,discovery,result);}
    __except(EXCEPTION_EXECUTE_HANDLER){return NVSDK_NGX_Result_FAIL_PlatformError;}
}
}
struct FgCompatibilitySession::Impl {
    std::unique_lock<std::mutex> lock{sessionMutex,std::defer_lock};
    HMODULE provider=nullptr, driver=nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources;
    uint64_t serial=0;
    // transfusion: the 310.9.1 provider, patched by DlssgTransfusion instead of
    // the audited-310.7 Ada/Ampere unlocks. driverGates: scoped NGX core gates
    // located (required on 310.7, optional on 310.9.1).
    bool transfusion=false, driverGates=false;
    bool ampere=false, ready=false, allocated=false, initializing=false, initialized=false, startup=false, createAttempted=false, fatal=false, uncertainGate=false;
    Patch metadata,validation;
    uintptr_t deviceGate=0;
    bool programs() const {
        if(transfusion){const auto s=DlssgTransfusion::snapshot();return s.applied&&s.identityVerified;}
        if(ampere){const auto s=AmpereMfgUnlock::snapshot();return s.applied&&s.identityVerified&&s.fatbinsRedirected;}
        const auto s=AdaMfgUnlock::snapshot();return s.applied&&s.identityVerified&&s.kernelPatched&&s.mfgGatePatched;
    }
};
FgCompatibilitySession::FgCompatibilitySession():impl_(std::make_unique<Impl>()){}
FgCompatibilitySession::~FgCompatibilitySession(){
    auto& s=*impl_;
    if(!s.lock.owns_lock())return;
    bool restored=!s.validation.uncertain&&!s.metadata.uncertain&&!s.uncertainGate&&!compat::memoryProtectionUncertain;
    if(s.validation.active)restored=s.validation.set(false)&&restored;
    if(s.metadata.active)restored=s.metadata.set(false)&&restored;
    if(s.deviceGate){
        const uint8_t before[]={0xeb,4},after[]={0x0f,0x84};
        const auto result=protected_pointer::ReplaceProtectedBytes(s.deviceGate,before,after,2,PAGE_EXECUTE_READ,&VirtualProtect,&FlushInstructionCache,PAGE_EXECUTE_READWRITE);
        restored=(result.disposition==protected_pointer::PublishDisposition::ePublishedRestored)&&restored;
    }
    restored=DlssgTransfusion::release()&&restored;
    restored=AdaMfgUnlock::release()&&restored;
    restored=AmpereMfgUnlock::release()&&restored;
    restored=NvapiArchSpoof::release()&&restored;
    if(!restored){
        poisoned=true;
        log::error("fg-compat","rollback unproven; retaining modules/resources until process exit; restart required before further NGX use");
        s.lock.unlock();
        (void)impl_.release();
        return;
    }
    s.resources.clear();
    if(s.driver)FreeLibrary(s.driver);
    if(s.provider)FreeLibrary(s.provider);
    log::info("fg-compat",std::format("session released generation={}",s.serial));
}
bool FgCompatibilitySession::processHealthy(){return !poisoned.load()&&!compat::memoryProtectionUncertain.load();}
bool FgCompatibilitySession::requested(uint32_t vendor,uint32_t device) {
    return (!env(L"VEYRA_DISABLE_AMPERE_MFG_UNLOCK")&&(AmpereMfgUnlock::adapterIsAmpere(vendor,device)||env(L"VEYRA_TEST_FORCE_AMPERE_UNLOCK")))||
        (!env(L"VEYRA_DISABLE_ADA_MFG_UNLOCK")&&(AdaMfgUnlock::adapterIsAda(vendor,device)||env(L"VEYRA_TEST_FORCE_ADA_UNLOCK")));
}
bool FgCompatibilitySession::open(ID3D12Device* device,uint32_t vendor,uint32_t deviceId,const std::wstring& directory) {
    auto& s=*impl_;
    if(!device||!requested(vendor,deviceId)||!s.lock.try_lock())return false;
    if(!processHealthy()){s.lock.unlock();return false;}
    s.ampere=AmpereMfgUnlock::adapterIsAmpere(vendor,deviceId)||env(L"VEYRA_TEST_FORCE_AMPERE_UNLOCK");
    s.device=device;s.serial=++generation;
    const auto path=std::filesystem::absolute(std::filesystem::path(directory)/L"nvngx_dlssg.dll");
    FileIdentity identity;IdentityError error;
    computeFileIdentity(path.wstring(),identity,error);
    log::info("fg-compat",std::format("provider path={} sha256={} bytes={} generation={} LUID={:08X}:{:08X} adapter=0x{:04X}",
        path.string(),identity.sha256Upper,identity.sizeBytes,s.serial,uint32_t(device->GetAdapterLuid().HighPart),device->GetAdapterLuid().LowPart,deviceId));
    s.transfusion=DlssgTransfusion::moduleIsKnown(identity.sizeBytes,identity.sha256Upper);
    if(identity.sha256Upper!=AdaMfgUnlock::kKnownModuleSha256&&!s.transfusion)return false;
    s.provider=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!s.provider)return false;
    wchar_t actual[32768]{};
    if(!GetModuleFileNameW(s.provider,actual,32768)||!std::filesystem::equivalent(path,actual))return false;
    const auto entry=GetProcAddress(s.provider,"NVSDK_NGX_D3D12_CreateFeature");
    MEMORY_BASIC_INFORMATION memory{};
    if(!entry||!VirtualQuery(reinterpret_cast<const void*>(entry),&memory,sizeof(memory))||memory.AllocationBase!=s.provider)return false;
    log::info("fg-compat",std::format("provider module=0x{:X} ownedCreate=0x{:X} profile={}",uintptr_t(s.provider),uintptr_t(entry),s.transfusion?"310.9.1-transfusion":"310.7-audited"));
    if(s.transfusion){
        // Patched before NGX initializes the provider: its NvAPI entry point
        // and CUDA programs are resolved during Init.
        const auto options=DlssgTransfusion::defaultOptions((std::filesystem::path(directory)/L"dlssg-kernels").wstring());
        const auto state=DlssgTransfusion::apply(s.provider,s.ampere?DlssgTransfusion::Target::Ampere:DlssgTransfusion::Target::Ada,options);
        log::info("fg-compat",std::format("310.9.1 provider patches applied={} target={}",state.applied,s.ampere?"ampere":"ada"));
        if(!state.applied)return false;
    }
    return true;
}
bool FgCompatibilitySession::transfusionProfile() const{return impl_->transfusion;}
HMODULE FgCompatibilitySession::provider() const{return impl_->provider;}
bool FgCompatibilitySession::prepareDriver(const std::wstring& directory,const char* project,const char* engine) {
    auto& s=*impl_;if(!s.provider||!s.programs()){log::error("fg-compat","prepareDriver: provider/programs missing");return false;}
    if(!s.ampere){s.ready=true;return true;}
    uintptr_t gate=0;
    compat::Image providerImage;if(!providerImage.Open(s.provider)){log::error("fg-compat","prepareDriver: invalid image");return false;}
    // On 310.9.1 DlssgTransfusion already removed the same count-policy branch.
    if(!s.transfusion){
    // The audited legacy provider has one preset. Preserve all count/index
    // checks, changing only its Blackwell-only choice of maximum=5.
    constexpr uint8_t pattern[]={0x84,0xd2,0x0f,0x84,0x03,0x01,0,0,0xbe,5,0,0,0};
    const auto* sections=IMAGE_FIRST_SECTION(providerImage.nt);
    for(unsigned i=0;i<providerImage.nt->FileHeader.NumberOfSections;++i){
      const auto& section=sections[i];
      if(!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
      const auto begin=providerImage.base+section.VirtualAddress;
      const auto length=std::max(section.Misc.VirtualSize,section.SizeOfRawData);
      if(!providerImage.Readable(begin,length)){log::error("fg-compat",std::format("prepareDriver: unreadable section rva=0x{:X} length={}",section.VirtualAddress,length));return false;}
      for(uint32_t n=0;uint64_t(n)+sizeof(pattern)<=length;++n){
        const uintptr_t p=begin+n;
        if(std::memcmp(reinterpret_cast<void*>(p),pattern,sizeof(pattern)))continue;
        if(!providerImage.Executable(p)||!providerImage.Executable(p+0x10b)||!providerImage.Readable(p+0x10b,4)||gate){log::error("fg-compat","prepareDriver: count policy memory/uniqueness mismatch");return false;}
        constexpr uint8_t target[]={0x41,0x83,0xf8,1};
        if(std::memcmp(reinterpret_cast<void*>(p+0x10b),target,sizeof(target))){log::error("fg-compat","prepareDriver: count policy target mismatch");return false;}
        gate=p;
      }
    }
    if(!gate){log::error("fg-compat","prepareDriver: count policy not found");return false;}
    // Replace 0F 84 with EB 04, retaining the displacement bytes as skipped data.
    const uint8_t before[]={0x0f,0x84},after[]={0xeb,4};
    PSAPI_WORKING_SET_EX_INFORMATION working{};
    working.VirtualAddress=reinterpret_cast<void*>(gate+2);
    const bool privatePage=QueryWorkingSetEx(GetCurrentProcess(),&working,sizeof(working))&&working.VirtualAttributes.Valid&&!working.VirtualAttributes.Shared;
    auto patched=protected_pointer::ReplaceProtectedBytes(gate+2,before,after,2,PAGE_EXECUTE_READ,&VirtualProtect,&FlushInstructionCache,
        privatePage?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_WRITECOPY);
    if(patched.disposition==protected_pointer::PublishDisposition::ePublishedRestored)s.deviceGate=gate+2;
    if(patched.disposition==protected_pointer::PublishDisposition::eIndeterminate)s.uncertainGate=true;
    if(patched.disposition!=protected_pointer::PublishDisposition::ePublishedRestored){log::error("fg-compat",std::format("prepareDriver: count policy publication disposition={}",uint32_t(patched.disposition)));return false;}
    }
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(s.device->GetAdapterLuid(),IID_PPV_ARGS(&adapter))))return false;
    const wchar_t* paths[]={directory.c_str()};
    NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
    NVSDK_NGX_FeatureDiscoveryInfo discovery{};
    discovery.SDKVersion=NVSDK_NGX_Version_API;discovery.FeatureID=NVSDK_NGX_Feature_FrameGeneration;
    discovery.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
    discovery.Identifier.v.ProjectDesc={project,NVSDK_NGX_ENGINE_TYPE_CUSTOM,engine};
    discovery.ApplicationDataPath=directory.c_str();discovery.FeatureInfo=&common;
    NVSDK_NGX_FeatureRequirement requirements{};
    // This public call asks the static NGX loader to resolve the installed
    // driver. Its native result is evidence, not compatibility admission.
    auto result=requirementsCall(adapter.Get(),&discovery,&requirements);
    log::info("fg-compat",std::format("native requirements result=0x{:X} support={} minArch=0x{:X}",uint32_t(result),uint32_t(requirements.FeatureSupported),requirements.MinHWArchitecture));
    std::array<HMODULE,2048> modules{};DWORD bytes=0;
    if(!EnumProcessModules(GetCurrentProcess(),modules.data(),sizeof(modules),&bytes)||bytes>sizeof(modules))return false;
    uintptr_t metadata=0,validation=0;
    for(size_t i=0;i<bytes/sizeof(HMODULE);++i){
        const auto module=modules[i];
        const auto create=GetProcAddress(module,"NVSDK_NGX_D3D12_CreateFeature");
        const auto caps=GetProcAddress(module,"NVSDK_NGX_D3D12_GetCapabilityParameters");
        if(!create||!caps)continue;
        compat::Image image;if(!image.Open(module)||!image.Executable(uintptr_t(create))||!image.Executable(uintptr_t(caps)))continue;
        const auto m=compat::FindPattern(image,ampere_patterns::kAmpereNgxMetadataPattern.data(),ampere_patterns::kAmpereNgxMetadataPatternMask.data(),ampere_patterns::kAmpereNgxMetadataPattern.size(),ampere_patterns::kAmpereNgxMetadataBranchOffset);
        const auto v=compat::FindCreateValidation(image,uintptr_t(create));
        wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);
        log::info("fg-compat",std::format("driver discovery path={} base=0x{:X} metadataRva=0x{:X} validationRva=0x{:X}",std::filesystem::path(path).string(),image.base,m?m-image.base:0,v?v-image.base:0));
        if(!m||!v)continue;
        if(s.driver)return false;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(module),&s.driver)||s.driver!=module)return false;
        metadata=m;validation=v;
    }
    if(!s.driver){
        if(!s.transfusion)return false;
        // 310.9.1: the provider's own minimum architecture was lowered, which
        // admits Ampere through the unmodified NGX core (upstream-validated).
        log::info("fg-compat","NGX core gates not located; 310.9.1 relies on the provider minimum-architecture patch");
        s.ready=s.programs();
        return s.ready;
    }
    s.driverGates=true;
    s.metadata={metadata+ampere_patterns::kAmpereNgxMetadataBranchOffset,ampere_patterns::kAmpereNgxMetadataOriginal};
    s.validation={validation+ampere_patterns::kAmpereNgxCreateValidationBranchOffset,ampere_patterns::kAmpereNgxCreateValidationOriginal};
    if(!s.metadata.set(true))return false;
    result=requirementsCall(adapter.Get(),&discovery,&requirements);
    const bool restored=s.metadata.set(false);
    log::info("fg-compat",std::format("scoped FG requirements result=0x{:X} support={} restored={}",uint32_t(result),uint32_t(requirements.FeatureSupported),restored));
    const bool supported=result==NVSDK_NGX_Result_Success&&(requirements.FeatureSupported==NVSDK_NGX_FeatureSupportResult_Supported||requirements.FeatureSupported==NVSDK_NGX_FeatureSupportResult_AdapterUnsupported);
    // The static loader can return NotImplemented for discovery after Shutdown1.
    // Discovery is optional: the scoped capability query and real Create/Evaluate
    // still decide whether this freshly allocated session is usable.
    const bool bootstrap=result==NVSDK_NGX_Result_FAIL_FeatureNotSupported||result==NVSDK_NGX_Result_FAIL_FeatureNotFound||
        result==NVSDK_NGX_Result_FAIL_NotImplemented;
    s.ready=restored&&(supported||bootstrap)&&s.programs();
    return s.ready;
}
bool FgCompatibilitySession::bindResources(std::span<ID3D12Resource* const> real,std::span<ID3D12Resource* const> generated) {
    auto& s=*impl_;
    if(!s.ready||s.createAttempted||real.size()!=2||generated.size()!=10)return false;
    D3D12_RESOURCE_DESC first{};
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> allocated;
    for(const auto group:{real,generated})for(auto* resource:group){
        if(!resource)return false;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if(FAILED(resource->GetDevice(IID_PPV_ARGS(&device)))||device.Get()!=s.device.Get())return false;
        for(const auto& prior:allocated)if(prior.Get()==resource)return false;
        const auto d=resource->GetDesc();
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.SampleDesc.Count!=1||d.DepthOrArraySize!=1||d.MipLevels!=1||!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return false;
        if(allocated.empty())first=d;
        if(d.Width!=first.Width||d.Height!=first.Height||d.Format!=first.Format)return false;
        allocated.emplace_back(resource);
    }
    s.resources=std::move(allocated);s.allocated=true;
    log::info("fg-compat",std::format("resource proof generation={} real=2 generated=10 extent={}x{} format={}",s.serial,first.Width,first.Height,uint32_t(first.Format)));
    return true;
}
bool FgCompatibilitySession::beginInitialization(){
    auto& s=*impl_;
    if(!s.ready||!s.allocated||s.initializing||s.initialized||s.createAttempted||s.fatal)return false;
    // NGX caches provider initialization failures before the capability query.
    // Upstream BeginStartup covers this call as well as GetCapabilityParameters.
    if(s.driverGates&&!s.metadata.set(true)){s.fatal=true;return false;}
    s.initializing=true;
    log::info("fg-compat",std::format("Init scope generation={} ampereMetadata={}",s.serial,s.driverGates));
    return true;
}
bool FgCompatibilitySession::endInitialization(bool success){
    auto& s=*impl_;
    if(!s.initializing)return false;
    const bool restored=!s.driverGates||s.metadata.set(false);
    s.initializing=false;s.initialized=success&&restored;
    s.fatal|=!restored||!success;
    log::info("fg-compat",std::format("Init completed generation={} success={} metadataRestored={}",s.serial,success,restored));
    return !s.fatal;
}
bool FgCompatibilitySession::beginCapabilities(){
    auto& s=*impl_;if(!s.ready||!s.allocated||!s.initialized||s.initializing||s.createAttempted||s.fatal)return false;
    if(s.driverGates&&!s.metadata.set(true)){s.fatal=true;return false;}return true;
}
bool FgCompatibilitySession::endCapabilities(){
    auto& s=*impl_;if(s.driverGates&&!s.metadata.set(false)){s.fatal=true;return false;}return !s.fatal;
}
bool FgCompatibilitySession::publishStartup(NVSDK_NGX_Parameter* parameters){
    auto& s=*impl_;
    if(!parameters||!s.ready||!s.allocated||!s.programs()||s.createAttempted||s.fatal)return false;
    unsigned maximum=0,available=0;
    const auto native=parameters->Get("DLSSG.MultiFrameCountMax",&maximum);
    const auto availability=parameters->Get("FrameGeneration.Available",&available);
    if(native==NVSDK_NGX_Result_Success&&maximum>5)return false;
    // Ada still requires native FG availability. Ampere crosses only the
    // separately verified metadata/Create gates and full SM86 program route.
    if(!s.ampere&&(availability!=NVSDK_NGX_Result_Success||!available))return false;
    parameters->Set("FrameGeneration.Available",1u);
    parameters->Set("DLSSG.MultiFrameCountMax",5u);
    if(parameters->Get("FrameGeneration.Available",&available)!=NVSDK_NGX_Result_Success||available!=1||
       parameters->Get("DLSSG.MultiFrameCountMax",&maximum)!=NVSDK_NGX_Result_Success||maximum!=5)return false;
    s.startup=true;
    log::info("fg-compat",std::format("scoped startup generation={} maxGenerated=5; actual Create/Evaluate still required",s.serial));
    return true;
}
bool FgCompatibilitySession::beginCreate(ID3D12GraphicsCommandList* list){
    auto& s=*impl_;Microsoft::WRL::ComPtr<ID3D12Device> device;
    if(!list||!s.startup||s.createAttempted||s.fatal||!s.programs()||!s.allocated||
       FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||device.Get()!=s.device.Get()||!sameLuid(device->GetAdapterLuid(),s.device->GetAdapterLuid()))return false;
    s.createAttempted=true;
    if(s.driverGates&&!s.validation.set(true)){s.fatal=true;return false;}
    return true;
}
bool FgCompatibilitySession::endCreate(bool success){
    auto& s=*impl_;
    if(s.driverGates&&!s.validation.set(false))s.fatal=true;
    log::info("fg-compat",std::format("Create completed generation={} success={} driverGateRestored={}",s.serial,success,!s.fatal));
    return !s.fatal;
}
}
