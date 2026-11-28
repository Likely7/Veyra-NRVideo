// SPDX-License-Identifier: GPL-3.0-only
// 性能诊断: environment snapshot and the once-per-second system sampler.
// Read-only by design; see PerfDiagnosticsSystem.h.
#include "PerfDiagnosticsSystem.h"
#include "veyra/diagnostics/DiagnosticTrace.h"
#include <windows.h>
#include <winternl.h>   // NTSTATUS for d3dkmthk.h
#include <d3dkmthk.h>
#include <dxgi1_6.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <powrprof.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace veyra::ui::perfdiag {
namespace {
using Microsoft::WRL::ComPtr;

QString wide(const wchar_t* text) { return text ? QString::fromWCharArray(text) : QString(); }

std::optional<DWORD> regDword(HKEY root, const wchar_t* key, const wchar_t* value) {
    DWORD data = 0, size = sizeof(data);
    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS) return std::nullopt;
    return data;
}
QString regString(HKEY root, const wchar_t* key, const wchar_t* value) {
    DWORD size = 0;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < 2) return {};
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, text.data(), &size) != ERROR_SUCCESS) return {};
    text.resize(wcsnlen(text.c_str(), text.size()));
    return QString::fromStdWString(text);
}
QString luidText(LUID luid) {
    return QStringLiteral("%1:%2").arg(quint32(luid.HighPart), 8, 16, QLatin1Char('0')).arg(quint32(luid.LowPart), 8, 16, QLatin1Char('0'));
}
QString guidText(const GUID& g) {
    wchar_t text[64]{};
    StringFromGUID2(g, text, 64);
    return wide(text);
}

QJsonObject osInfo() {
    const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    QJsonObject os;
    os["product"] = regString(HKEY_LOCAL_MACHINE, key, L"ProductName");
    os["edition"] = regString(HKEY_LOCAL_MACHINE, key, L"EditionID");
    os["displayVersion"] = regString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion");
    const QString build = regString(HKEY_LOCAL_MACHINE, key, L"CurrentBuild");
    const auto ubr = regDword(HKEY_LOCAL_MACHINE, key, L"UBR");
    os["build"] = ubr ? QStringLiteral("%1.%2").arg(build).arg(*ubr) : build;
    // ProductName still says "Windows 10" on Windows 11; the build number decides.
    os["windows11"] = build.toInt() >= 22000;
    return os;
}

QJsonObject cpuInfo() {
    QJsonObject cpu;
    cpu["name"] = regString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString").trimmed();
    cpu["logicalProcessors"] = int(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) {
        cpu["memoryTotalMiB"] = double(memory.ullTotalPhys >> 20);
        cpu["memoryAvailableMiB"] = double(memory.ullAvailPhys >> 20);
        cpu["memoryLoadPercent"] = int(memory.dwMemoryLoad);
    }
    return cpu;
}

QJsonArray adapters() {
    QJsonArray out;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return out;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        QJsonObject gpu;
        gpu["index"] = int(i);
        gpu["name"] = wide(desc.Description);
        gpu["vendorId"] = QStringLiteral("%1").arg(desc.VendorId, 4, 16, QLatin1Char('0'));
        gpu["deviceId"] = QStringLiteral("%1").arg(desc.DeviceId, 4, 16, QLatin1Char('0'));
        gpu["subSysId"] = QStringLiteral("%1").arg(desc.SubSysId, 8, 16, QLatin1Char('0'));
        gpu["revision"] = int(desc.Revision);
        gpu["dedicatedMiB"] = double(desc.DedicatedVideoMemory >> 20);
        gpu["sharedMiB"] = double(desc.SharedSystemMemory >> 20);
        gpu["luid"] = luidText(desc.AdapterLuid);
        gpu["software"] = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        LARGE_INTEGER umd{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
            const unsigned a = HIWORD(umd.HighPart), b = LOWORD(umd.HighPart), c = HIWORD(umd.LowPart), d = LOWORD(umd.LowPart);
            gpu["driver"] = QStringLiteral("%1.%2.%3.%4").arg(a).arg(b).arg(c).arg(d);
            if (desc.VendorId == 0x10DE) {
                const unsigned nv = (c % 10) * 10000 + d;   // 32.0.15.6094 -> 560.94
                gpu["nvidiaDriver"] = QStringLiteral("%1.%2").arg(nv / 100).arg(nv % 100, 2, 10, QLatin1Char('0'));
            }
        }
        D3DKMT_OPENADAPTERFROMLUID open{};
        open.AdapterLuid = desc.AdapterLuid;
        if (D3DKMTOpenAdapterFromLuid(&open) == 0) {
            D3DKMT_WDDM_2_7_CAPS caps{};
            D3DKMT_QUERYADAPTERINFO query{};
            query.hAdapter = open.hAdapter;
            query.Type = KMTQAITYPE_WDDM_2_7_CAPS;
            query.pPrivateDriverData = &caps;
            query.PrivateDriverDataSize = sizeof(caps);
            if (D3DKMTQueryAdapterInfo(&query) == 0) {
                gpu["hagsSupported"] = bool(caps.HwSchSupported);
                gpu["hagsEnabled"] = bool(caps.HwSchEnabled);
            }
            D3DKMT_CLOSEADAPTER close{open.hAdapter};
            D3DKMTCloseAdapter(&close);
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO local{};
        ComPtr<IDXGIAdapter3> adapter3;
        if (SUCCEEDED(adapter.As(&adapter3)) && SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) {
            gpu["budgetMiB"] = double(local.Budget >> 20);
            gpu["processUsageMiB"] = double(local.CurrentUsage >> 20);
        }
        out.append(gpu);
    }
    return out;
}

