#include "veyra/source/AverMediaAudioSwitch.h"

#include "veyra/Log.h"

#include <windows.h>
#include <setupapi.h>

#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace veyra::source {
namespace {

bool containsCaseInsensitive(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;
    for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool match = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            if (towlower(haystack[start + i]) != towlower(needle[i])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

std::wstring deviceProperty(HDEVINFO set, SP_DEVINFO_DATA* info, DWORD property) {
    wchar_t buffer[1024] = {};
    DWORD type = 0;
    DWORD bytes = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, info, property, &type, reinterpret_cast<BYTE*>(buffer), sizeof(buffer), &bytes)) return {};
    if (type != REG_SZ && type != REG_EXPAND_SZ) return {};
    return std::wstring(buffer, bytes / sizeof(wchar_t) > 0 ? (bytes / sizeof(wchar_t)) - 1 : 0);
}

// Device interface classes that carry the real "\\?\usb#vid_...#{guid}" path a
// USB audio function is reached by. Declared locally so this translation unit
// does not need INITGUID (which would leak definitions into other TUs).
constexpr GUID kCategoryAudio = {0x6994AD04, 0x93EF, 0x11D0, {0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
constexpr GUID kCategoryCapture = {0x65E8773D, 0x8F56, 0x11D0, {0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};

// Lower-case display-name form the vendor parser expects: it looks for the
// literal tokens "vid_" and "&pid_", so an upper-case instance id would never
// match. Used only when no real interface path could be resolved.
std::wstring syntheticInterfacePath(const std::wstring& instanceId) {
    std::wstring lowered;
    lowered.reserve(instanceId.size() + 8);
    for (wchar_t character : instanceId) lowered.push_back(wchar_t(towlower(character)));
    for (wchar_t& character : lowered) if (character == L'\\') character = L'#';
    return L"\\\\?\\" + lowered;
}

// ---------------------------------------------------------------------------
// ABI mirror of the component's public headers (GPLv2, shipped in
// AVerMedia-Technologies-Inc/obs-MultichannelAudio). Only the layout matters:
// both the component and Veyra are built with the MSVC x64 toolchain, so these
// are plain aggregates with the same member order.
// ---------------------------------------------------------------------------
struct VendorDeviceOpenerParam {
    std::wstring name;  // +0x00
    std::wstring path;  // +0x20
};

using RawFn = long long (*)(void*, void*, void*, void*, void*);

struct RawCall {
    RawFn fn = nullptr;
    void* self = nullptr;
    void* a = nullptr;
    void* b = nullptr;
    void* c = nullptr;
    void* d = nullptr;
    long long result = -1;
    unsigned sehCode = 0;
};

// SEH must live in a function that owns no C++ objects, which is why the
// invocation is split away from everything else. Foreign code can both throw
// (their wrappers use std::string/throw) and fault, so both are handled.
void invokeRaw(RawCall* call) noexcept {
    __try {
        call->result = call->fn(call->self, call->a, call->b, call->c, call->d);
    } __except (call->sehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        call->result = -1;
    }
}

// Entry points of avt_device_opener.dll, decorated names as exported.
constexpr const char* kExportVendorSdkCtor = "??0VendorSdk@AVerMedia@@QEAA@PEBD@Z";
constexpr const char* kExportVendorSdkDtor = "??1VendorSdk@AVerMedia@@QEAA@XZ";
constexpr const char* kExportSdkInitialize = "?initialize@VendorSdk@AVerMedia@@QEAAHXZ";
constexpr const char* kExportSdkUninitialize = "?uninitialize@VendorSdk@AVerMedia@@QEAAHXZ";
constexpr const char* kExportSdkClosePort = "?closePort@VendorSdk@AVerMedia@@QEAAHXZ";
constexpr const char* kExportSdkGetAudioFormat = "?getAudioFormat@VendorSdk@AVerMedia@@QEAAHPEAH@Z";
// Public SDK signatures; PR64's author reports verification on 1.0.15.
// Optional so older components still load. No real component on our test host.
constexpr const char* kExportSdkGetNonPcmOnOff = "?getNonPcmOnOff@VendorSdk@AVerMedia@@QEAAHPEAI@Z";
constexpr const char* kExportSdkSetNonPcmOnOff = "?setNonPcmOnOff@VendorSdk@AVerMedia@@QEAAHH@Z";
constexpr const char* kExportOpenerCtor = "??0DeviceOpener@AVerMedia@@QEAA@XZ";
constexpr const char* kExportOpenerDtor = "??1DeviceOpener@AVerMedia@@QEAA@XZ";
constexpr const char* kExportOpenerSetVendorSdk = "?SetVendorSdk@DeviceOpener@AVerMedia@@QEAAXPEAVVendorSdk@2@@Z";
constexpr const char* kExportOpenerSwitch = "?SwitchDeviceThenDetectAudioFormat@DeviceOpener@AVerMedia@@QEAAXAEBUDeviceOpenerParam@2@@Z";
constexpr const char* kExportOpenerIsNonPcm = "?IsAudioFormatNonPcm@DeviceOpener@AVerMedia@@QEBA_NXZ";
constexpr const char* kExportOpenerStartChecking = "?StartChecking@DeviceOpener@AVerMedia@@QEAAXXZ";
constexpr const char* kExportOpenerStopChecking = "?StopChecking@DeviceOpener@AVerMedia@@QEAAXXZ";
// Static log sinks of the component. Plain function pointers (not
// std::function), so installing them crosses no C++ ABI that could bite us.
// Their own plugin installs the same four before it calls anything else.
constexpr const char* kExportSetDebugHandler = "?setDebugHandler@LogHelper@@YAXP6AXPEBDPEAD@Z@Z";
constexpr const char* kExportSetErrorHandler = "?setErrorHandler@LogHelper@@YAXP6AXPEBDPEAD@Z@Z";
constexpr const char* kExportSetInfoHandler = "?setInfoHandler@LogHelper@@YAXP6AXPEBDPEAD@Z@Z";
constexpr const char* kExportSetWarningHandler = "?setWarningHandler@LogHelper@@YAXP6AXPEBDPEAD@Z@Z";

// The component prints its diagnostics through these; without them the reason a
// switch failed stays invisible (the 2026-09-17 run only ever showed our own
// "card not reached" line). Signature matches their void(const char*, va_list).
void vendorLogSink(const char* format, char* args) noexcept {
    if (format == nullptr) return;
    char buffer[1024] = {};
    try {
        if (args != nullptr) vsnprintf(buffer, sizeof(buffer), format, args);
        else snprintf(buffer, sizeof(buffer), "%s", format);
    } catch (...) {
        return;
    }
    veyra::log::info("capture-audio-vendor", std::string("vendor: ") + buffer);
}

using SetVendorLogHandler = void (*)(void (*)(const char*, char*));

// DeviceOpener's own sink: the documented way their plugin collects these
// messages ("SetLogHandler" with a std::function<void(int,const char*)>).
// std::wstring already crossed this boundary correctly (the component read the
// DeviceOpenerParam we built), and the call is guarded, so a surprise here is
// reported instead of fatal.
constexpr const char* kExportOpenerSetLogHandler =
    "?SetLogHandler@DeviceOpener@AVerMedia@@QEAAXAEBV?$function@$$A6AXHPEBD@Z@std@@@Z";

void vendorOpenerLog(int level, const char* message) noexcept {
    if (message == nullptr) return;
    try {
        if (level == 1) veyra::log::warn("capture-audio-vendor", std::string("vendor: ") + message);
        else veyra::log::info("capture-audio-vendor", std::string("vendor: ") + message);
    } catch (...) {
    }
}

// The chip reports this value while the HDMI source sends a compressed
// (non-PCM) stream; their enable path refuses to switch for anything else.
constexpr int kChipFormatNonPcm = 20;

// Both classes hold a single opaque implementation pointer, but the storage is
// over-sized on purpose: the constructor only writes what it needs, and a
// larger zeroed block keeps a layout surprise from corrupting the stack.
struct ObjectStorage {
    alignas(16) unsigned char bytes[64] = {};
    void* raw() noexcept { return bytes; }
};

std::string narrowUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

bool fileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool directoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring registryPathValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0;
    DWORD bytes = 0;
    std::wstring value;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_SZ && bytes > sizeof(wchar_t)) {
        value.resize(bytes / sizeof(wchar_t));
        if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS) value.clear();
        while (!value.empty() && value.back() == L'\0') value.pop_back();
    }
    RegCloseKey(key);
    return value;
}

std::wstring joinPath(const std::wstring& base, const wchar_t* relative) {
    if (base.empty()) return {};
    std::wstring out = base;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out.append(relative);
    return out;
}

bool looksLikeComponent(const std::wstring& root) {
    if (root.empty()) return false;
    return fileExists(joinPath(root, L"obs-plugins\\64bit\\avt_device_opener.dll")) &&
           fileExists(joinPath(root, L"data\\obs-plugins\\AVerMediaMultichannelAudio\\RTICE_SDK_x64.dll")) &&
           fileExists(joinPath(root, L"data\\obs-plugins\\AVerMediaMultichannelAudio\\RTK_IO_x64.dll"));
}

// Qt6Core.dll is what avt_device_opener.dll links against and what OBS ships
// next to its own executable. Loading it first by absolute path keeps the
// dependency from resolving against whatever else happens to be on PATH; we
// never add OBS directories to this process's search path.
std::wstring findQt6Core(const std::wstring& root) {
    const wchar_t* candidates[] = {
        L"bin\\64bit\\Qt6Core.dll",
        L"obs-plugins\\64bit\\Qt6Core.dll",
        L"Qt6Core.dll",
    };
    for (const wchar_t* candidate : candidates) {
        const std::wstring path = joinPath(root, candidate);
        if (fileExists(path)) return path;
    }
    return {};
}

// Applies one vendor call, converting both C++ exceptions and SEH faults into
// a negative result so a foreign component can never take the capture down.
bool callVendor(RawCall& call, std::string& error) {
    if (call.fn == nullptr) {
        error = "entry point missing";
        return false;
    }
    try {
        invokeRaw(&call);
    } catch (...) {
        error = "component raised a C++ exception";
        return false;
    }
    if (call.sehCode != 0) {
        error = std::format("component faulted (SEH 0x{:08X})", call.sehCode);
        return false;
    }
    return true;
}

} // namespace

