#pragma once

#include "veyra/base/PortableLayout.h"
#include <windows.h>

namespace veyra::portable {

inline bool plainPath(const std::filesystem::path& path, bool directory) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        && bool(attrs & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

// The launcher holds the installation lock while this file is being replaced.
inline bool writeSmallFile(const std::filesystem::path& path, std::string_view data) {
    auto temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".new";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool saved = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)
        && written == data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (saved && MoveFileExW(temporary.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temporary.c_str());
    return false;
}

inline bool setInstalledVersions(const std::filesystem::path& root, const InstalledVersions& state) {
    return writeSmallFile(root / versionState, state.current + "\n" + state.previous + "\n");
}

} // namespace veyra::portable