QString outputTechnology(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY t) {
    switch (t) {
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return QStringLiteral("HDMI");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return QStringLiteral("DisplayPort");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: return QStringLiteral("eDP");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return QStringLiteral("DVI");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL: return QStringLiteral("internal");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST: return QStringLiteral("Miracast");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED: return QStringLiteral("indirect");
    case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL: return QStringLiteral("virtual");
    default: return QString::number(int(t));
    }
}

QJsonArray displays() {
    QJsonArray out;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return out;
    paths.resize(pathCount);
    for (const auto& path : paths) {
        QJsonObject display;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) display["device"] = wide(source.viewGdiDeviceName);
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) display["monitor"] = wide(target.monitorFriendlyDeviceName);
        const auto& rate = path.targetInfo.refreshRate;
        if (rate.Denominator) display["refreshHz"] = double(rate.Numerator) / double(rate.Denominator);
        display["output"] = outputTechnology(path.targetInfo.outputTechnology);
        display["adapterLuid"] = luidText(path.sourceInfo.adapterId);
        display["rotation"] = int(path.targetInfo.rotation);
        if (path.sourceInfo.modeInfoIdx < modes.size() && modes[path.sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            const auto& m = modes[path.sourceInfo.modeInfoIdx].sourceMode;
            display["width"] = int(m.width);
            display["height"] = int(m.height);
            display["primary"] = m.position.x == 0 && m.position.y == 0;
        }
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
            display["hdrSupported"] = bool(color.advancedColorSupported);
            display["hdrEnabled"] = bool(color.advancedColorEnabled);
            display["bitsPerColor"] = int(color.bitsPerColorChannel);
        }
        out.append(display);
    }
    return out;
}

QJsonObject graphicsSettings() {
    QJsonObject g;
    if (const auto hags = regDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode"))
        g["hwSchMode"] = int(*hags);
    else
        g["hwSchMode"] = QStringLiteral("absent (driver default)");
    if (const auto mode = regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"AutoGameModeEnabled"))
        g["gameModeEnabled"] = *mode != 0;
    else
        g["gameModeEnabled"] = QStringLiteral("default(on)");
    const wchar_t* prefs = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
    g["directxUserGlobalSettings"] = regString(HKEY_CURRENT_USER, prefs, L"DirectXUserGlobalSettings");
    const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    g["veyraGpuPreference"] = regString(HKEY_CURRENT_USER, prefs, reinterpret_cast<const wchar_t*>(exe.utf16()));
    return g;
}

QJsonObject powerInfo() {
    QJsonObject p;
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) == ERROR_SUCCESS && scheme) {
        p["schemeGuid"] = guidText(*scheme);
        DWORD size = 0;
        if (PowerReadFriendlyName(nullptr, scheme, nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS && size) {
            std::vector<UCHAR> name(size);
            if (PowerReadFriendlyName(nullptr, scheme, nullptr, nullptr, name.data(), &size) == ERROR_SUCCESS)
                p["scheme"] = wide(reinterpret_cast<const wchar_t*>(name.data()));
        }
        LocalFree(scheme);
    }
    // Windows 11 power mode slider (an overlay over the plan).
    using Overlay = DWORD(WINAPI*)(GUID*);
    if (HMODULE powrprof = GetModuleHandleW(L"powrprof.dll"))
        if (auto overlay = reinterpret_cast<Overlay>(GetProcAddress(powrprof, "PowerGetEffectiveOverlayScheme"))) {
            GUID mode{};
            if (overlay(&mode) == ERROR_SUCCESS) {
                const QString id = guidText(mode).toLower();
                p["powerModeGuid"] = id;
                p["powerMode"] = id.contains(QStringLiteral("ded574b5")) ? QStringLiteral("best-performance")
                               : id.contains(QStringLiteral("961cc777")) ? QStringLiteral("best-power-efficiency")
                               : id.contains(QStringLiteral("3af9b8d9")) ? QStringLiteral("balanced-efficiency")
                               : QStringLiteral("balanced");
            }
        }
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status)) {
        p["acLine"] = status.ACLineStatus == 1 ? QStringLiteral("online") : status.ACLineStatus == 0 ? QStringLiteral("battery") : QStringLiteral("unknown");
        p["hasBattery"] = status.BatteryFlag != 128 && status.BatteryFlag != 255;
        if (status.BatteryLifePercent <= 100) p["batteryPercent"] = int(status.BatteryLifePercent);
        p["batterySaver"] = status.SystemStatusFlag == 1;
    }
    return p;
}

