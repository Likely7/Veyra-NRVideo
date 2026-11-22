#include "veyra/base/InstanceWindow.h"
#include <cstdio>
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
    return 0;
}