struct AverMediaAudioSwitch::Impl {
    struct Exports {
        RawFn vendorCtor = nullptr;
        RawFn vendorDtor = nullptr;
        RawFn initialize = nullptr;
        RawFn uninitialize = nullptr;
        RawFn closePort = nullptr;
        RawFn getAudioFormat = nullptr;
        RawFn getNonPcmOnOff = nullptr;  // optional
        RawFn setNonPcmOnOff = nullptr;  // optional
        RawFn openerCtor = nullptr;
        RawFn openerDtor = nullptr;
        RawFn setVendorSdk = nullptr;
        RawFn switchDevice = nullptr;
        RawFn setLogHandler = nullptr;  // optional
        RawFn isNonPcm = nullptr;
        RawFn startChecking = nullptr;
        RawFn stopChecking = nullptr;
        SetVendorLogHandler setDebugHandler = nullptr;
        SetVendorLogHandler setErrorHandler = nullptr;
        SetVendorLogHandler setInfoHandler = nullptr;
        SetVendorLogHandler setWarningHandler = nullptr;
        bool complete() const {
            return vendorCtor && vendorDtor && initialize && uninitialize && closePort && getAudioFormat &&
                   openerCtor && openerDtor && setVendorSdk && switchDevice && isNonPcm &&
                   startChecking && stopChecking;
        }
    };