struct Known { const wchar_t* name; const char* product; bool prefix; };
// Modules seen in past field cases (see docs/PERF_DIAGNOSTICS.md).
constexpr Known kModules[] = {
    {L"NvPresent64.dll", "NVIDIA Smooth Motion (driver frame generation)", false},
    {L"RTSSHooks64.dll", "RivaTuner Statistics Server", false},
    {L"GPP64.dll", "GamePP", false}, {L"GameTracker64.dll", "GamePP", false}, {L"shade64.dll", "GamePP", false},
    {L"AudioDevProps2.dll", "Nahimic", false}, {L"NahimicOSD.dll", "Nahimic", false},
    {L"GameOverlayRenderer64.dll", "Steam overlay", false},
    {L"DiscordHook64.dll", "Discord overlay", false},
    {L"graphics-hook64.dll", "OBS game capture hook", false},
    {L"nvspcap64.dll", "NVIDIA overlay capture", false},
    {L"igo64.dll", "EA app overlay", false},
    {L"ReShade64.dll", "ReShade", false},
    {L"bdcamvk64.dll", "Bandicam", false}, {L"bdcap64.dll", "Bandicam", false},
};
constexpr Known kProcesses[] = {
    {L"RTSS.exe", "RivaTuner Statistics Server", false}, {L"MSIAfterburner.exe", "MSI Afterburner", false},
    {L"GamePP", "GamePP", true},
    {L"NVIDIA Overlay.exe", "NVIDIA overlay", false}, {L"NVIDIA Share.exe", "NVIDIA overlay", false},
    {L"NVIDIA app.exe", "NVIDIA app", false},
    {L"obs64.exe", "OBS Studio", false}, {L"obs32.exe", "OBS Studio", false},
    {L"Streamlabs OBS.exe", "Streamlabs", false},
    {L"直播伴侣.exe", "Douyin live companion", false}, {L"MediaSDK_Server.exe", "Douyin live companion", false},
    {L"GameBar.exe", "Xbox Game Bar", false}, {L"XboxGameBarWidgets.exe", "Xbox Game Bar", false},
    {L"Discord.exe", "Discord", false}, {L"steam.exe", "Steam", false},
    {L"RECentral.exe", "AVerMedia RECentral", false}, {L"StreamingCenter.exe", "AVerMedia Streaming Center", false},
    {L"4KCaptureUtility.exe", "Elgato 4K Capture Utility", false}, {L"Elgato Studio.exe", "Elgato Studio", false},
    {L"Camera Hub.exe", "Elgato Camera Hub", false},
    {L"wallpaper64.exe", "Wallpaper Engine", false}, {L"wallpaper32.exe", "Wallpaper Engine", false},
    {L"HWiNFO64.exe", "HWiNFO", false}, {L"aida64.exe", "AIDA64", false}, {L"CapFrameX.exe", "CapFrameX", false},
    {L"PresentMon", "PresentMon", true}, {L"Bandicam.exe", "Bandicam", false}, {L"iCUE.exe", "Corsair iCUE", false},
    {L"ArmouryCrate", "ASUS Armoury Crate", true}, {L"LightingService.exe", "ASUS Aura", false},
};

bool matches(const wchar_t* name, const Known& k) {
    return k.prefix ? _wcsnicmp(name, k.name, wcslen(k.name)) == 0 : _wcsicmp(name, k.name) == 0;
}

