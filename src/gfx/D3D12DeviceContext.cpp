#include "veyra/gfx/D3D12DeviceContext.h"

#include <windows.h>
#include <winevt.h>
#pragma comment(lib, "wevtapi.lib")

#include <format>
#include <string>
#include <vector>

#include "veyra/Log.h"
#include "veyra/NgxResult.h"
#include "veyra/gfx/CommandSlotRing.h"

namespace veyra::gfx {

namespace {

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string converted(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), converted.data(), length, nullptr, nullptr);
    return converted;
}

// Driver version from the kernel service key: first the friendly
// DisplayVersion string when present, otherwise the file version of the
// nvlddmkm.sys image the ImagePath value points at.
std::wstring queryNvidiaDriverVersion()
{
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm",
        0, KEY_QUERY_VALUE, &key);
    if (openResult != ERROR_SUCCESS) {
        return {};
    }

    wchar_t display[64]{};
    DWORD displaySize = sizeof(display);
    if (RegQueryValueExW(key, L"DisplayVersion", nullptr, nullptr, reinterpret_cast<LPBYTE>(display), &displaySize) == ERROR_SUCCESS &&
        displaySize >= sizeof(wchar_t) && display[0] != L'\0') {
        RegCloseKey(key);
        return std::wstring(display);
    }

    wchar_t imagePath[512]{};
    DWORD imagePathSize = sizeof(imagePath);
    if (RegQueryValueExW(key, L"ImagePath", nullptr, nullptr, reinterpret_cast<LPBYTE>(imagePath), &imagePathSize) != ERROR_SUCCESS ||
        imagePathSize < sizeof(wchar_t) || imagePath[0] == L'\0') {
        RegCloseKey(key);
        return {};
    }
    RegCloseKey(key);

    // ImagePath uses the kernel form "\SystemRoot\..."; ExpandEnvironmentStrings
    // only handles %%-references, so translate the prefix manually.
    std::wstring driverFile(imagePath);
    const wchar_t* systemRootPrefix = L"\\SystemRoot";
    const size_t prefixLength = 11; // wcslen(L"\\SystemRoot")
    if (_wcsnicmp(driverFile.c_str(), systemRootPrefix, prefixLength) == 0) {
        wchar_t windowsDir[MAX_PATH]{};
        const UINT windowsLength = GetWindowsDirectoryW(windowsDir, MAX_PATH);
        if (windowsLength == 0 || windowsLength >= MAX_PATH) {
            return {};
        }
        driverFile = std::wstring(windowsDir) + driverFile.substr(prefixLength);
    }

    DWORD handle = 0;
    const DWORD versionSize = GetFileVersionInfoSizeW(driverFile.c_str(), &handle);
    if (versionSize == 0) {
        return {};
    }
    std::vector<unsigned char> buffer(versionSize);
    if (!GetFileVersionInfoW(driverFile.c_str(), 0, versionSize, buffer.data())) {
        return {};
    }
    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT length = 0;
    if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &length) && length >= sizeof(VS_FIXEDFILEINFO)) {
        return std::format(L"{}.{}.{}.{}",
            HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS),
            HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
    }
    return {};
}

bool fillAdapterInfo(const DXGI_ADAPTER_DESC1& desc, AdapterInfo& info)
{
    info.description = desc.Description;
    info.vendorId = desc.VendorId;
    info.vendorIdHex = std::format("0x{:04X}", desc.VendorId);
    info.luid = (static_cast<uint64_t>(static_cast<uint32_t>(desc.AdapterLuid.HighPart)) << 32)
        | static_cast<uint32_t>(desc.AdapterLuid.LowPart);
    info.luidString = std::format("0x{:08X}:0x{:016X}",
        static_cast<uint32_t>(desc.AdapterLuid.HighPart),
        static_cast<uint64_t>(desc.AdapterLuid.LowPart));
    info.dedicatedVideoMemoryBytes = desc.DedicatedVideoMemory;
    info.isNvidia = desc.VendorId == 0x10DE;
    info.deviceId = desc.DeviceId;
    info.isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    info.driverVersion = info.isNvidia?queryNvidiaDriverVersion():std::wstring{};
    info.driverVersionSource = info.driverVersion.empty() ? L"unavailable" : L"registry";
    return !info.isSoftware;
}

} // namespace

