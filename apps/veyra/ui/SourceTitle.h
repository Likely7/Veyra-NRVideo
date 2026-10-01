#pragma once

#include <filesystem>
#include <string>

namespace veyra::ui {

// Physical capture sources are opened through opaque connection strings
// ("capture:<index>..." and "capture2:<hex device path>:..."). They are not
// file names. Using them as the window title leaked a multi-hundred character
// hex blob to every window list (windows list of third-party broadcast tools,
// taskbar, Alt+Tab), which made the live window impossible to recognise and
// matched the reported "cannot find Veyra while it is capturing" symptom.
inline bool isCaptureCardSource(const std::wstring& source) {
    return source.starts_with(L"capture:") || source.starts_with(L"capture2:");
}

inline bool isRemotePlaySource(const std::wstring& source) {
    return source.starts_with(L"remoteplay:");
}

inline std::wstring windowTitleForSource(const std::wstring& source) {
    if (source.empty()) return L"Veyra — Local Experimental";
    if (isCaptureCardSource(source)) return L"Veyra — Capture Card · LIVE";
    if (isRemotePlaySource(source)) return L"Veyra — PS5 Remote Play";
    if (source.starts_with(L"screen:")) return L"Veyra — Screen Capture · LIVE";
    return L"Veyra — " + std::filesystem::path(source).filename().wstring();
}

} // namespace veyra::ui
