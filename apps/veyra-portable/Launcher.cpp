#include "veyra/base/PortableFiles.h"

#include <shellapi.h>
#include <vector>

namespace {
using namespace veyra::portable;
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

    Handle lock{CreateFileW((root / L".veyra-running.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (lock.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION)
            return showError(L"这份 Veyra 正在运行或更新。\nThis copy of Veyra is running or updating.");
        return showError(L"无法写入便携目录。\nCopy Veyra to a writable folder.");
    }

    int count = 0;
    auto* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> initialArgs;
    if (wide) {
        for (int i = 1; i < count; ++i) initialArgs.emplace_back(wide[i]);
        LocalFree(wide);
    }
    const bool manualRollback = initialArgs.size() == 1 && initialArgs.front() == L"--rollback";
    if (manualRollback) initialArgs.clear();
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
