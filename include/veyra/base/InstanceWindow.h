#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <windows.h>

namespace veyra::instance {

inline constexpr ULONG_PTR launchMessage = 0x56595201;
inline constexpr size_t maxTargetChars = 32767;

enum class Notice { StartingOrClosing, AlreadyOpen, Hung, ConfirmEnd, EndPending, EndFailed };
enum class Handoff { NoWindow, Done, Released };
enum class ProcessExit { Exited, Pending, Failed };
using Notify = std::function<bool(Notice, DWORD, const std::filesystem::path& dump)>;

struct OwnerStamp {
    DWORD pid = 0;
    uint64_t created = 0;
    bool windowReady = false;
    std::filesystem::path logs;
};

std::wstring profileKey(const std::filesystem::path& profile);
std::wstring launchTarget(std::span<const std::wstring> arguments);
HWND findWindow(const std::wstring& key);
std::filesystem::path newestHangDump(const std::filesystem::path& directory, DWORD pid, uint64_t created);
bool forwardLaunch(HWND window, DWORD pid, const std::wstring& target);
bool timedOut(HWND window, DWORD timeout = 1200);
std::optional<OwnerStamp> readOwnerStamp(const std::filesystem::path& directory);
void clearOwnerStamp(const std::filesystem::path& directory);

// Lightweight launcher-side handoff: window message only, no OpenProcess,
// Toolhelp32 snapshot, bcrypt.dll, or cross-process TerminateProcess.
Handoff handoffWindowOnly(const std::filesystem::path& directory, const std::filesystem::path& logDirectory,
                          const std::wstring& key, HWND window, const std::wstring& target, const Notify& notify);
bool reportStalledOwnerFromStamp(const std::filesystem::path& directory, const std::filesystem::path& logDirectory,
                                 const Notify& notify);

// The process handle, rather than its window or mutex, confirms exit.
ProcessExit waitForExit(HANDLE process, bool endRequested);

class Profile {
public:
    Profile() = default;
    Profile(std::filesystem::path directory, std::filesystem::path logDirectory);

    const std::wstring& key() const { return key_; }
    bool rememberOwner(bool windowReady) const;
    void clearOwner() const;
    bool ownerAlive() const;
    bool reportStalledOwner(const Notify& notify) const;

    // Ending an unresponsive instance additionally requires the exact
    // executable, its live process handle, and an explicit yes from the user.
    Handoff handoff(HWND window, const std::wstring& target, const std::filesystem::path& executable,
                    const Notify& notify) const;

private:
    std::filesystem::path directory_;
    std::filesystem::path logDirectory_;
    std::wstring key_;
};

} // namespace veyra::instance