    AverMediaSwitchStatus status;
    std::wstring componentRoot;   // install root (owns obs-plugins\64bit)
    std::wstring dataDirectory;   // ...\data\obs-plugins\AVerMediaMultichannelAudio
    std::string dataDirectoryUtf8;
    HMODULE avtModule = nullptr;
    HMODULE qtModule = nullptr;
    Exports exports;
    ObjectStorage sdkStorage;
    ObjectStorage openerStorage;
    bool sdkConstructed = false;
    bool openerConstructed = false;
    bool sdkInitialized = false;
    bool checking = false;
    std::wstring lastAppliedPath;

    void* sdk() noexcept { return sdkStorage.raw(); }
    void* opener() noexcept { return openerStorage.raw(); }

    bool resolveExports() {
        struct Entry { const char* name; RawFn* target; };
        const Entry entries[] = {
            {kExportVendorSdkCtor, &exports.vendorCtor},
            {kExportVendorSdkDtor, &exports.vendorDtor},
            {kExportSdkInitialize, &exports.initialize},
            {kExportSdkUninitialize, &exports.uninitialize},
            {kExportSdkClosePort, &exports.closePort},
            {kExportSdkGetAudioFormat, &exports.getAudioFormat},
            {kExportSdkGetNonPcmOnOff, &exports.getNonPcmOnOff},
            {kExportSdkSetNonPcmOnOff, &exports.setNonPcmOnOff},
            {kExportOpenerCtor, &exports.openerCtor},
            {kExportOpenerDtor, &exports.openerDtor},
            {kExportOpenerSetVendorSdk, &exports.setVendorSdk},
            {kExportOpenerSwitch, &exports.switchDevice},
            {kExportOpenerSetLogHandler, &exports.setLogHandler},
            {kExportOpenerIsNonPcm, &exports.isNonPcm},
            {kExportOpenerStartChecking, &exports.startChecking},
            {kExportOpenerStopChecking, &exports.stopChecking},
        };
        for (const Entry& entry : entries) {
            FARPROC address = GetProcAddress(avtModule, entry.name);
            if (address == nullptr) {
                // The log sink is optional: keep working with components that
                // do not export it, we just lose their internal trace.
                if (entry.target == &exports.setLogHandler ||
                    entry.target == &exports.getNonPcmOnOff ||
                    entry.target == &exports.setNonPcmOnOff) continue;
                status.detail = std::format("missing export {}", entry.name);
                return false;
            }
            *entry.target = reinterpret_cast<RawFn>(address);
        }
        return true;
    }