QJsonObject moduleInfo() {
    QJsonObject result;
    QJsonArray flagged, third;
    HMODULE modules[1024];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        wchar_t windowsDir[MAX_PATH]{}, exeDir[MAX_PATH]{};
        GetWindowsDirectoryW(windowsDir, MAX_PATH);
        GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        if (auto* slash = wcsrchr(exeDir, L'\\')) slash[1] = 0;
        const auto startsWith = [](const wchar_t* path, const wchar_t* prefix) {
            const size_t n = wcslen(prefix); return n && _wcsnicmp(path, prefix, n) == 0;
        };
        const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), DWORD(std::size(modules)));
        for (DWORD i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH]{};
            if (!GetModuleFileNameW(modules[i], path, MAX_PATH)) continue;
            const wchar_t* base = wcsrchr(path, L'\\');
            base = base ? base + 1 : path;
            for (const auto& k : kModules)
                if (matches(base, k)) flagged.append(QJsonObject{{"module", wide(base)}, {"product", k.product}});
            // System graphics DLLs loaded from anywhere but System32 are wrappers.
            for (const wchar_t* graphics : {L"dxgi.dll", L"d3d12.dll", L"d3d11.dll"})
                if (_wcsicmp(base, graphics) == 0 && !startsWith(path, windowsDir))
                    flagged.append(QJsonObject{{"module", wide(base)}, {"product", "graphics wrapper outside Windows (ReShade/Special K/...)"}});
            const bool windows = startsWith(path, windowsDir) && !wcsstr(path, L"\\DriverStore\\");
            if (!windows && !startsWith(path, exeDir)) third.append(wide(base));
        }
    }
    result["flagged"] = flagged;
    result["thirdParty"] = third;
    return result;
}

QJsonObject processInfo() {
    QJsonObject result;
    QJsonArray flagged;
    int otherVeyra = 0;
    const HANDLE raw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (raw != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        std::map<QString, QString> seen;
        for (BOOL found = Process32FirstW(raw, &process); found; found = Process32NextW(raw, &process)) {
            if (_wcsicmp(process.szExeFile, L"veyra_qml_ui.exe") == 0 && process.th32ProcessID != GetCurrentProcessId()) ++otherVeyra;
            for (const auto& k : kProcesses)
                if (matches(process.szExeFile, k)) seen[wide(process.szExeFile)] = QString::fromUtf8(k.product);
        }
        CloseHandle(raw);
        for (const auto& [name, product] : seen) flagged.append(QJsonObject{{"process", name}, {"product", product}});
    }
    result["flagged"] = flagged;
    result["otherVeyraInstances"] = otherVeyra;
    return result;
}

