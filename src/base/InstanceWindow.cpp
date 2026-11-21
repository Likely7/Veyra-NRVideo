#include "veyra/base/InstanceWindow.h"

#include "veyra/FileIdentity.h"
#include <memory>

namespace veyra::instance {
namespace {

using ProcessHandle = std::unique_ptr<void, decltype(&CloseHandle)>;

bool forwardLaunch(HWND window, DWORD pid, const std::wstring& target) {
    if (target.size() > maxTargetChars) return false;
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT data{launchMessage, DWORD(target.size() * sizeof(wchar_t)), const_cast<wchar_t*>(target.data())};
    DWORD_PTR reply = 0;
    return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                               3000, &reply) && reply == launchMessage;
}

bool timedOut(HWND window) {
    // A protocol mismatch, destroyed window, or elevation barrier must never
    // be treated as an unresponsive message pump.
    DWORD_PTR reply = 0;
    SetLastError(ERROR_SUCCESS);
    return !SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_BLOCK | SMTO_ERRORONEXIT,
                                1200, &reply) && GetLastError() == ERROR_TIMEOUT;
}

bool runningExecutable(HANDLE process, const std::filesystem::path& expected) {
    if (expected.empty()) return false;
    std::wstring image(32768, L'\0');
    DWORD size = DWORD(image.size());
    return QueryFullProcessImageNameW(process, 0, image.data(), &size)
        && CompareStringOrdinal(expected.c_str(), int(expected.native().size()),
                                image.data(), int(size), TRUE) == CSTR_EQUAL;
}

} // namespace

std::wstring profileKey(const std::filesystem::path& profile) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(profile, ec);
    if (ec) return {};
    auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    auto normalized = (ec ? absolute.lexically_normal() : canonical).make_preferred();
    if (normalized.filename().empty() && normalized != normalized.root_path()) normalized = normalized.parent_path();
    auto path = normalized.wstring();
    CharLowerBuffW(path.data(), DWORD(path.size()));
    const auto hash = sha256Hex(reinterpret_cast<const uint8_t*>(path.data()), path.size() * sizeof(wchar_t));
    return hash.empty() ? std::wstring{} : L"Local\\Veyra.Qml.Profile." + std::wstring(hash.begin(), hash.end());
}

std::wstring launchTarget(std::span<const std::wstring> arguments) {
    std::wstring target;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& option = arguments[i];
        if (option == L"--managed-portable") continue;
        if (option == L"--data-dir" || option == L"--import-1.4.4-data") {
            ++i;
            continue;
        }
        // Other switches belong to a fresh launch or an isolated test run.
        if (option.starts_with(L'-')) return {};
        target = option;
    }
    const auto colon = target.find(L':');
    // Leave URL schemes for the player's existing URL validation. A colon at
    // character one belongs to a Windows drive.
    if (target.empty() || (colon != std::wstring::npos && colon > 1 && target.find_first_of(L"\\/") > colon))
        return target;
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(target), ec);
    return ec ? std::wstring{} : absolute.lexically_normal().wstring();
}

HWND findWindow(const std::wstring& key) {
    if (key.empty()) return nullptr;
    struct Search {
        const std::wstring& key;
        HWND window = nullptr;
    } search{key};
    EnumWindows([](HWND window, LPARAM arg) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(arg);
        if (!GetPropW(window, search.key.c_str())) return TRUE;
        search.window = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

Handoff handoff(HWND window, const std::wstring& key, const std::wstring& target,
                const std::filesystem::path& executable, const Notify& notify) {
    DWORD pid = 0;
    if (!window || !GetWindowThreadProcessId(window, &pid) || !GetPropW(window, key.c_str()))
        return Handoff::NoWindow;
    if (forwardLaunch(window, pid, target)) return Handoff::Done;
    const auto sameWindow = [&] {
        DWORD current = 0;
        GetWindowThreadProcessId(window, &current);
        return current == pid && GetPropW(window, key.c_str());
    };
    if (!sameWindow()) return Handoff::NoWindow;
    if (!timedOut(window)) {
        if (!sameWindow()) return Handoff::NoWindow;
        notify(Notice::AlreadyOpen, 0);
        return Handoff::Done;
    }
    ProcessHandle owner(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                    FALSE, pid), CloseHandle);
    if (!owner || !runningExecutable(owner.get(), executable)) {
        notify(Notice::Hung, pid);
        return Handoff::Done;
    }
    if (!notify(Notice::ConfirmEnd, pid)) return Handoff::Done;
    if (WaitForSingleObject(owner.get(), 0) == WAIT_OBJECT_0) return Handoff::Released;
    if (sameWindow() && timedOut(window) && sameWindow()
        && TerminateProcess(owner.get(), 0) && WaitForSingleObject(owner.get(), 3000) == WAIT_OBJECT_0)
        return Handoff::Released;
    if (!sameWindow()) return Handoff::NoWindow;
    if (forwardLaunch(window, pid, target)) return Handoff::Done;
    notify(Notice::AlreadyOpen, 0);
    return Handoff::Done;
}

} // namespace veyra::instance