    // Log sinks are optional: an older component without them must still load.
    void installLogSinks() {
        const struct { const char* name; SetVendorLogHandler* slot; } sinks[] = {
            {kExportSetDebugHandler, &exports.setDebugHandler},
            {kExportSetErrorHandler, &exports.setErrorHandler},
            {kExportSetInfoHandler, &exports.setInfoHandler},
            {kExportSetWarningHandler, &exports.setWarningHandler},
        };
        for (const auto& sink : sinks) {
            const FARPROC address = GetProcAddress(avtModule, sink.name);
            if (address == nullptr) continue;
            *sink.slot = reinterpret_cast<SetVendorLogHandler>(address);
            (*sink.slot)(vendorLogSink);
        }
    }

    bool loadComponent() {
        if (avtModule != nullptr) return true;
        componentRoot = AverMediaAudioSwitch::locateComponentRoot();
        status.componentRoot = componentRoot;
        status.componentFound = !componentRoot.empty();
        if (!status.componentFound) {
            status.detail = "AVerMedia Multichannel Audio is not installed";
            return false;
        }
        dataDirectory = joinPath(componentRoot, L"data\\obs-plugins\\AVerMediaMultichannelAudio");
        // Qt is optional here: if it is already loaded (or absent because the
        // component does not need it) the loader resolves it on its own.
        const std::wstring qtPath = findQt6Core(componentRoot);
        if (!qtPath.empty()) {
            qtModule = LoadLibraryExW(qtPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
        const std::wstring avtPath = joinPath(componentRoot, L"obs-plugins\\64bit\\avt_device_opener.dll");
        avtModule = LoadLibraryExW(avtPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (avtModule == nullptr) {
            status.detail = std::format("LoadLibraryExW failed (error {})", GetLastError());
            if (qtModule != nullptr) { FreeLibrary(qtModule); qtModule = nullptr; }
            return false;
        }
        status.dllsLoaded = true;
        if (!resolveExports()) {
            FreeLibrary(avtModule);
            avtModule = nullptr;
            status.dllsLoaded = false;
            if (qtModule != nullptr) { FreeLibrary(qtModule); qtModule = nullptr; }
            return false;
        }
        // Do this before anything else calls into the component: from here on
        // its own diagnostics land in the Veyra log instead of a console the
        // GUI process does not have.
        installLogSinks();
        return true;
    }
};

AverMediaAudioSwitch::AverMediaAudioSwitch() : p_(std::make_unique<Impl>()) {}
AverMediaAudioSwitch::~AverMediaAudioSwitch() { stop(); }

std::wstring AverMediaAudioSwitch::locateComponentRoot() {
    // Diagnostic override: a user whose component lives outside the OBS default
    // layout can point Veyra at the folder that owns obs-plugins\64bit. The
    // layout is still validated, so a bad value is ignored rather than loaded.
    wchar_t overrideBuffer[32768] = {};
    const DWORD overrideLength = GetEnvironmentVariableW(L"VEYRA_AVERMEDIA_COMPONENT", overrideBuffer, 32768);
    if (overrideLength > 0 && overrideLength < 32768) {
        const std::wstring candidate(overrideBuffer);
        if (looksLikeComponent(candidate)) return candidate;
    }
    std::wstring root = registryPathValue(HKEY_LOCAL_MACHINE,
                                          L"SOFTWARE\\AVerMedia\\OBS_Plugins\\AVerMediaMultichannelAudio", L"Path");
    if (looksLikeComponent(root)) return root;
    // Fall back to the default OBS locations when the registry entry is missing
    // (portable installs, or a plugin folder copied by hand).
    const wchar_t* environmentRoots[] = {L"ProgramFiles", L"ProgramFiles(x86)"};
    for (const wchar_t* variable : environmentRoots) {
        wchar_t buffer[MAX_PATH] = {};
        const DWORD length = GetEnvironmentVariableW(variable, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) continue;
        for (const wchar_t* suffix : {L"\\obs-studio", L"\\OBS Studio"}) {
            const std::wstring candidate = std::wstring(buffer) + suffix;
            if (looksLikeComponent(candidate)) return candidate;
        }
    }
    return {};
}

bool AverMediaAudioSwitch::isAverMediaDevicePath(std::wstring_view devicePath) {
    std::wstring lowered(devicePath);
    for (wchar_t& character : lowered) character = wchar_t(towlower(character));
    return lowered.find(L"vid_07ca") != std::wstring::npos;
}

std::vector<AverMediaUsbFunction> AverMediaAudioSwitch::findUsbFunctions(std::wstring_view vendorToken) {
    std::vector<AverMediaUsbFunction> result;
    if (vendorToken.empty()) return result;
    // Preferred route: enumerate the device interface classes directly. This is
    // what yields the "\\?\usb#vid_...#{guid}" strings the vendor component
    // parses; enumerating interfaces of a class-less device set returns none.
    for (const GUID* category : {&kCategoryAudio, &kCategoryCapture}) {
        HDEVINFO interfaces = SetupDiGetClassDevsW(category, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (interfaces == INVALID_HANDLE_VALUE) continue;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA interfaceData{};
            interfaceData.cbSize = sizeof(interfaceData);
            if (!SetupDiEnumDeviceInterfaces(interfaces, nullptr, category, index, &interfaceData)) break;
            DWORD required = 0;
            SetupDiGetDeviceInterfaceDetailW(interfaces, &interfaceData, nullptr, 0, &required, nullptr);
            if (required == 0) continue;
            std::vector<BYTE> buffer(required);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            SP_DEVINFO_DATA info{};
            info.cbSize = sizeof(info);
            if (!SetupDiGetDeviceInterfaceDetailW(interfaces, &interfaceData, detail, required, nullptr, &info)) continue;
            wchar_t instance[512] = {};
            if (!SetupDiGetDeviceInstanceIdW(interfaces, &info, instance, 512, nullptr)) continue;
            if (!containsCaseInsensitive(instance, vendorToken)) continue;
            bool duplicate = false;
            for (const auto& existing : result) {
                if (_wcsicmp(existing.instanceId.c_str(), instance) == 0) { duplicate = true; break; }
            }
            if (duplicate) continue;
            AverMediaUsbFunction function;
            function.instanceId = instance;
            function.friendlyName = deviceProperty(interfaces, &info, SPDRP_FRIENDLYNAME);
            function.interfacePath = detail->DevicePath;
            result.push_back(std::move(function));
            if (result.size() > 64) break;  // sanity bound
        }
        SetupDiDestroyDeviceInfoList(interfaces);
    }
    if (!result.empty()) return result;
    // Fallback: class-less enumeration, which still gives the instance id (that
    // alone carries vid_/pid_ once lower-cased).
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return result;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(set, index, &info)) break;
        wchar_t instance[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(set, &info, instance, 512, nullptr)) continue;
        if (!containsCaseInsensitive(instance, vendorToken)) continue;
        AverMediaUsbFunction function;
        function.instanceId = instance;
        function.friendlyName = deviceProperty(set, &info, SPDRP_FRIENDLYNAME);
        // No interface class matched: give the vendor parser the lower-cased
        // display-name shape anyway, because it only needs vid_/pid_ from here
        // and re-enumerates the device tree itself afterwards.
        function.interfacePath = syntheticInterfacePath(instance);
        // First interface of this function is enough: the vendor component
        // re-enumerates the device tree itself once it has vid/pid, and it only
        // needs one path that carries them.
        for (DWORD interfaceIndex = 0;; ++interfaceIndex) {
            SP_DEVICE_INTERFACE_DATA interfaceData{};
            interfaceData.cbSize = sizeof(interfaceData);
            if (!SetupDiEnumDeviceInterfaces(set, &info, nullptr, interfaceIndex, &interfaceData)) break;
            DWORD required = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &required, nullptr);
            if (required == 0) continue;
            std::vector<BYTE> buffer(required);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, required, nullptr, nullptr)) {
                function.interfacePath = detail->DevicePath;
                break;
            }
        }
        result.push_back(std::move(function));
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