// --- NVML (driver's own nvml.dll, absolute path only) ----------------------
struct Nvml {
    using Fn0 = int(*)();
    using Count = int(*)(unsigned*);
    using Handle = int(*)(unsigned, void**);
    using Name = int(*)(void*, char*, unsigned);
    struct Utilization { unsigned gpu, memory; };
    struct Memory { unsigned long long total, free, used; };
    using Util = int(*)(void*, Utilization*);
    using Clock = int(*)(void*, int, unsigned*);
    using Temp = int(*)(void*, int, unsigned*);
    using Power = int(*)(void*, unsigned*);
    using PState = int(*)(void*, int*);
    using Throttle = int(*)(void*, unsigned long long*);
    using Mem = int(*)(void*, Memory*);
    HMODULE module = nullptr;
    Fn0 init = nullptr, shutdown = nullptr;
    Util utilization = nullptr; Clock clock = nullptr; Temp temperature = nullptr; Power power = nullptr, limit = nullptr;
    PState pstate = nullptr; Throttle throttle = nullptr; Mem memory = nullptr;
    void* device = nullptr;
    std::string name;
    bool load() {
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        for (const std::wstring path : {std::wstring(system) + L"\\nvml.dll",
                                        std::wstring(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll")}) {
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (module) break;
        }
        if (!module) return false;
        const auto get = [&](const char* name) { return GetProcAddress(module, name); };
        init = reinterpret_cast<Fn0>(get("nvmlInit_v2"));
        shutdown = reinterpret_cast<Fn0>(get("nvmlShutdown"));
        auto count = reinterpret_cast<Count>(get("nvmlDeviceGetCount_v2"));
        auto handle = reinterpret_cast<Handle>(get("nvmlDeviceGetHandleByIndex_v2"));
        auto deviceName = reinterpret_cast<Name>(get("nvmlDeviceGetName"));
        utilization = reinterpret_cast<Util>(get("nvmlDeviceGetUtilizationRates"));
        clock = reinterpret_cast<Clock>(get("nvmlDeviceGetClockInfo"));
        temperature = reinterpret_cast<Temp>(get("nvmlDeviceGetTemperature"));
        power = reinterpret_cast<Power>(get("nvmlDeviceGetPowerUsage"));
        limit = reinterpret_cast<Power>(get("nvmlDeviceGetEnforcedPowerLimit"));
        pstate = reinterpret_cast<PState>(get("nvmlDeviceGetPerformanceState"));
        throttle = reinterpret_cast<Throttle>(get("nvmlDeviceGetCurrentClocksEventReasons"));
        if (!throttle) throttle = reinterpret_cast<Throttle>(get("nvmlDeviceGetCurrentClocksThrottleReasons"));
        memory = reinterpret_cast<Mem>(get("nvmlDeviceGetMemoryInfo"));
        unsigned devices = 0;
        if (!init || !shutdown || !count || !handle || init() != 0) { FreeLibrary(module); module = nullptr; return false; }
        if (count(&devices) != 0 || devices == 0 || handle(0, &device) != 0) { shutdown(); FreeLibrary(module); module = nullptr; return false; }
        char text[96]{};
        if (deviceName && deviceName(device, text, sizeof(text)) == 0) name = text;
        return true;
    }
    void unload() {
        if (!module) return;
        shutdown();
        FreeLibrary(module);
        module = nullptr;
    }
};

// Instance names look like pid_1234_luid_0x00000000_0x0000D1A2_phys_0_eng_3_engtype_VideoDecode.
struct EngineInstance { DWORD pid = 0; std::wstring engine; std::wstring type; };
bool parseEngine(const wchar_t* name, EngineInstance& out) {
    if (_wcsnicmp(name, L"pid_", 4) != 0) return false;
    out.pid = wcstoul(name + 4, nullptr, 10);
    const wchar_t* luid = wcsstr(name, L"_luid_");
    const wchar_t* type = wcsstr(name, L"_engtype_");
    if (!luid || !type) return false;
    out.engine.assign(luid, type);
    out.type = type + 9;
    return true;
}
enum Group { G3d, GCompute, GCopy, GDecode, GEncode, GOther, GCount };
Group groupOf(const std::wstring& type) {
    if (_wcsicmp(type.c_str(), L"3D") == 0) return G3d;
    if (_wcsnicmp(type.c_str(), L"Compute", 7) == 0 || _wcsnicmp(type.c_str(), L"Cuda", 4) == 0) return GCompute;
    if (_wcsnicmp(type.c_str(), L"Copy", 4) == 0) return GCopy;
    if (_wcsnicmp(type.c_str(), L"VideoDecode", 11) == 0) return GDecode;
    if (_wcsnicmp(type.c_str(), L"VideoEncode", 11) == 0) return GEncode;
    return GOther;
}

std::vector<std::pair<std::wstring, double>> counterArray(PDH_HCOUNTER counter) {
    std::vector<std::pair<std::wstring, double>> out;
    DWORD bytes = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr) != PDH_MORE_DATA) return out;
    std::vector<BYTE> buffer(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return out;
    out.reserve(count);
    for (DWORD i = 0; i < count; ++i)
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
            out.emplace_back(items[i].szName, items[i].FmtValue.doubleValue);
    return out;
}

uint64_t fileTime(const FILETIME& t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
int64_t unixMsNow() {
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    return int64_t(fileTime(ft) / 10000) - 11644473600000LL;
}

QString snapshotName(DWORD pid) {
    static std::unordered_map<DWORD, QString> names;
    static ULONGLONG refreshed = 0;
    if (!names.count(pid) && GetTickCount64() - refreshed > 10000) {
        refreshed = GetTickCount64();
        names.clear();
        const HANDLE raw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (raw != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            for (BOOL found = Process32FirstW(raw, &entry); found; found = Process32NextW(raw, &entry))
                names[entry.th32ProcessID] = QString::fromWCharArray(entry.szExeFile);
            CloseHandle(raw);
        }
    }
    const auto it = names.find(pid);
    return it == names.end() ? QStringLiteral("pid%1").arg(pid) : it->second;
}

QString processName(DWORD pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return snapshotName(pid).replace(QLatin1Char(','), QLatin1Char(' '));
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    QString name = QStringLiteral("pid%1").arg(pid);
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        const wchar_t* base = wcsrchr(path, L'\\');
        name = wide(base ? base + 1 : path);
    }
    CloseHandle(process);
    return name.replace(QLatin1Char(','), QLatin1Char(' '));
}
} // namespace

QString redactText(QString text) {
    // C:\Users\<name>\..., also with forward slashes and in escaped JSON.
    static const QRegularExpression users(QStringLiteral(R"(([A-Za-z]:(?:\\\\|\\|/)+Users(?:\\\\|\\|/)+)[^\\/"\r\n:*?<>|]+)"),
                                          QRegularExpression::CaseInsensitiveOption);
    text.replace(users, QStringLiteral("\\1%USERNAME%"));
    return text;
}

