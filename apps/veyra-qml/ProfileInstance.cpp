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

bool ProfileInstance::acquire(const std::filesystem::path& profile, const std::wstring& target,
                              const instance::Notify& notify) {
    name_ = instance::profileKey(profile);
    if (name_.empty()) {
        notify(instance::Notice::LockFailed, ERROR_INVALID_NAME);
        return false;
    }
    mutex_ = CreateMutexW(nullptr, FALSE, name_.c_str());
    if (!mutex_) {
        notify(instance::Notice::LockFailed, GetLastError());
        return false;
    }

    const auto deadline = GetTickCount64() + 3000;
    do {
        const auto result = WaitForSingleObject(mutex_, 0);
        owns_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (owns_) return true;
        if (result == WAIT_FAILED) {
            notify(instance::Notice::LockFailed, GetLastError());
            return false;
        }
        if (const auto window = instance::findWindow(name_)) {
            std::wstring self(32768, L'\0');
            const auto size = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
            self.resize(size < self.size() ? size : 0);
            const auto outcome = instance::handoff(window, name_, target, self, notify);
            if (outcome == instance::Handoff::Done) return false;
            if (outcome == instance::Handoff::Released) {
                const auto acquired = WaitForSingleObject(mutex_, 500);
                owns_ = acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED;
                if (owns_) return true;
                break;
            }
        }
        Sleep(60);
    } while (GetTickCount64() < deadline);
    notify(instance::Notice::StartingOrClosing, 0);
    return false;
}

void ProfileInstance::listen(HWND window, Launch launch) {
    if (!owns_) return;
    launch_ = std::move(launch);
    QCoreApplication::instance()->installNativeEventFilter(this);
    window_ = window;
    if (!SetPropW(window_, name_.c_str(), reinterpret_cast<HANDLE>(1)))
        log::warn("app-instance", std::format("window registration failed error={}", GetLastError()));
}

void ProfileInstance::stopListening() {
    if (!window_) return;
    RemovePropW(window_, name_.c_str());
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
