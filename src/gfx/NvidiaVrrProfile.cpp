#include "veyra/gfx/NvidiaVrrProfile.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace veyra::gfx {
namespace {
// Minimal ABI declarations checked against NVIDIA/nvapi (MIT), nvapi.h,
// nvapi_interface.h and NvApiDriverSettings.h. No vendor DLL/SDK is bundled.
using Handle = void*;
using U = uint32_t;
constexpr int settingNotFound = -160, executableNotFound = -166;
constexpr U vsyncModeId = 0x00A879CF; // VSYNCMODE_ID from NVIDIA NvApiDriverSettings.h (MIT)

std::string vsyncDescription(const std::optional<VrrProfileValue>& value) {
    if (!value) return "unset";
    // EValues_VSYNCMODE, NvApiDriverSettings.h. Preserve unknown values in
    // the diagnostic so newer driver modes are still visible.
    const char* mode = "unknown";
    switch (value->value) {
    case 0x60925292: mode = "application-controlled"; break;
    case 0x08416747: mode = "force-off"; break;
    case 0x47814940: mode = "force-on"; break;
    case 0x32610244: mode = "interval-2"; break;
    case 0x71271021: mode = "interval-3"; break;
    case 0x13245256: mode = "interval-4"; break;
    case 0x18888888: mode = "virtual"; break;
    }
    return std::format("{}(0x{:08X})", mode, value->value);
}
#pragma pack(push, 4)
union Value {
    U dword;
    struct { U length; unsigned char bytes[4096]; } binary;
    wchar_t text[2048];
    uint64_t qword;
};
struct Setting {
    U version; wchar_t name[2048]; U id, type, location, predefined, predefinedValid;
    Value defaultValue, current;
};
struct Application { U version, predefined; wchar_t name[2048], friendly[2048], launcher[2048]; };
struct Profile { U version; wchar_t name[2048]; U gpuSupport, predefined, applications, settings; };
#pragma pack(pop)
static_assert(sizeof(Setting) == 12320 && sizeof(Application) == 12296 && sizeof(Profile) == 4116);
static_assert(sizeof(wchar_t) == 2);
template<class T> T versioned() { T v{}; v.version = U(sizeof(T)) | (1u << 16); return v; }
struct Failure { VrrProfileError error; int status = 0; };
void checked(int code) { if (code) throw Failure{VrrProfileError::Driver, code}; }
template<size_t N> void copy(wchar_t (&to)[N], const std::wstring& from) {
    if (from.size() >= N) throw Failure{VrrProfileError::UnsafeProfile};
    std::copy(from.begin(), from.end(), to); to[from.size()] = 0;
}
std::wstring normalized(std::wstring name) {
    std::replace(name.begin(), name.end(), L'/', L'\\');
    if (name.size() < 3 || !((name[1] == L':' && name[2] == L'\\')
        || (name[0] == L'\\' && name[1] == L'\\'))) return {};
    const DWORD length = GetFullPathNameW(name.c_str(), 0, nullptr, nullptr);
    if (!length || length >= 2048) return {};
    std::wstring absolute(length, 0);
    const DWORD written = GetFullPathNameW(name.c_str(), length, absolute.data(), nullptr);
    if (!written || written >= length) return {};
    absolute.resize(written);
    std::transform(absolute.begin(), absolute.end(), absolute.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    return absolute;
}
struct Api {
    using Query = void*(__cdecl*)(U);
    Query query = nullptr;
    int status = -2;
    Api() {
        wchar_t system[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(system, MAX_PATH);
        if (!length || length >= MAX_PATH) return;
        const auto path = std::wstring(system) + L"\\nvapi64.dll";
        auto module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return;
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(module), &pinned);
        query = reinterpret_cast<Query>(GetProcAddress(module, "nvapi_QueryInterface"));
        if (!query) return;
        auto init = reinterpret_cast<int(__cdecl*)()>(query(0x0150E828));
        status = init ? init() : -3;
        // Like NvidiaVrr.h, keep NVAPI initialized and pinned for process life.
        // Unloading it here could invalidate the presenter's active interfaces.
    }
    template<class F> F function(U id) const {
        auto pointer = query ? query(id) : nullptr;
        if (!pointer) throw Failure{VrrProfileError::Unavailable, -3};
        return reinterpret_cast<F>(pointer);
    }
};
const Api& api() { static const auto* const instance = new Api; return *instance; }
// All our check/repair/restore calls serialize, without touching the render loop.
// Match the pinned driver's process lifetime. A worker may still be finishing
// during application shutdown; do not destroy its mutex in CRT teardown.
std::mutex& operations() { static auto* const mutex = new std::mutex; return *mutex; }
class Session {
    const Api& api_ = api();
public:
    Handle session = nullptr, profile = nullptr, global = nullptr, base = nullptr;
    Session() {
        if (api_.status) throw Failure{VrrProfileError::Unavailable, api_.status};
        checked(call<Handle*>(0x0694D52E, &session));
        try {
            checked(call<Handle>(0x375DBD6B, session));
            checked(call<Handle, Handle*>(0x617BFF9F, session, &global));
            checked(call<Handle, Handle*>(0xDA8466A0, session, &base));
        } catch (...) { destroy(); throw; }
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { destroy(); }
    void destroy() noexcept {
        if (!session) return;
        try { call<Handle>(0xDAD9CFF8, session); } catch (...) {}
        session = nullptr;
    }
    template<class... A> int call(U id, A... args) const {
        return api_.function<int(__cdecl*)(A...)>(id)(args...);
    }
    std::optional<VrrProfileValue> readDword(Handle target, U id) const {
        auto setting = versioned<Setting>();
        const int code = call<Handle, Handle, U, Setting*>(0x73BF8338, session, target, id, &setting);
        if (code == settingNotFound) return {};
        checked(code);
        if (setting.type != 0) throw Failure{VrrProfileError::UnsafeProfile};
        return VrrProfileValue{setting.current.dword, setting.location == 0};
    }
    VrrProfileValue value(Handle target, U id) const {
        const auto entry = readDword(target, id).value_or(VrrProfileValue{});
        if (entry.value > 2) throw Failure{VrrProfileError::UnsafeProfile};
        return entry;
    }
    bool findApplication(const std::wstring& executable, Application& app) {
        wchar_t name[2048]{}; copy(name, executable);
        int code = call<Handle, wchar_t*, Handle*, Application*>(0xEEE566B2, session, name, &profile, &app);
        if (code == executableNotFound) {
            // Check a basename association too; never overwrite a shared match
            // or create a conflicting association for a different installation.
            const auto slash = executable.find_last_of(L'\\');
            copy(name, executable.substr(slash + 1));
            code = call<Handle, wchar_t*, Handle*, Application*>(0xEEE566B2, session, name, &profile, &app);
        }
        if (code == executableNotFound) { profile = nullptr; return false; }
        checked(code);
        return true;
    }
    VrrProfileSnapshot read(const std::wstring& executable) {
        VrrProfileSnapshot state;
        state.executable = normalized(executable);
        if (state.executable.empty()) throw Failure{VrrProfileError::UnsafeProfile};
        state.profileName = L"Veyra VRR - " + state.executable;
        if (state.profileName.size() >= 2048) throw Failure{VrrProfileError::UnsafeProfile};
        for (size_t k = 0; k < state.global.size(); ++k) state.global[k] = value(global, vrrProfileSettingIds[k]);
        auto app = versioned<Application>();
        if (!findApplication(state.executable, app)) { state.isolated = true; return state; }
        auto info = versioned<Profile>();
        checked(call<Handle, Handle, Profile*>(0x61CD6FD6, session, profile, &info));
        state.exists = true; state.profileName = info.name; state.settingCount = info.settings;
        state.isolated = profile != global && profile != base && !info.predefined && info.applications == 1
            && normalized(app.name) == state.executable;
        for (size_t k = 0; k < state.settings.size(); ++k) state.settings[k] = value(profile, vrrProfileSettingIds[k]);
        return state;
    }
    void apply(const VrrProfileSnapshot& state, const VrrProfilePlan& plan) {
        if (!plan.allowed) throw Failure{VrrProfileError::UnsafeProfile};
        if (plan.createProfile) {
            auto info = versioned<Profile>(); copy(info.name, state.profileName);
            checked(call<Handle, Profile*, Handle*>(0xCC176068, session, &info, &profile));
            auto app = versioned<Application>(); copy(app.name, state.executable); copy(app.friendly, L"Veyra");
            checked(call<Handle, Handle, Application*>(0x4347A9DE, session, profile, &app));
        }
        if (plan.removeProfile) checked(call<Handle, Handle>(0x17093206, session, profile));
        else for (size_t k = 0; k < plan.edits.size(); ++k) {
            if (plan.edits[k] == VrrProfileEdit::Remove) {
                const auto code = call<Handle, Handle, U>(0xE4A26362, session, profile, vrrProfileSettingIds[k]);
                if (code != settingNotFound) checked(code);
            } else if (plan.edits[k] == VrrProfileEdit::Set) {
                auto setting = versioned<Setting>(); setting.id = vrrProfileSettingIds[k];
                setting.current.dword = plan.values[k];
                checked(call<Handle, Handle, Setting*>(0x577DD202, session, profile, &setting));
            }
        }
        // Only this one profile and these two IDs have been edited in memory.
        // Any failure above destroys the session without saving.
        if (plan.changes()) checked(call<Handle>(0xFCBC7E14, session));
    }
};
VrrProfileResult failure(Failure error) { VrrProfileResult r; r.error = error.error; r.driverStatus = error.status; return r; }
VrrProfileResult write(const VrrProfileSnapshot& expected, const VrrProfileSnapshot* original) {
    try {
        VrrProfilePlan plan;
        {
            Session session;
            const auto current = session.read(expected.executable);
            if (current != expected) return failure({VrrProfileError::Changed});
            plan = original ? planVrrProfileRestore(*original, current) : planVrrProfileRepair(current);
            if (!plan.allowed) return failure({VrrProfileError::UnsafeProfile});
            session.apply(current, plan);
        }
        Session verification;
        VrrProfileResult result; result.snapshot = verification.read(expected.executable); result.changed = plan.changes();
        if (result.snapshot.global != expected.global) result.error = VrrProfileError::Verification;
        if (!original) {
            if (!result.snapshot.configured()) result.error = VrrProfileError::Verification;
        } else {
            const auto remaining = planVrrProfileRestore(*original, result.snapshot);
            if (!remaining.allowed || remaining.changes()) result.error = VrrProfileError::Verification;
        }
        return result;
    } catch (Failure e) { return failure(e); }
    catch (...) { return failure({VrrProfileError::Driver}); }
}
} // namespace
VrrProfileResult inspectVrrProfile(const std::wstring& executable) {
    const std::lock_guard lock(operations());
    try { Session session; VrrProfileResult result; result.snapshot = session.read(executable); return result; }
    catch (Failure e) { return failure(e); }
    catch (...) { return failure({VrrProfileError::Driver}); }
}
std::string nvidiaVsyncProfileDiagnostic(const std::wstring& executable) {
    const std::lock_guard lock(operations());
    try {
        const auto path = normalized(executable);
        if (path.empty()) return "global=unavailable app=unavailable reason=invalid-executable-path";
        Session session;
        const auto global = session.readDword(session.global, vsyncModeId);
        auto application = versioned<Application>();
        const bool present = session.findApplication(path, application);
        const auto app = present ? session.readDword(session.profile, vsyncModeId) : std::nullopt;
        return std::format("global={} app={} appProfile={} appLocal={} effective={}",
            vsyncDescription(global), present ? vsyncDescription(app) : "no-profile",
            present, app && app->local, vsyncDescription(app ? app : global));
    } catch (Failure error) {
        return std::format("global=unavailable app=unavailable nvapiStatus={} error={}",
                           error.status, int(error.error));
    } catch (...) {
        return "global=unavailable app=unavailable reason=read-failed";
    }
}
VrrProfileResult repairVrrProfile(const VrrProfileSnapshot& expected) {
    const std::lock_guard lock(operations()); return write(expected, nullptr);
}
VrrProfileResult restoreVrrProfile(const VrrProfileSnapshot& original, const VrrProfileSnapshot& expected) {
    const std::lock_guard lock(operations()); return write(expected, &original);
}
} // namespace veyra::gfx
