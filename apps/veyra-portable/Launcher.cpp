#include "veyra/base/PortableFiles.h"
#include "veyra/base/LauncherDiagnostics.h"
#include "veyra/base/InstanceWindow.h"

#include <shellapi.h>
#include <format>
#include <vector>

namespace {
using namespace veyra::portable;
namespace instance = veyra::instance;
namespace fs = std::filesystem;

struct Handle {
    HANDLE value;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::wstring quote(std::wstring_view argument) {
    // CommandLineToArgvW consumes doubled backslashes before a quote.
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        result.push_back(character);
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

enum class FailureKind { Missing, Reparse, WrongType, Inspect, Start, Wait, ExitCode, Loader, Lock };

struct Failure {
    FailureKind kind;
    DWORD code;
    fs::path path;
};
struct RunResult {
    DWORD exitCode = 0;
    std::optional<Failure> failure;
};
struct FailureText {
    const wchar_t* category;
    const wchar_t* chinese;
    const wchar_t* english;
};

FailureText failureText(FailureKind kind) {
    using namespace launcherText;
    switch (kind) {
    case FailureKind::Missing: return {L"missing", missing, L"Main program file not found. Repair Veyra with the official installer or check Protection history in Windows Security."};
    case FailureKind::Reparse: return {L"reparse", redirected, L"The program path contains a link or cloud placeholder. Put Veyra in a regular local folder."};
    case FailureKind::WrongType: return {L"wrong-type", wrongType, L"The program path has an unexpected file type. Repair Veyra with the official installer."};
    case FailureKind::Inspect: return {L"inspect", inspect, L"Windows could not read the program path."};
    case FailureKind::Wait: return {L"wait", wait, L"Windows could not wait for the program to exit."};
    case FailureKind::ExitCode: return {L"exit-code", exitCode, L"Windows could not read the program's exit code."};
    case FailureKind::Loader: return {L"loader", start, L"Windows could not load the program."};
    case FailureKind::Lock: return {L"lock", lock, L"Windows could not acquire the installation lock. Check access to the Veyra folder."};
    case FailureKind::Start: return {L"start", start, L"Windows could not start the program."};
    }
    return {L"start", start, L"Windows could not start the program."};
}

bool missingPath(DWORD code) {
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}
std::optional<Failure> checkPath(const fs::path& path, bool directory) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        return Failure{missingPath(code) ? FailureKind::Missing : FailureKind::Inspect, code, path};
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        return Failure{FailureKind::Reparse, ERROR_SUCCESS, path};
    if (bool(attrs & FILE_ATTRIBUTE_DIRECTORY) != directory)
        return Failure{FailureKind::WrongType, ERROR_SUCCESS, path};
    return {};
}

RunResult run(const fs::path& program, const std::vector<std::wstring>& args) {
    const auto directory = program.parent_path();
    if (auto failure = checkPath(directory.parent_path(), true)) return {0, std::move(failure)};
    if (auto failure = checkPath(directory, true)) return {0, std::move(failure)};
    if (auto failure = checkPath(program, false)) return {0, std::move(failure)};
    std::wstring command = quote(program.native());
    for (const auto& argument : args) command += L" " + quote(argument);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &child)) {
        const DWORD code = GetLastError();
        return {0, Failure{missingPath(code) ? FailureKind::Missing : FailureKind::Start, code, program}};
    }
    Handle process{child.hProcess}, thread{child.hThread};
    if (const DWORD wait = WaitForSingleObject(process.value, INFINITE); wait != WAIT_OBJECT_0) {
        const DWORD code = wait == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
        return {0, Failure{FailureKind::Wait, code, program}};
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process.value, &code)) {
        const DWORD error = GetLastError();
        return {0, Failure{FailureKind::ExitCode, error, program}};
    }
    return {code, {}};
}

int showError(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"Veyra", MB_OK | MB_ICONERROR);
    return 1;
}

constexpr DWORD kInvalidImage = 0xC000007B;
constexpr DWORD kInvalidImageNotMz = 0xC000012F;
constexpr DWORD kDllNotFound = 0xC0000135;

