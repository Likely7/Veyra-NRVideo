#include "ProfileInstance.h"

#include "veyra/Log.h"
#include <QCoreApplication>
#include <format>
#include <utility>

namespace veyra::ui {

ProfileInstance::~ProfileInstance() {
    if (owns_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
}

bool ProfileInstance::acquire(const std::filesystem::path& profile, const std::filesystem::path& logs,
                              const std::wstring& target, const instance::Notify& notify) {
    profile_ = instance::Profile(profile, logs);
    const auto& key = profile_.key();
    if (key.empty()) {
        lockError_ = ERROR_INVALID_NAME;
    } else {
        mutex_ = CreateMutexW(nullptr, FALSE, key.c_str());
        if (!mutex_) lockError_ = GetLastError();
    }

    auto deadline = GetTickCount64() + 3000;
    do {
        if (mutex_ && !owns_) {
            const auto result = WaitForSingleObject(mutex_, 0);
            owns_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
            if (result == WAIT_FAILED) {
                lockError_ = GetLastError();
                CloseHandle(mutex_);
                mutex_ = nullptr;
            }
        }
        if (owns_ && !profile_.ownerAlive()) {
            if (!profile_.rememberOwner(false))
                log::warn("app-instance", "could not record the profile owner PID");
            return true;
        }
        const auto window = instance::findWindow(key);
        // The mutex API may be unavailable in a sandbox. Known live owners
        // still get the normal handoff, and capture has its own device lock.
        if (!mutex_ && !window && !profile_.ownerAlive()) return true;
        if (window) {
            std::wstring self(32768, L'\0');
            const auto size = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
            self.resize(size < self.size() ? size : 0);
            const auto outcome = profile_.handoff(window, target, self, notify);
            if (outcome == instance::Handoff::Done) return false;
            if (outcome == instance::Handoff::Released) deadline = GetTickCount64() + 3000;
        }
        Sleep(60);
    } while (GetTickCount64() < deadline);
    if (!profile_.reportStalledOwner(notify)) notify(instance::Notice::StartingOrClosing, 0, {});
    return false;
}

void ProfileInstance::listen(HWND window, Launch launch) {
    if (!owns_) return;
    launch_ = std::move(launch);
    QCoreApplication::instance()->installNativeEventFilter(this);
    window_ = window;
    if (!SetPropW(window_, profile_.key().c_str(), reinterpret_cast<HANDLE>(1)))
        log::warn("app-instance", std::format("window registration failed error={}", GetLastError()));
    if (!profile_.rememberOwner(true))
        log::warn("app-instance", "could not record the profile owner window status");
}

void ProfileInstance::stopListening() {
    if (!window_) return;
    RemovePropW(window_, profile_.key().c_str());
    QCoreApplication::instance()->removeNativeEventFilter(this);
    window_ = nullptr;
    launch_ = {};
}

bool ProfileInstance::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    if (type != "windows_generic_MSG") return false;
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->hwnd != window_ || msg->message != WM_COPYDATA) return false;
    const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(msg->lParam);
    if (!data || data->dwData != instance::launchMessage || data->cbData > instance::maxTargetChars * sizeof(wchar_t)
        || data->cbData % sizeof(wchar_t) || (data->cbData && !data->lpData)) return false;
    const auto target = data->cbData
        ? QString::fromWCharArray(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t))
        : QString{};
    if (target.contains(QChar(u'\0'))) return false;
    launch_(target);
    *result = instance::launchMessage;
    return true;
}

} // namespace veyra::ui
