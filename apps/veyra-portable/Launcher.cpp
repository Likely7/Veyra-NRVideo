#include "veyra/base/PortableFiles.h"
#include "veyra/base/InstanceWindow.h"

#include <shellapi.h>
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

std::optional<DWORD> run(const fs::path& program, const std::vector<std::wstring>& args) {
    if (!plainPath(program.parent_path(), true) || !plainPath(program, false)) return {};
    fs::path workingDirectory = program.parent_path();
    std::wstring command = quote(program.native());
    for (const auto& argument : args) command += L" " + quote(argument);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startup, &child)) return {};
    Handle process{child.hProcess}, thread{child.hThread};
    DWORD code = 0;
    if (WaitForSingleObject(process.value, INFINITE) != WAIT_OBJECT_0
        || !GetExitCodeProcess(process.value, &code)) return {};
    return code;
}

int showError(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"Veyra", MB_OK | MB_ICONERROR);
    return 1;
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
    if (packageFlavor(root).empty() || !plainPath(versions, true)
        || !plainPath(root / versionState, false))
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
    if (lockError != ERROR_SUCCESS)
        return showError(L"无法写入便携目录。\nCopy Veyra to a writable folder.");

    auto state = installedVersions(root);
    if (state.current.empty()) return showError(L"便携包版本记录损坏。\nInvalid Veyra version record.");

    auto rollback = [&] {
        if (state.previous.empty() || !plainPath(versions / state.previous, true)
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
        const auto result = run(versions / state.current / playerName, playerArgs);
        if (!result || (*result != 0 && *result != 42 && *result != applyExitCode
                        && *result != overlayRestartExitCode && GetTickCount64() - started < 15000)) {
            if (rollback()) continue;
            if (!result) return showError(L"Veyra 无法启动。\nVeyra could not start.");
            return static_cast<int>(*result);
        }
        if (*result == applyExitCode) {
            const auto applied = run(versions / state.current / workerName, {root.native()});
            const auto updates = root / updateFolder;
            if ((!applied || *applied != 0) && plainPath(updates, true)) {
                for (const auto* name : {L"pending.json", L"download.zip"})
                    if (plainPath(updates / name, false)) DeleteFileW((updates / name).c_str());
                if (readSmallFile(updates / L"result.txt").empty())
                    writeSmallFile(updates / L"result.txt", "worker-failed\n");
            }
            return 0;
        } else if (*result == 42 || *result == overlayRestartExitCode) {
            args = {L"--page", L"set"};
            for (size_t i = 0; i + 1 < playerArgs.size(); ++i)
                if (playerArgs[i] == L"--data-dir") args.insert(args.end(), {playerArgs[i], playerArgs[i + 1]});
            if (*result == overlayRestartExitCode) args.emplace_back(L"--overlay-compat-restart");
        } else {
            return static_cast<int>(*result);
        }
    }
}