std::wstring systemMessage(const Failure& failure) {
    // An attribute check can fail even when Windows read the attributes
    // successfully. Loader exit codes are NTSTATUS.
    if (!failure.code || failure.kind == FailureKind::Loader) return {};
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, failure.code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (!length) return {};
    std::wstring message(buffer, length);
    LocalFree(buffer);
    message.erase(message.find_last_not_of(L"\r\n \t") + 1);
    if (failure.code == ERROR_BAD_EXE_FORMAT || failure.code == ERROR_EXE_MACHINE_TYPE_MISMATCH) {
        // These system messages use %1 for the executable's name.
        const auto placeholder = message.find(L"%1");
        if (placeholder != std::wstring::npos) message.replace(placeholder, 2, failure.path.filename().native());
    }
    return message;
}

std::string utf8(std::wstring_view value) {
    const int size = static_cast<int>(value.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), size, nullptr, 0, nullptr, nullptr);
    if (!bytes) return {};
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), size, out.data(), bytes, nullptr, nullptr);
    return out;
}

fs::path recordFailure(const fs::path& root, const fs::path& program, const Failure& failure, bool installationLocked = true) {
    constexpr auto name = L"launcher-last-error.txt";
    SYSTEMTIME time{};
    GetSystemTime(&time);
    const auto code = failure.code ? std::format(L"{} (0x{:08X})", failure.code, failure.code) : L"n/a";
    const auto entry = std::format(L"time={:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z\n"
                                   L"category={}\ncode={}\nprogram={}\npath={}\n",
                                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                                   time.wMilliseconds, failureText(failure.kind).category, code,
                                   program.native(), failure.path.native());
    const auto data = utf8(entry);
    if (data.empty()) return {};
    if (installationLocked && writeSmallFile(root / name, data)) return root / name;

    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    if (!length || length >= std::size(local)) return {};
    const fs::path localRoot(local);
    if (!localRoot.is_absolute() || !plainPath(localRoot, true)) return {};
    const auto fallback = localRoot / L"Veyra";
    std::error_code error;
    fs::create_directory(fallback, error);
    if (error || !plainPath(fallback, true) || !writeSmallFile(fallback / name, data)) return {};
    return fallback / name;
}

void showFailure(const fs::path& program, const Failure& failure, const fs::path& diagnostic) {
    const auto wording = failureText(failure.kind);
    std::wstring message = std::wstring(wording.chinese) + L"\n" + wording.english;
    const auto system = systemMessage(failure);
    if (failure.kind == FailureKind::Loader) {
        message += std::format(L"\nNTSTATUS 0x{:08X}", failure.code);
    } else if (failure.code) {
        message += L"\n" + std::format(launcherText::windowsError, failure.code)
            + L" / Windows error " + std::to_wstring(failure.code);
        if (!system.empty()) message += L"\n" + system;
    }
    const wchar_t* advice = nullptr;
    const wchar_t* english = nullptr;
    switch (failure.code) {
    case ERROR_ACCESS_DENIED:
        advice = launcherText::denied;
        english = L"Check access to the installation folder, system policy and your security software's protection history.";
        break;
    case ERROR_BAD_EXE_FORMAT:
    case ERROR_EXE_MACHINE_TYPE_MISMATCH:
    case kInvalidImage:
    case kInvalidImageNotMz:
        advice = launcherText::damaged;
        english = L"The file may be damaged or incompatible with this Windows system. Repair Veyra with the official installer.";
        break;
    case ERROR_VIRUS_INFECTED:
    case ERROR_VIRUS_DELETED:
        advice = launcherText::blocked;
        english = L"Windows reports that security software blocked or removed the file. Check Protection history.";
        break;
    case kDllNotFound:
        advice = launcherText::dependency;
        english = L"The program is missing runtime components. Repair Veyra with the official installer.";
        break;
    }
    if (advice) message += L"\n\n" + std::wstring(advice) + L"\n" + english;
    message += L"\n\n" + program.native();
    if (failure.path != program) message += L"\n" + failure.path.native();
    if (!diagnostic.empty()) message += L"\n\n" + std::wstring(launcherText::logPath) + L" / Diagnostic log\n" + diagnostic.native();
    showError(message.c_str());
}

