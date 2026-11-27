#pragma once

#include "veyra/base/PortableLayout.h"
#include <windows.h>

namespace veyra::ui::login_startup {

inline constexpr wchar_t runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t valueName[] = L"Veyra";
inline constexpr size_t maxCommandLength = 260; // Windows Run key contract

inline std::filesystem::path executableForPlayer(const std::filesystem::path& player) {
    const auto root = portable::rootFromVersionDirectory(player.parent_path());
    return root.empty() ? player : root / portable::launcherName;
}

inline std::wstring commandFor(const std::filesystem::path& executable) {
    const auto path = executable.lexically_normal().make_preferred();
    if (!path.is_absolute() || path.native().find(L'"') != std::wstring::npos) return {};
    return L"\"" + path.native() + L"\"";
}

// Success means the exact command which this copy of Veyra would register.
// A value pointing elsewhere, of another type, or too large is occupied.
inline LSTATUS entryStatus(HKEY registry, const std::wstring& command) {
    if (command.empty()) return ERROR_INVALID_NAME;
    if (command.size() > maxCommandLength) return ERROR_FILENAME_EXCED_RANGE;
    wchar_t actual[maxCommandLength + 1]{};
    DWORD bytes = sizeof(actual);
    const LSTATUS status = RegGetValueW(registry, runKey, valueName, RRF_RT_REG_SZ | RRF_NOEXPAND,
                                        nullptr, actual, &bytes);
    if (status == ERROR_MORE_DATA || status == ERROR_UNSUPPORTED_TYPE) return ERROR_ALREADY_EXISTS;
    if (status != ERROR_SUCCESS) return status;
    return bytes == (command.size() + 1) * sizeof(wchar_t) &&
        CompareStringOrdinal(actual, static_cast<int>(command.size()), command.c_str(),
                             static_cast<int>(command.size()), TRUE) == CSTR_EQUAL
        ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS;
}

inline LSTATUS setEnabled(HKEY registry, const std::filesystem::path& executable, bool enabled) {
    const auto command = commandFor(executable);
    // Serialize toggles from two copies in the same Windows session. A busy
    // toggle can be retried without waiting on the GUI thread.
    const HANDLE lock = CreateMutexW(nullptr, FALSE, L"Local\\Veyra.LoginStartup");
    if (!lock) return GetLastError();
    const DWORD wait = WaitForSingleObject(lock, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        const auto status = wait == WAIT_TIMEOUT ? ERROR_BUSY : GetLastError();
        CloseHandle(lock);
        return status;
    }

    LSTATUS status = entryStatus(registry, command);
    if (enabled && status == ERROR_FILE_NOT_FOUND) {
        const auto attrs = GetFileAttributesW(executable.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) status = GetLastError();
        else if (attrs & FILE_ATTRIBUTE_DIRECTORY) status = ERROR_DIRECTORY;
        else status = RegSetKeyValueW(registry, runKey, valueName, REG_SZ, command.c_str(),
                                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else if (!enabled) {
        if (status == ERROR_SUCCESS) status = RegDeleteKeyValueW(registry, runKey, valueName);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_ALREADY_EXISTS) status = ERROR_SUCCESS;
    }

    ReleaseMutex(lock);
    CloseHandle(lock);
    return status;
}

} // namespace veyra::ui::login_startup