const AverMediaUsbFunction* AverMediaAudioSwitch::pickAudioFunction(const std::vector<AverMediaUsbFunction>& functions) {
    // A UVC/UAC composite puts the audio streaming interface on a later
    // function (mi_00/mi_01 are the camera); prefer that, then an audio-looking
    // friendly name, then the only candidate.
    for (const auto& function : functions) {
        if (containsCaseInsensitive(function.instanceId, L"mi_02")) return &function;
    }
    for (const auto& function : functions) {
        if (containsCaseInsensitive(function.interfacePath, L"audio")) return &function;
    }
    for (const auto& function : functions) {
        if (containsCaseInsensitive(function.friendlyName, L"audio")) return &function;
    }
    return functions.size() == 1 ? &functions.front() : nullptr;
}

bool AverMediaAudioSwitch::apply(const std::wstring& deviceName, const std::wstring& devicePath,bool monitor) {
    auto& impl = *p_;
    // A graph rebuild reuses this SDK object. Quiesce its old monitor before
    // reading the flag and conditionally enabling; never contend with our own
    // monitoring thread between the read and the write.
    if(!monitor&&impl.checking){
        RawCall stop{impl.exports.stopChecking,impl.opener()};std::string error;
        if(!callVendor(stop,error)){impl.status.detail="StopChecking before graph setup failed: "+error;log::warn("capture-audio-vendor",impl.status.detail);return false;}
        impl.checking=false;
    }
    if (impl.sdkInitialized && !devicePath.empty() && devicePath == impl.lastAppliedPath) {
        // Already armed (for example after an audio reconnect); the vendor
        // monitor thread keeps the mode applied, so refreshing the status is
        // enough. This deliberately avoids a second setDevice/setPort cycle.
        RawCall refresh{impl.exports.isNonPcm, impl.opener()};
        std::string refreshError;
        impl.status.nonPcmActive = callVendor(refresh, refreshError) && (refresh.result & 0xFF) != 0;
        if(monitor)startMonitoring();
        return true;
    }
    if (!impl.loadComponent()) {
        impl.status.state = AverMediaSwitchState::Unavailable;
        return false;
    }
    if (!impl.status.sdkReady) {
        impl.dataDirectoryUtf8 = narrowUtf8(impl.dataDirectory);
        // The component keeps no reference to this string; const_cast only
        // satisfies the raw-call signature.
        RawCall ctor{impl.exports.vendorCtor, impl.sdk(), const_cast<char*>(impl.dataDirectoryUtf8.c_str())};
        if (!callVendor(ctor, impl.status.detail)) {
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        impl.sdkConstructed = true;
        RawCall initialize{impl.exports.initialize, impl.sdk()};
        if (!callVendor(initialize, impl.status.detail) || initialize.result != 0) {
            impl.status.detail = std::format("vendor initialize failed (ret={})", initialize.result);
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        // Their own plugin sleeps one second after initialize() before it
        // touches the device; without it the SDK is not ready yet and
        // setDevice/getAudioFormat fail ("card not reached"). Same one second
        // on teardown, between closePort and uninitialize.
        Sleep(1000);
        impl.sdkInitialized = true;
        impl.status.sdkReady = true;
    }
    if (!impl.openerConstructed) {
        RawCall ctor{impl.exports.openerCtor, impl.opener()};
        if (!callVendor(ctor, impl.status.detail)) {
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        impl.openerConstructed = true;
        RawCall bind{impl.exports.setVendorSdk, impl.opener(), impl.sdk()};
        std::string error;
        if (!callVendor(bind, error)) {
            impl.status.detail = "SetVendorSdk failed: " + error;
            impl.status.state = AverMediaSwitchState::Failed;
            return false;
        }
        if (impl.exports.setLogHandler != nullptr) {
            std::function<void(int, const char*)> sink = vendorOpenerLog;
            RawCall attachLog{impl.exports.setLogHandler, impl.opener(), &sink};
            std::string logError;
            if (!callVendor(attachLog, logError)) {
                // Losing the trace is not fatal, but say so.
                log::warn("capture-audio-vendor", std::string("component log sink could not be attached: ") + logError);
            }
        }
    }
    VendorDeviceOpenerParam param{deviceName, devicePath};
    RawCall switchCall{impl.exports.switchDevice, impl.opener(), &param};
    if (!callVendor(switchCall, impl.status.detail)) {
        impl.status.state = AverMediaSwitchState::Failed;
        return false;
    }
    int chipFormat = -1;
    RawCall formatCall{impl.exports.getAudioFormat, impl.sdk(), &chipFormat};
    std::string formatError;
    const bool formatOk = callVendor(formatCall, formatError) && formatCall.result == 0;
    impl.status.chipAudioFormat = formatOk ? chipFormat : -1;
    RawCall nonPcmCall{impl.exports.isNonPcm, impl.opener()};
    std::string nonPcmError;
    impl.status.nonPcmActive = callVendor(nonPcmCall, nonPcmError) && (nonPcmCall.result & 0xFF) != 0;
    if (!formatOk) {
        // SwitchDeviceThenDetectAudioFormat returns void and swallows its own
        // errors, so the chip query is the only confirmation we get. If it does
        // not answer, the card was not reached and the mode is not armed.
        impl.status.state = AverMediaSwitchState::Failed;
        impl.status.detail = std::format("card not reached: getAudioFormat ret={}{}{}",
            formatCall.result, formatError.empty() ? "" : " ", formatError);
        return false;
    }
    // The SDK returns the format sign-extended (measured -236 = 0xFFFFFF14).
    const int formatByte = chipFormat & 0xFF;
    impl.status.detail = std::format("chipAudioFormat={}{}", chipFormat,
        formatByte == kChipFormatNonPcm ? " (non-PCM/Dolby)" : " (PCM)");
    impl.status.state = AverMediaSwitchState::Switched;
    impl.lastAppliedPath = devicePath;
    if(monitor)startMonitoring();
    return true;
}

void AverMediaAudioSwitch::stop() noexcept {
    if (!p_) return;
    auto& impl = *p_;
    try {
        if (impl.checking && impl.avtModule != nullptr) {
            RawCall stopChecking{impl.exports.stopChecking, impl.opener()};
            std::string error;
            callVendor(stopChecking, error);
            impl.checking = false;
        }
        if (impl.sdkInitialized) {
            RawCall close{impl.exports.closePort, impl.sdk()};
            std::string error;
            callVendor(close, error);
            Sleep(1000);  // matches the component's own unload sequence
            RawCall uninitialize{impl.exports.uninitialize, impl.sdk()};
            callVendor(uninitialize, error);
            impl.sdkInitialized = false;
            impl.status.sdkReady = false;
        }
        if (impl.openerConstructed) {
            RawCall dtor{impl.exports.openerDtor, impl.opener()};
            std::string error;
            callVendor(dtor, error);
            impl.openerConstructed = false;
        }
        if (impl.sdkConstructed) {
            RawCall dtor{impl.exports.vendorDtor, impl.sdk()};
            std::string error;
            callVendor(dtor, error);
            impl.sdkConstructed = false;
        }
    } catch (...) {
        // Teardown must never throw; the vendors' worker threads are already
        // stopped by the calls above.
    }
    impl.lastAppliedPath.clear();
    impl.status.passthroughKnown=false;impl.status.passthroughEnabled=false;
    impl.status.chipAudioFormat=-1;impl.status.nonPcmActive=false;
    // The modules stay loaded on purpose: their monitor thread and the Qt
    // runtime are not safe to unload while other threads may still touch them.
}

bool AverMediaAudioSwitch::refreshChipState() {
    if (!p_) return false;
    auto& impl = *p_;
    impl.status.passthroughKnown=false;
    impl.status.passthroughEnabled=false;
    impl.status.chipAudioFormat=-1;
    if (!impl.sdkInitialized || impl.exports.getAudioFormat == nullptr) return false;
    std::string error;
    int chipFormat = -1;
    RawCall formatCall{impl.exports.getAudioFormat, impl.sdk(), &chipFormat};
    const bool formatOk = callVendor(formatCall, error) && formatCall.result == 0;
    if (formatOk) impl.status.chipAudioFormat = chipFormat;
    else {
        impl.status.detail=std::format("getAudioFormat failed result={} {}",int32_t(uint32_t(formatCall.result)),error);
        log::warn("capture-audio-vendor",impl.status.detail);
    }
    if (impl.exports.getNonPcmOnOff != nullptr) {
        unsigned onOff = 0;
        error.clear();
        RawCall onOffCall{impl.exports.getNonPcmOnOff, impl.sdk(), &onOff};
        if (callVendor(onOffCall, error) && onOffCall.result == 0) {
            impl.status.passthroughEnabled = onOff != 0;
            impl.status.passthroughKnown=true;
        }else{
            impl.status.detail=std::format("getNonPcmOnOff failed result={} {}",int32_t(uint32_t(onOffCall.result)),error);
            log::warn("capture-audio-vendor",impl.status.detail);
        }
    }
    return formatOk;
}

bool AverMediaAudioSwitch::sourceIsBitstream() {
    return refreshChipState() && (p_->status.chipAudioFormat & 0xFF) == kChipFormatNonPcm;
}

bool AverMediaAudioSwitch::passthroughEnabled() const {
    return p_ && p_->status.passthroughEnabled;
}

bool AverMediaAudioSwitch::setPassthrough(bool enable) {
    if (!p_ || !enable) return false;
    auto& impl = *p_;
    if (!refreshChipState() || !impl.status.passthroughKnown) {
        log::warn("capture-audio-vendor","passthrough query unavailable/failed; refusing a blind forced enable");return false;
    }
    if(impl.status.passthroughEnabled)return true;
    if(impl.exports.setNonPcmOnOff == nullptr){log::warn("capture-audio-vendor","setNonPcmOnOff export unavailable; leaving current mode unchanged");return false;}
    // The raw invoker passes void* arguments; an int parameter travels in the
    // low 32 bits of the register on x64, which is how the component reads it.
    RawCall call{impl.exports.setNonPcmOnOff, impl.sdk(),
        reinterpret_cast<void*>(static_cast<intptr_t>(enable ? 1 : 0))};
    std::string error;
    const bool ok = callVendor(call, error) && call.result == 0;
    log::info("capture-audio-vendor", std::format("setNonPcmOnOff({}) result={}{}",
        enable ? 1 : 0, int32_t(uint32_t(call.result)), ok ? "" : (error.empty() ? " failed" : " failed: " + error)));
    // A successful write is not proof that the chip actually armed.
    const bool readback=ok&&refreshChipState()&&impl.status.passthroughKnown&&impl.status.passthroughEnabled;
    if(ok&&!readback)log::warn("capture-audio-vendor","passthrough enable did not read back as armed");
    return readback;
}

void AverMediaAudioSwitch::alignPassthroughOnce() {
    if (!p_ || !p_->sdkInitialized) return;
    const bool bitstream = sourceIsBitstream();
    const bool armed = passthroughEnabled();
    log::info("capture-audio-vendor", std::format(
        "HDMI source audio format: chipFormat={} ({}); passthrough known={} armed={}",
        p_->status.chipAudioFormat, bitstream ? "bitstream" : "PCM/unknown",p_->status.passthroughKnown, armed ? 1 : 0));
    // Forcing this call when the component already reports armed left the KS
    // pin silent (2026-10-01). Only top it up when it is actually off, and
    // never disarm: the monitor thread treats a disarm as something to undo.
    if (p_->status.passthroughKnown&&!armed) setPassthrough(true);
    else if(!p_->status.passthroughKnown)log::warn("capture-audio-vendor","passthrough flag unknown; preserving existing mode");
}

void AverMediaAudioSwitch::startMonitoring() {
    auto& impl = *p_;
    if (!impl.sdkInitialized || impl.checking || impl.avtModule == nullptr) return;
    RawCall start{impl.exports.startChecking, impl.opener()};
    std::string error;
    if (!callVendor(start, error)) {
        impl.status.detail = "StartChecking failed: " + error;
        return;
    }
    impl.checking = true;
}

bool AverMediaAudioSwitch::active() const {
    return p_ && p_->checking;
}

const AverMediaSwitchStatus& AverMediaAudioSwitch::status() const {
    return p_->status;
}

} // namespace veyra::source