D3D12DeviceContext::~D3D12DeviceContext()
{
    shutdown();
}

bool D3D12DeviceContext::initialize(const DeviceContextDesc& desc, Status& status)
{
    if (initialized_) {
        shutdown();
    }

    // Microsoft DRED settings apply only to subsequently created devices.
    // Enable before even the adapter's D3D12 capability probe below.
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
    const auto dredHr=D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
    if(SUCCEEDED(dredHr)) {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
    veyra::log::info("dred",std::format("configured before device creation hr=0x{:X} breadcrumbs={} pageFault={}",unsigned(dredHr),SUCCEEDED(dredHr),SUCCEEDED(dredHr)));
    // 1. Optional debug layer.
    if (desc.enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (SUCCEEDED(result)) {
            debug->EnableDebugLayer();
            debugLayerEnabled_ = true;
            veyra::log::info("gfx", "d3d12 debug layer enabled");
        }
        else {
            veyra::log::warn("gfx", std::format("d3d12 debug layer unavailable hr={}; continuing without it", veyra::hresultString(result)));
        }
    }

    // 2. DXGI factory.
    const UINT factoryFlags = debugLayerEnabled_ ? DXGI_CREATE_FACTORY_DEBUG : 0;
    HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("CreateDXGIFactory2 failed hr={}", veyra::hresultString(result)));
        return false;
    }

    // Keep every backend on one hardware adapter. Never silently use WARP.
    DXGI_ADAPTER_DESC1 chosenDesc{};
    bool chosen = false;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumResult = factory_->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (enumResult == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(enumResult)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("EnumAdapterByGpuPreference index={} failed hr={}", index, veyra::hresultString(enumResult)));
            return false;
        }

        DXGI_ADAPTER_DESC1 candidateDesc{};
        result = candidate->GetDesc1(&candidateDesc);
        if (FAILED(result)) {
            status = Status::DeviceFailure;
            veyra::log::error("gfx", std::format("adapter GetDesc1 index={} failed hr={}", index, veyra::hresultString(result)));
            return false;
        }

        const bool isSoftware = (candidateDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        veyra::log::info("gfx", std::format("adapter[{}] vendorId=0x{:04X} software={} dedicatedVideoMiB={} desc={}",
            index, candidateDesc.VendorId, isSoftware,
            candidateDesc.DedicatedVideoMemory / (1024 * 1024),
            narrow(candidateDesc.Description)));

        const auto candidateLuid=(uint64_t(uint32_t(candidateDesc.AdapterLuid.HighPart))<<32)|candidateDesc.AdapterLuid.LowPart;
        if ((!desc.requiredVendorId||candidateDesc.VendorId==desc.requiredVendorId) &&
            (!desc.requiredLuid||candidateLuid==desc.requiredLuid) && !isSoftware &&
            SUCCEEDED(D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_12_0,__uuidof(ID3D12Device),nullptr))) {
            adapter_ = candidate;
            chosenDesc = candidateDesc;
            chosen = true;
            break;
        }
    }

    if (!chosen) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", "no compatible requested D3D12 hardware adapter found");
        return false;
    }
    fillAdapterInfo(chosenDesc, adapterInfo_);

    // 4. Device at minimum feature level 12_0, reporting the highest level.
    result = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("D3D12CreateDevice failed hr={}", veyra::hresultString(result)));
        return false;
    }

    const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
    featureLevels.pFeatureLevelsRequested = requested;
    featureLevels.NumFeatureLevels = static_cast<UINT>(std::size(requested));
    const HRESULT flResult = device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
    if (SUCCEEDED(flResult)) {
        const uint32_t packed = static_cast<uint32_t>(featureLevels.MaxSupportedFeatureLevel);
        featureLevel_ = std::format("{}_{}", packed >> 12, (packed >> 8) & 0xF);
    }
    else {
        featureLevel_ = "12_0";
    }
    veyra::log::info("gfx", std::format("device created featureLevelMax={} checkHr={}", featureLevel_, veyra::hresultString(flResult)));

    // 5. Direct command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    result = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_));
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("CreateCommandQueue failed hr={}", veyra::hresultString(result)));
        return false;
    }
    queue_->SetName(L"Veyra direct queue");   // DRED names the queue a hang happened on

    // 6. Fence + event.
    result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(result)) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("CreateFence failed hr={}", veyra::hresultString(result)));
        return false;
    }
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr) {
        status = Status::DeviceFailure;
        veyra::log::error("gfx", std::format("CreateEventW failed lastError={}", GetLastError()));
        return false;
    }

    commandSlotCount_ = desc.commandSlotCount;
    initialized_ = true;
    veyra::log::info("gfx", std::format("device context initialized adapter={} vendor={} luid={} driver={} ({}) featureLevel={} slots={}",
        narrow(adapterInfo_.description), adapterInfo_.vendorIdHex, adapterInfo_.luidString,
        narrow(adapterInfo_.driverVersion.empty() ? std::wstring(L"<none>") : adapterInfo_.driverVersion),
        narrow(adapterInfo_.driverVersionSource),
        featureLevel_, commandSlotCount_));
    return true;
}