QJsonObject systemSnapshot() {
    QJsonObject s;
    s["os"] = osInfo();
    s["cpu"] = cpuInfo();
    s["gpus"] = adapters();
    // HAGS as the first hardware adapter reports it (the registry value is only a request).
    QJsonObject graphics = graphicsSettings();
    for (const auto& gpu : s["gpus"].toArray())
        if (!gpu.toObject().value(QStringLiteral("software")).toBool() && gpu.toObject().contains(QStringLiteral("hagsEnabled"))) {
            graphics["hagsEnabled"] = gpu.toObject().value(QStringLiteral("hagsEnabled"));
            break;
        }
    s["displays"] = displays();
    s["graphics"] = graphics;
    s["power"] = powerInfo();
    s["modules"] = moduleInfo();
    s["processes"] = processInfo();
    QJsonObject time;
    TIME_ZONE_INFORMATION zone{};
    const DWORD zoneId = GetTimeZoneInformation(&zone);
    time["utcOffsetMinutes"] = -int(zone.Bias + (zoneId == TIME_ZONE_ID_DAYLIGHT ? zone.DaylightBias : 0));
    time["utcMs"] = double(unixMsNow());
    s["time"] = time;
    return s;
}

// --- sampler ----------------------------------------------------------------
struct TelemetrySampler::State {
    FILE* file = nullptr;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr, processMemory = nullptr, adapterMemory = nullptr;
    Nvml nvml;
    bool nvmlLoaded = false;
    uint64_t lastIdle = 0, lastKernel = 0, lastUser = 0, lastProcess = 0, lastWall = 0;
    std::unordered_map<DWORD, QString> names;
    double maxUiGapMs = 0;
    int64_t lastPushHost = 0;
};

TelemetrySampler::TelemetrySampler() : state_(new State) {}
TelemetrySampler::~TelemetrySampler() { stop(); delete state_; }

std::string TelemetrySampler::sources() const {
    std::string out = "cpu";
    if (state_->engine) out += " pdh-gpu-engine";
    if (state_->processMemory) out += " pdh-gpu-memory";
    if (state_->nvmlLoaded) out += " nvml(" + state_->nvml.name + ")";
    return out;
}

bool TelemetrySampler::start(const std::filesystem::path& csv) {
    stop();
    auto& st = *state_;
    st.file = _wfopen(csv.c_str(), L"wb");
    if (!st.file) return false;
    std::fputs("host,utc_ms,cpu_total,cpu_veyra,mem_load,veyra_private_mib,veyra_ws_mib,"
               "gpu_busy_max,gpu_3d_total,gpu_compute_total,gpu_veyra_max,gpu_veyra_3d,gpu_veyra_compute,gpu_veyra_copy,"
               "gpu_veyra_vdec,gpu_veyra_venc,gpu_others_max,gpu_top_other_util,gpu_top_other_name,"
               "gpu_veyra_dedicated_mib,gpu_adapter_dedicated_mib,"
               "nv_util,nv_mem_util,nv_clock_mhz,nv_mem_clock_mhz,nv_temp_c,nv_power_w,nv_power_limit_w,nv_pstate,nv_throttle,"
               "nv_mem_used_mib,nv_mem_total_mib,ui_gap_ms,running,capture,submit_fps,low1_fps,capture_fps,late_ms,late_p95_ms,"
               "sched_wait_p95_ms,process_cpu_p95_ms,present_cpu_p95_ms,frames,generated,capture_received,capture_dropped,"
               "capture_rate_skipped,preview_skipped,fg_budget_limited,display_vrr,composition_mode,content_fps,"
               "nominal_source_fps,display_refresh_hz,source_w,source_h,applying\n", st.file);
    std::fflush(st.file);
    if (PdhOpenQueryW(nullptr, 0, &st.query) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(st.query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &st.engine) != ERROR_SUCCESS) st.engine = nullptr;
        if (PdhAddEnglishCounterW(st.query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &st.processMemory) != ERROR_SUCCESS) st.processMemory = nullptr;
        if (PdhAddEnglishCounterW(st.query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &st.adapterMemory) != ERROR_SUCCESS) st.adapterMemory = nullptr;
        PdhCollectQueryData(st.query);
    }
    st.nvmlLoaded = st.nvml.load();
    FILETIME idle{}, kernel{}, user{}, created{}, exited{}, pk{}, pu{}, now{};
    GetSystemTimes(&idle, &kernel, &user);
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &pk, &pu);
    GetSystemTimeAsFileTime(&now);
    st.lastIdle = fileTime(idle); st.lastKernel = fileTime(kernel); st.lastUser = fileTime(user);
    st.lastProcess = fileTime(pk) + fileTime(pu); st.lastWall = fileTime(now);
    {
        std::lock_guard lock(mutex_);
        stopping_ = false; haveSample_ = false; st.maxUiGapMs = 0; st.lastPushHost = 0;
    }
    rows_ = 0;
    thread_ = std::thread([this] { run(); });
    return true;
}

