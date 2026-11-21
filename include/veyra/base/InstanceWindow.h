#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <windows.h>

namespace veyra::instance {

inline constexpr ULONG_PTR launchMessage = 0x56595201;
inline constexpr size_t maxTargetChars = 32767;

enum class Notice { LockFailed, StartingOrClosing, AlreadyOpen, Hung, ConfirmEnd };
enum class Handoff { NoWindow, Done, Released };
using Notify = std::function<bool(Notice, DWORD)>;

std::wstring profileKey(const std::filesystem::path& profile);
std::wstring launchTarget(std::span<const std::wstring> arguments);
HWND findWindow(const std::wstring& key);

// The target HWND advertises this profile. Ending an unresponsive instance
// additionally requires its exact executable, its still-live process handle,
// a second check of the window, and an explicit yes from the user.
Handoff handoff(HWND window, const std::wstring& key, const std::wstring& target,
                const std::filesystem::path& executable, const Notify& notify);

} // namespace veyra::instance
