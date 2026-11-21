#pragma once

#include "veyra/base/InstanceWindow.h"
#include <QAbstractNativeEventFilter>
#include <QString>
#include <filesystem>
#include <functional>
#include <windows.h>

namespace veyra::ui {

class ProfileInstance final : public QAbstractNativeEventFilter {
public:
    using Launch = std::function<void(const QString& target)>;

    ProfileInstance() = default;
    ~ProfileInstance() override;
    ProfileInstance(const ProfileInstance&) = delete;
    ProfileInstance& operator=(const ProfileInstance&) = delete;

    // Runs before the logger, preference migration and capture engine. A false
    // result means the request was handed off or an existing owner is still alive.
    bool acquire(const std::filesystem::path& profile, const std::filesystem::path& logs,
                 const std::wstring& target, const instance::Notify& notify);
    DWORD lockError() const { return lockError_; }
    void listen(HWND window, Launch launch);
    void stopListening();

private:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

    HANDLE mutex_ = nullptr;
    DWORD lockError_ = ERROR_SUCCESS;
    bool owns_ = false;
    instance::Profile profile_;
    HWND window_ = nullptr;
    Launch launch_;
};

} // namespace veyra::ui