void TelemetrySampler::push(const EngineSample& sample) {
    std::lock_guard lock(mutex_);
    if (state_->lastPushHost) state_->maxUiGapMs = std::max(state_->maxUiGapMs, double(sample.host - state_->lastPushHost) / 10000.0);
    state_->lastPushHost = sample.host;
    latest_ = sample;
    haveSample_ = true;
}

void TelemetrySampler::stop() {
    if (thread_.joinable()) {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        wake_.notify_all();
        thread_.join();
    }
    auto& st = *state_;
    if (st.query) { PdhCloseQuery(st.query); st.query = nullptr; st.engine = st.processMemory = st.adapterMemory = nullptr; }
    if (st.nvmlLoaded) { st.nvml.unload(); st.nvmlLoaded = false; }
    if (st.file) { std::fclose(st.file); st.file = nullptr; }
}

void TelemetrySampler::run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    auto& st = *state_;
    const DWORD self = GetCurrentProcessId();
    const int cores = std::max<int>(1, int(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
    std::string row;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            if (wake_.wait_for(lock, std::chrono::seconds(1), [&] { return stopping_; })) break;
        }
        row.clear();
        char cell[160];
        const auto add = [&](const char* format, auto... values) {
            std::snprintf(cell, sizeof(cell), format, values...);
            row += cell;
        };
        const auto empty = [&] { row += ','; };
        add("%lld,%lld", static_cast<long long>(diagnostics::traceHostNow()), static_cast<long long>(unixMsNow()));
        // CPU
        FILETIME idle{}, kernel{}, user{}, created{}, exited{}, pk{}, pu{}, now{};
        GetSystemTimes(&idle, &kernel, &user);
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &pk, &pu);
        GetSystemTimeAsFileTime(&now);
        const uint64_t i = fileTime(idle), k = fileTime(kernel), u = fileTime(user), p = fileTime(pk) + fileTime(pu), w = fileTime(now);
        const double total = double((k - st.lastKernel) + (u - st.lastUser));
        const double busy = total > 0 ? 100.0 * (total - double(i - st.lastIdle)) / total : 0;
        const double wall = double(w - st.lastWall);
        const double proc = wall > 0 ? 100.0 * double(p - st.lastProcess) / (wall * cores) : 0;
        st.lastIdle = i; st.lastKernel = k; st.lastUser = u; st.lastProcess = p; st.lastWall = w;
        MEMORYSTATUSEX memory{sizeof(memory)};
        GlobalMemoryStatusEx(&memory);
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
        add(",%.1f,%.1f,%lu,%.0f,%.0f", busy, proc, memory.dwMemoryLoad, double(counters.PrivateUsage) / 1048576.0, double(counters.WorkingSetSize) / 1048576.0);
        // GPU engines
        if (st.query && PdhCollectQueryData(st.query) == ERROR_SUCCESS && st.engine) {
            std::map<std::wstring, double> engineTotal, engineSelf, engineOthers;
            std::map<std::wstring, Group> engineGroup;
            std::array<double, GCount> self3{};
            std::unordered_map<DWORD, double> otherPeak;
            for (const auto& [name, value] : counterArray(st.engine)) {
                EngineInstance e;
                if (!parseEngine(name.c_str(), e)) continue;
                const Group g = groupOf(e.type);
                engineGroup[e.engine] = g;
                engineTotal[e.engine] += value;
                if (e.pid == self) { engineSelf[e.engine] += value; self3[g] += value; }
                else { engineOthers[e.engine] += value; otherPeak[e.pid] = std::max(otherPeak[e.pid], value); }
            }
            double busyMax = 0, total3d = 0, totalCompute = 0, selfMax = 0, othersMax = 0;
            for (const auto& [engine, value] : engineTotal) {
                busyMax = std::max(busyMax, value);
                if (engineGroup[engine] == G3d) total3d = std::max(total3d, value);
                if (engineGroup[engine] == GCompute) totalCompute = std::max(totalCompute, value);
            }
            for (const auto& [engine, value] : engineSelf) selfMax = std::max(selfMax, value);
            for (const auto& [engine, value] : engineOthers) othersMax = std::max(othersMax, value);
            DWORD topPid = 0; double topValue = 0;
            for (const auto& [pid, value] : otherPeak) if (pid && value > topValue) { topValue = value; topPid = pid; }
            QString topName;
            if (topPid && topValue >= 1.0) {
                auto it = st.names.find(topPid);
                if (it == st.names.end()) it = st.names.emplace(topPid, processName(topPid)).first;
                topName = it->second;
            }
            add(",%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,", busyMax, total3d, totalCompute, selfMax,
                self3[G3d], self3[GCompute], self3[GCopy], self3[GDecode], self3[GEncode], othersMax, topValue);
            row += topName.toUtf8().constData();
        } else {
            for (int c = 0; c < 12; ++c) empty();
        }
        // GPU memory
        if (st.processMemory) {
            double selfBytes = 0;
            for (const auto& [name, value] : counterArray(st.processMemory))
                if (_wcsnicmp(name.c_str(), L"pid_", 4) == 0 && wcstoul(name.c_str() + 4, nullptr, 10) == self) selfBytes += value;
            add(",%.0f", selfBytes / 1048576.0);
        } else empty();
        if (st.adapterMemory) {
            double peak = 0;
            for (const auto& [name, value] : counterArray(st.adapterMemory)) peak = std::max(peak, value);
            add(",%.0f", peak / 1048576.0);
        } else empty();
        // NVML
        if (st.nvmlLoaded) {
            auto& n = st.nvml;
            Nvml::Utilization util{};
            unsigned clock = 0, memClock = 0, temp = 0, power = 0, limit = 0;
            int pstate = -1;
            unsigned long long reasons = 0;
            Nvml::Memory mem{};
            const bool haveUtil = n.utilization && n.utilization(n.device, &util) == 0;
            const bool haveClock = n.clock && n.clock(n.device, 0, &clock) == 0;
            const bool haveMemClock = n.clock && n.clock(n.device, 2, &memClock) == 0;
            const bool haveTemp = n.temperature && n.temperature(n.device, 0, &temp) == 0;
            const bool havePower = n.power && n.power(n.device, &power) == 0;
            const bool haveLimit = n.limit && n.limit(n.device, &limit) == 0;
            const bool havePState = n.pstate && n.pstate(n.device, &pstate) == 0;
            const bool haveReasons = n.throttle && n.throttle(n.device, &reasons) == 0;
            const bool haveMem = n.memory && n.memory(n.device, &mem) == 0;
            if (haveUtil) add(",%u,%u", util.gpu, util.memory); else { empty(); empty(); }
            if (haveClock) add(",%u", clock); else empty();
            if (haveMemClock) add(",%u", memClock); else empty();
            if (haveTemp) add(",%u", temp); else empty();
            if (havePower) add(",%.1f", power / 1000.0); else empty();
            if (haveLimit) add(",%.1f", limit / 1000.0); else empty();
            if (havePState) add(",%d", pstate); else empty();
            if (haveReasons) add(",0x%llx", reasons); else empty();
            if (haveMem) add(",%.0f,%.0f", double(mem.used >> 20), double(mem.total >> 20)); else { empty(); empty(); }
        } else {
            for (int c = 0; c < 11; ++c) empty();
        }
        // Engine sample from the interface thread
        EngineSample sample;
        bool have = false;
        double gap = 0;
        {
            std::lock_guard lock(mutex_);
            sample = latest_; have = haveSample_;
            gap = std::max(st.maxUiGapMs, st.lastPushHost ? double(diagnostics::traceHostNow() - st.lastPushHost) / 10000.0 : 0.0);
            st.maxUiGapMs = 0;
        }
        add(",%.0f", gap);
        if (have) {
            const auto opt = [&](double v) { if (v >= 0) add(",%.2f", v); else empty(); };
            add(",%d,%d", int(sample.running), int(sample.capture));
            opt(sample.submitFps); opt(sample.low1Fps);
            add(",%.2f,%.3f,%.3f,%.3f,%.3f,%.3f", sample.captureFps, sample.lateMs, sample.lateP95Ms,
                sample.schedulingWaitP95Ms, sample.processCpuP95Ms, sample.presentCpuP95Ms);
            add(",%llu,%llu,%llu,%llu,%llu,%llu", static_cast<unsigned long long>(sample.frames), static_cast<unsigned long long>(sample.generated),
                static_cast<unsigned long long>(sample.captureReceived), static_cast<unsigned long long>(sample.captureDropped),
                static_cast<unsigned long long>(sample.captureRateSkipped), static_cast<unsigned long long>(sample.previewSkipped));
            add(",%d,%u,%d,%d,%.3f,%.3f,%u,%u,%d", int(sample.fgBudgetLimited), sample.displayVrr, sample.compositionMode,
                sample.contentFps, sample.nominalSourceFps, sample.displayRefreshHz, sample.sourceWidth, sample.sourceHeight, int(sample.applying));
        } else {
            for (int c = 0; c < 25; ++c) empty();
        }
        row += '\n';
        if (st.file) { std::fputs(row.c_str(), st.file); std::fflush(st.file); }
        ++rows_;
    }
}

}