bool instanceNotice(instance::Notice notice, DWORD pid, const fs::path& dump) {
    const auto identifier = L"Veyra (PID " + std::to_wstring(pid) + L")";
    if (notice == instance::Notice::ConfirmEnd) {
        const auto question = identifier
            + L" 未响应。结束进程可能丢失正在写入的录制。是否结束它并重新打开 Veyra？\n\n"
            + identifier + L" is not responding. Ending it may lose an ongoing recording. End it and reopen Veyra?";
        return MessageBoxW(nullptr, question.c_str(), L"Veyra", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES;
    }
    std::wstring message;
    if (notice == instance::Notice::EndPending) {
        message = identifier
            + L" 仍未退出，Windows 可能还在等待设备操作结束。如果使用 USB 采集卡，请重新插拔后再打开。问题仍在时请重启电脑。\n\n"
            + identifier + L" has not exited. Windows may still be waiting for a device operation. "
              L"If you use a USB capture card, reconnect it and open Veyra again. Restart Windows if the problem persists.";
    } else if (notice == instance::Notice::EndFailed) {
        message = L"Windows 无法确认 " + identifier + L" 已退出。请在任务管理器中查看此进程。\n\n"
            + L"Windows could not confirm that " + identifier + L" exited. Check the process in Task Manager.";
    } else if (notice == instance::Notice::Hung) {
        message = identifier
            + L" 未响应。请在任务管理器中检查此进程。\n"
            + identifier + L" is not responding. Check the process in Task Manager.";
    } else if (notice == instance::Notice::StartingOrClosing) {
        message = L"Veyra 正在打开或退出。请稍候再试。\nVeyra is starting or closing. Wait a moment and try again.";
    } else {
        message = L"Veyra 已经打开。请切换到原窗口继续使用。\nVeyra is already open. Switch to its window to continue.";
    }
    if (!dump.empty()) message += L"\n\n反馈时请附上以下转储文件。\nAttach this dump when reporting the issue.\n" + dump.wstring();
    showError(message.c_str());
    return false;
}

HANDLE openRunLock(const fs::path& root) {
    return CreateFileW((root / L".veyra-running.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                       0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, std::size(executable));
    if (length == 0 || length >= std::size(executable)) return 1;
    const auto root = fs::path(executable).parent_path();
    const auto versions = root / versionsFolder;
    if (packageFlavor(root).empty() || !plainPath(root / versionState, false))
        return showError(L"便携包文件不完整，请重新解压。\nExtract the complete Veyra portable ZIP.");

    int count = 0;
    auto* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> initialArgs;
    if (wide) {
        for (int i = 1; i < count; ++i) initialArgs.emplace_back(wide[i]);
        LocalFree(wide);
    }
    const bool manualRollback = initialArgs.size() == 1 && initialArgs.front() == L"--rollback";
    if (manualRollback) initialArgs.clear();

    Handle lock{openRunLock(root)};
    DWORD lockError = lock.value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (lockError == ERROR_SHARING_VIOLATION && !manualRollback) {
        // The lock still serializes starts, installs and updates. While it is
        // held, only contact a window that already advertises this profile.
        auto profile = root / L"runtime_local" / L"user-data-2.0.0";
        for (size_t i = 0; i + 1 < initialArgs.size(); ++i) {
            if (initialArgs[i] == L"--data-dir") {
                profile = versions / installedVersions(root).current / initialArgs[++i];
            }
        }
        const instance::Profile owner(std::move(profile), root / L"logs");
        const auto target = instance::launchTarget(initialArgs);
        auto deadline = GetTickCount64() + 3000;
        do {
            if (const HWND existing = instance::findWindow(owner.key())) {
                const auto state = installedVersions(root);
                const auto player = state.current.empty() ? fs::path{} : versions / state.current / playerName;
                const auto result = owner.handoff(existing, target, player, instanceNotice);
                if (result == instance::Handoff::Done) return 0;
                if (result == instance::Handoff::Released) deadline = GetTickCount64() + 3000;
            }
            Sleep(60);
            lock.value = openRunLock(root);
            lockError = lock.value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        } while (lockError == ERROR_SHARING_VIOLATION && GetTickCount64() < deadline);
        if (lockError == ERROR_SHARING_VIOLATION && owner.reportStalledOwner(instanceNotice)) return 0;
    }
    if (lockError == ERROR_SHARING_VIOLATION)
        return showError(L"这份 Veyra 正在运行或更新。\nThis copy of Veyra is running or updating.");
    if (lockError != ERROR_SUCCESS) {
        const auto current = installedVersions(root).current;
        const auto path = root / L".veyra-running.lock";
        const auto program = current.empty() ? path : versions / current / playerName;
        const Failure failure{FailureKind::Lock, lockError, path};
        const auto diagnostic = recordFailure(root, program, failure, false);
        showFailure(program, failure, diagnostic);
        return 1;
    }

    auto state = installedVersions(root);
    if (state.current.empty()) return showError(L"便携包版本记录损坏。\nInvalid Veyra version record.");

    auto rollback = [&] {
        if (state.previous.empty() || !plainPath(versions, true) || !plainPath(versions / state.previous, true)
            || !plainPath(versions / state.previous / playerName, false)) return false;
        const InstalledVersions fallback{state.previous, {}};
        if (!setInstalledVersions(root, fallback)) return false;
        state = fallback;
        const auto updates = root / updateFolder;
        if (plainPath(updates, true)) writeSmallFile(updates / L"result.txt", "rolled-back\n");
        return true;
    };
    if (manualRollback && !rollback())
        return showError(L"没有可恢复的版本。\nNo previous version is available.");

    auto args = std::move(initialArgs);
    for (;;) {
        auto playerArgs = args;
        playerArgs.insert(playerArgs.begin(), L"--managed-portable");
        const auto started = GetTickCount64();
        const auto program = versions / state.current / playerName;
        auto result = run(program, playerArgs);
        if (!result.failure && (result.exitCode == kInvalidImage || result.exitCode == kInvalidImageNotMz
                                || result.exitCode == kDllNotFound)) {
            result.failure = Failure{FailureKind::Loader, result.exitCode, program};
        }
        if (result.failure) {
            const auto logPath = recordFailure(root, program, *result.failure);
            if (rollback()) continue;
            showFailure(program, *result.failure, logPath);
            return result.failure->kind == FailureKind::Loader ? static_cast<int>(result.exitCode) : 1;
        }
        if (result.exitCode != 0 && result.exitCode != 42 && result.exitCode != applyExitCode
            && result.exitCode != overlayRestartExitCode && GetTickCount64() - started < 15000) {
            if (rollback()) continue;
            return static_cast<int>(result.exitCode);
        }
        if (result.exitCode == applyExitCode) {
            const auto worker = versions / state.current / workerName;
            const auto applied = run(worker, {root.native()});
            if (applied.failure) recordFailure(root, worker, *applied.failure);
            const auto updates = root / updateFolder;
            if ((applied.failure || applied.exitCode != 0) && plainPath(updates, true)) {
                for (const auto* name : {L"pending.json", L"download.zip"})
                    if (plainPath(updates / name, false)) DeleteFileW((updates / name).c_str());
                if (readSmallFile(updates / L"result.txt").empty())
                    writeSmallFile(updates / L"result.txt", "worker-failed\n");
            }
            return 0;
        } else if (result.exitCode == 42 || result.exitCode == overlayRestartExitCode) {
            args = {L"--page", L"set"};
            for (size_t i = 0; i + 1 < playerArgs.size(); ++i)
                if (playerArgs[i] == L"--data-dir") args.insert(args.end(), {playerArgs[i], playerArgs[i + 1]});
            if (result.exitCode == overlayRestartExitCode) args.emplace_back(L"--overlay-compat-restart");
        } else {
            return static_cast<int>(result.exitCode);
        }
    }
}
