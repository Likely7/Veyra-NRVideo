#include "veyra/ngx/NvapiLifetime.h"
#include <array>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

int main()
{
    std::array<HMODULE, 8> handles{};
    std::array<std::thread, 8> threads;
    std::atomic_bool ok{true};
    // Race the first acquisition as well as subsequent graph-style borrows.
    for (size_t i = 0; i < threads.size(); ++i) threads[i] = std::thread([&, i] {
        for (unsigned cycle = 0; cycle < 64; ++cycle) {
            const auto module = veyra::ngx::processNvapi();
            if (!module || (handles[i] && handles[i] != module)) ok = false;
            handles[i] = module;
        }
    });
    for (auto& thread : threads) thread.join();
    if (!ok) return 1;
    for (const auto handle : handles) if (handle != handles[0]) return 2;
    wchar_t directory[MAX_PATH]{}, actual[MAX_PATH]{};
    if (!GetSystemDirectoryW(directory, MAX_PATH) || !GetModuleFileNameW(handles[0], actual, MAX_PATH)) return 3;
    const std::wstring expected = std::wstring(directory) + L"\\nvapi64.dll";
    if (_wcsicmp(actual, expected.c_str())) return 4;
    for (unsigned cycle = 0; cycle < 64; ++cycle) {
        HMODULE temporary = LoadLibraryExW(expected.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (temporary != handles[0] || !FreeLibrary(temporary)) return 5;
        if (GetModuleHandleW(expected.c_str()) != handles[0] ||
            !GetProcAddress(handles[0], "nvapi_QueryInterface")) return 6;
    }
    // Isolated process: exercise the documented PIN guarantee even when a
    // consumer incorrectly drops extra references. Never do this in playback.
    for (unsigned i = 0; i < 3; ++i) if (!FreeLibrary(handles[0])) return 7;
    if (GetModuleHandleW(expected.c_str()) != handles[0] ||
        !GetProcAddress(handles[0], "nvapi_QueryInterface")) return 8;
    std::cout << "PASS concurrent system-only acquisition, 512 stable borrows, 64 balanced consumer load/unloads, process pin survives final-reference drops\n";
    return 0;
}