void D3D12DeviceContext::shutdown()
{
    if (!initialized_) {
        return;
    }
    // Slot work is drained by CommandSlotRing::shutdown(); the context only
    // owns the fence/event lifetime (ring is the sole signaler).
    if (fenceEvent_ != nullptr) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    queue_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
    initialized_ = false;
    veyra::log::info("gfx", "device context shutdown complete");
}

bool D3D12DeviceContext::waitForFenceValue(uint64_t value, uint32_t timeoutMs)
{
    if(!fence_)return false;
    const auto completed=fence_->GetCompletedValue();
    if(completed==UINT64_MAX){reportDeviceFailure("fence-sentinel",value);return false;}
    if (completed >= value) {
        return true;
    }
    const HRESULT result = fence_->SetEventOnCompletion(value, fenceEvent_);
    if (FAILED(result)) {
        veyra::log::error("gfx", std::format("SetEventOnCompletion failed hr={} value={}", veyra::hresultString(result), value));
        return false;
    }
    const auto wait=WaitForSingleObject(fenceEvent_, timeoutMs);
    if(wait!=WAIT_OBJECT_0){reportDeviceFailure("fence-wait",value);return false;}
    uint32_t reason=0;
    return fence_->GetCompletedValue()!=UINT64_MAX&&checkDeviceAlive(reason);
}

