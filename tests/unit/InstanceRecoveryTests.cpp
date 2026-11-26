#include "veyra/base/InstanceWindow.h"
#include "veyra/FileIdentity.h"
#include "veyra/base/PortableFiles.h"
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <thread>

namespace {
using namespace veyra::instance;

int fixture(const std::filesystem::path& directory, const std::filesystem::path& logs,
            std::wstring_view mode, const std::filesystem::path& installationLock) {
    Profile profile(directory, logs);
    const HANDLE installation = installationLock.empty() ? nullptr :
        CreateFileW(installationLock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (installation == INVALID_HANDLE_VALUE) return 2;
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, profile.key().c_str());
    if (!mutex) return 2;
    if (mode == L"exiting") {
        // A live process can outlast the thread that owned its startup mutex.
        std::thread([mutex] { WaitForSingleObject(mutex, INFINITE); }).join();
    } else if (WaitForSingleObject(mutex, 0) != WAIT_OBJECT_0) return 2;
    HWND window = nullptr;
    if (mode == L"window" || mode == L"requested") {
        window = CreateWindowExW(0, L"STATIC", L"Veyra recovery fixture", WS_OVERLAPPEDWINDOW,
                                 0, 0, 200, 150, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window || !SetPropW(window, profile.key().c_str(), reinterpret_cast<HANDLE>(1))) return 2;
    }
    if (!profile.rememberOwner(mode != L"starting")) return 2;
    const auto eventName = L"Local\\Veyra.Recovery.Fixture." + std::to_wstring(GetCurrentProcessId());
    const HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (!stop) return 2;
    std::thread worker;
    if (mode == L"requested") worker = std::thread([stop] { WaitForSingleObject(stop, 120000); });
    std::puts("READY");
    std::fflush(stdout);
    // In window mode the fixture deliberately does not pump messages.
    WaitForSingleObject(stop, 120000);
    if (worker.joinable()) worker.join();
    CloseHandle(stop);
    if (window) {
        RemovePropW(window, profile.key().c_str());
        DestroyWindow(window);
    }
    if (mode != L"exiting") ReleaseMutex(mutex);
    CloseHandle(mutex);
    if (installation) CloseHandle(installation);
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace veyra::instance;
    if (argc >= 5 && std::wstring_view(argv[1]) == L"--fixture")
        return fixture(argv[2], argv[3], argv[4], argc > 5 ? std::filesystem::path(argv[5]) : std::filesystem::path{});

    const auto start = GetTickCount64();
    const auto pending = waitForExit(GetCurrentProcess(), true);
    const auto elapsed = GetTickCount64() - start;
    if (pending != ProcessExit::Pending || elapsed < 4900 || elapsed >= 10000) {
        std::printf("FAIL process wait: %llu ms\n", elapsed);
        return 1;
    }
    std::printf("PASS process still alive after %llu ms stays pending\n", elapsed);
    if (waitForExit(GetCurrentProcess(), false) != ProcessExit::Failed) return 1;
    std::puts("PASS denied termination is not reported as exit");
    const HANDLE exited = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (!exited) return 2;
    const auto result = waitForExit(exited, false);
    CloseHandle(exited);
    if (result != ProcessExit::Exited) return 1;
    std::puts("PASS signaled handle confirms exit even if the request raced");

    // Pure-C++ SHA-256 profileKey must match BCrypt sha256Hex byte-for-byte
    // so Veyra.exe stays compatible across in-app updates and rollbacks.
    {
        for (const std::wstring_view sample : {
                std::wstring_view(L"C:\\Veyra\\user-data-2.0.0"),
                std::wstring_view(L"C:\\Users\\Test\\AppData\\Local\\Veyra\\a-longer-profile-path-spanning-multiple-sha256-blocks\\user-data-2.0.0"),
                std::wstring_view(L"E:\\Veyra\\55bytes-boundary-check\\1234567890")}) {
            std::error_code ec;
            auto abs = std::filesystem::absolute(std::filesystem::path(sample), ec);
            auto can = std::filesystem::weakly_canonical(abs, ec);
            auto norm = (ec ? abs.lexically_normal() : can).make_preferred();
            if (norm.filename().empty() && norm != norm.root_path()) norm = norm.parent_path();
            auto lower = norm.wstring();
            CharLowerBuffW(lower.data(), DWORD(lower.size()));
            const auto expectedHash = veyra::sha256Hex(
                reinterpret_cast<const uint8_t*>(lower.data()), lower.size() * sizeof(wchar_t));
            const auto expected = L"Local\\Veyra.Qml.Profile." + std::wstring(expectedHash.begin(), expectedHash.end());
            const auto actual = profileKey(std::filesystem::path(sample));
            if (actual != expected || actual.empty()) {
                std::puts("FAIL profileKey SHA-256 mismatch with BCrypt");
                return 1;
            }
        }
        std::puts("PASS pure-C++ profileKey matches BCrypt SHA-256 across paths");
    }

    // Stalled-owner stamp check and cleanup (no OpenProcess/Toolhelp32 needed in launcher).
    {
        wchar_t tempBuf[MAX_PATH]{};
        const DWORD tempLen = GetTempPathW(MAX_PATH, tempBuf);
        if (!tempLen || tempLen >= MAX_PATH) return 2;
        const auto root = std::filesystem::path(tempBuf)
            / (L"veyra-instance-test-" + std::to_wstring(GetCurrentProcessId()));
        const auto logs = root / L"logs";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(logs, ec);
        const DWORD fakePid = GetCurrentProcessId() + 1000;
        const auto dumpPath = logs / (L"veyra-hang-20261126-000000-" + std::to_wstring(fakePid) + L".dmp");
        if (!veyra::portable::writeSmallFile(dumpPath, "dump")) return 2;
        const auto logsU8 = logs.u8string();
        const std::string logsStr(reinterpret_cast<const char*>(logsU8.data()), logsU8.size());
        // windowReady=0 must NOT trigger EndPending.
        if (!veyra::portable::writeSmallFile(root / L".veyra-instance.v1",
                std::to_string(fakePid) + " 1\n0\n" + logsStr)) return 2;
        bool called = false;
        if (reportStalledOwnerFromStamp(root, logs, [&](Notice, DWORD, const std::filesystem::path&) {
                called = true; return false;
            }) || called) {
            std::puts("FAIL windowReady=0 unexpectedly reported stalled owner");
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        // windowReady=1 must report EndPending with matching PID and hang dump.
        Notice seenNotice = Notice::AlreadyOpen;
        DWORD seenPid = 0;
        std::filesystem::path seenDump;
        if (!veyra::portable::writeSmallFile(root / L".veyra-instance.v1",
                std::to_string(fakePid) + " 1\n1\n" + logsStr)) return 2;
        const bool reported = reportStalledOwnerFromStamp(root, logs,
            [&](Notice n, DWORD pid, const std::filesystem::path& dump) {
                seenNotice = n; seenPid = pid; seenDump = dump; return false;
            });
        if (!reported || seenNotice != Notice::EndPending || seenPid != fakePid || seenDump != dumpPath) {
            std::puts("FAIL windowReady=1 did not report expected EndPending PID/dump");
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        // clearOwnerStamp must remove the stamp so stale runs don't report EndPending.
        clearOwnerStamp(root);
        if (reportStalledOwnerFromStamp(root, logs, [](Notice, DWORD, const std::filesystem::path&) { return false; })) {
            std::puts("FAIL clearOwnerStamp did not clear owner stamp");
            std::filesystem::remove_all(root, ec);
            return 1;
        }
        std::filesystem::remove_all(root, ec);
        std::puts("PASS launcher stamp detection and stale-stamp cleanup");
    }
    return 0;
}