bool D3D12DeviceContext::checkDeviceAlive(uint32_t& removedReason) const
{
    const HRESULT reason = device_?device_->GetDeviceRemovedReason():E_POINTER;
    removedReason = static_cast<uint32_t>(reason);
    return SUCCEEDED(reason);
}
bool D3D12DeviceContext::reportDeviceFailure(std::string_view operation,uint64_t requestedFence) const {
    uint32_t reason=0;const bool alive=checkDeviceAlive(reason);
    const auto completed=fence_?fence_->GetCompletedValue():0;
    veyra::log::error("gpu-failure",std::format("operation={} alive={} removedReason=0x{:X} requestedFence={} completedFence={} sentinel={} adapter={} device=0x{:X} luid={} driver={}",operation,alive,reason,requestedFence,completed,completed==UINT64_MAX,narrow(adapterInfo_.description),adapterInfo_.deviceId,adapterInfo_.luidString,narrow(adapterInfo_.driverVersion)));
    // Read at most 16 recent display/driver events; no application logs,
    // registry changes or global diagnostic settings are touched.
    const auto events=EvtQuery(nullptr,L"System",L"*[System[(Provider[@Name='Display'] or Provider[@Name='nvlddmkm']) and (EventID=4101 or EventID=153 or EventID=13 or EventID=14) and TimeCreated[timediff(@SystemTime)<=60000]]]",EvtQueryChannelPath|EvtQueryReverseDirection);
    if(events) {
        EVT_HANDLE items[16]{};DWORD count=0;
        if(EvtNext(events,16,items,0,0,&count))for(DWORD i=0;i<count;++i) {
            DWORD bytes=0,properties=0;
            EvtRender(nullptr,items[i],EvtRenderEventXml,0,nullptr,&bytes,&properties);
            if(bytes>0&&bytes<=65536) {
                std::vector<wchar_t> xml(bytes/sizeof(wchar_t)+1);
                if(EvtRender(nullptr,items[i],EvtRenderEventXml,DWORD(xml.size()*sizeof(wchar_t)),xml.data(),&bytes,&properties))
                    veyra::log::error("gpu-system-event",std::format("recent60s={} xml={}",i,narrow(xml.data())));
            }
            EvtClose(items[i]);
        }
        else veyra::log::info("gpu-system-event",std::format("no readable recent display events win32={}",GetLastError()));
        EvtClose(events);
    } else veyra::log::warn("gpu-system-event",std::format("query unavailable win32={}",GetLastError()));
    if(alive||!device_)return alive;
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    const auto query=device_.As(&dred);
    if(FAILED(query)){veyra::log::error("dred",std::format("QueryInterface hr=0x{:X}",unsigned(query)));return false;}
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 crumbs{};
    const auto crumbsHr=dred->GetAutoBreadcrumbsOutput1(&crumbs);
    veyra::log::error("dred",std::format("breadcrumb query hr=0x{:X}",unsigned(crumbsHr)));
    if(SUCCEEDED(crumbsHr)) {
        uint32_t count=0;
        for(auto* n=crumbs.pHeadAutoBreadcrumbNode;n&&count<16;n=n->pNext,++count) {
            const auto last=n->pLastBreadcrumbValue?*n->pLastBreadcrumbValue:0;
            const auto nextOp=n->pCommandHistory&&last<std::min(n->BreadcrumbCount,65536u)?unsigned(n->pCommandHistory[last]):UINT_MAX;
            // The op types around the stop point (D3D12_AUTO_BREADCRUMB_OP: dispatches, copies,
            // barriers, resolves), so a hang inside NR's dispatches reads differently from one
            // in a copy or a resolve.
            std::string around;
            if(n->pCommandHistory)for(uint32_t k=last>6?last-6:0;k<std::min(n->BreadcrumbCount,last+6);++k)around+=std::format("{}{}",around.empty()?"":",",unsigned(n->pCommandHistory[k]));
            veyra::log::error("dred",std::format("breadcrumb={} completed={} total={} nextOp={} list={} queue={} ops[{}..]={}",count,last,n->BreadcrumbCount,nextOp,n->pCommandListDebugNameA?n->pCommandListDebugNameA:"unnamed",n->pCommandQueueDebugNameA?n->pCommandQueueDebugNameA:"unnamed",last>6?last-6:0,around));
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};const auto faultHr=dred->GetPageFaultAllocationOutput1(&fault);
    veyra::log::error("dred",std::format("pageFault query hr=0x{:X} address=0x{:X}",unsigned(faultHr),fault.PageFaultVA));
    if(SUCCEEDED(faultHr))for(const auto& group:{fault.pHeadExistingAllocationNode,fault.pHeadRecentFreedAllocationNode}) {
        uint32_t count=0;for(auto* n=group;n&&count<16;n=n->pNext,++count)
            veyra::log::error("dred",std::format("allocation={} type={} name={}",count,unsigned(n->AllocationType),n->ObjectNameA?n->ObjectNameA:"unnamed"));
    }
    return false;
}

bool D3D12DeviceContext::videoMemoryInfo(uint64_t& budget,uint64_t& usage) const {
    ComPtr<IDXGIAdapter3> adapter;DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if(!adapter_||FAILED(adapter_.As(&adapter))||FAILED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info)))return false;
    budget=info.Budget;usage=info.CurrentUsage;return true;
}

bool D3D12DeviceContext::exerciseSlotRing()
{
    CommandSlotRing ring;
    Status status = Status::Ok;
    if (!ring.initialize(device_.Get(), queue_.Get(), fence_.Get(), fenceEvent_, commandSlotCount_, status)) {
        veyra::log::error("gfx", std::format("exerciseSlotRing: ring init failed status={}", veyra::statusString(status)));
        return false;
    }
    if (!ring.exerciseAll()) {
        return false;
    }
    ring.shutdown();
    return true;
}

} // namespace veyra::gfx
