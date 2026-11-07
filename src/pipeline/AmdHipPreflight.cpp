// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/pipeline/AmdHipPreflight.h"

#include "veyra/Log.h"
#include "veyra/pipeline/Rdna3HipDevices.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace veyra::pipeline::amdhip {
namespace {
constexpr DWORD kChildTimeoutMs = 30000;
const char* g_step = "start";

// Child side: one line per event on the inherited pipe, unbuffered, no allocation
// on the crash path.
void emit(const char* text) {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) return;
    char line[600];
    const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "amd-hip-preflight %s\n", text);
    DWORD written = 0;
    if (n > 0) WriteFile(out, line, DWORD(n), &written, nullptr);
}
void step(const char* name) {
    g_step = name;
    char text[96];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "step=%s", name);
    emit(text);
}
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    const auto* record = info ? info->ExceptionRecord : nullptr;
    const DWORD code = record ? record->ExceptionCode : 0;
    const void* address = record ? record->ExceptionAddress : nullptr;
    char where[320] = "unknown";
    HMODULE module = nullptr;
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCWSTR>(address), &module)) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        char narrow[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, name ? name + 1 : path, -1, narrow, MAX_PATH, nullptr, nullptr);
        _snprintf_s(where, sizeof(where), _TRUNCATE, "%s+0x%llX", narrow,
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    }
    char text[480];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "crash code=0x%08lX at=%s step=%s", code, where, g_step);
    emit(text);
    return EXCEPTION_EXECUTE_HANDLER;   // the child ends with the exception code
}
// Test hook: VEYRA_TEST_AMD_HIP_PREFLIGHT_CRASH=<step>:<class|all> reads address
// 0x390 on another thread at that step, as the field crash did.
void maybeCrash(const char* name, int priority) {
    char value[64]{};
    if (!GetEnvironmentVariableA("VEYRA_TEST_AMD_HIP_PREFLIGHT_CRASH", value, sizeof(value))) return;
    const char* colon = strchr(value, ':');
    if (!colon || size_t(colon - value) != strlen(name) || strncmp(value, name, strlen(name)) != 0) return;
    if (strcmp(colon + 1, "all") != 0 && atoi(colon + 1) != priority) return;
    std::thread([] { (void)*reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x390)); }).join();
}
int fail(const char* failedStep, long long code, int exitCode) {
    char text[160];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "failed step=%s code=%lld", failedStep, code);
    emit(text);
    return exitCode;
}

template<class F> F resolve(HMODULE module, const char* name) { return reinterpret_cast<F>(GetProcAddress(module, name)); }

// Parent side: run one child with only its own output pipe inherited.
bool runChild(const std::wstring& executable, const std::wstring& command, std::string& output, DWORD& exitCode, bool& timedOut) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 65536)) return false;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = write;
    startup.StartupInfo.hStdError = write;
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &write, sizeof(write), nullptr, nullptr)) {
        CloseHandle(read); CloseHandle(write); return false;
    }
    std::wstring line = command;
    PROCESS_INFORMATION process{};
    const bool created = CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process) != FALSE;
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(write);
    if (!created) { CloseHandle(read); return false; }
    // A few hundred bytes: they fit the pipe, so waiting first cannot block the child.
    const DWORD waited = WaitForSingleObject(process.hProcess, kChildTimeoutMs);
    timedOut = waited != WAIT_OBJECT_0;
    if (timedOut) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
    GetExitCodeProcess(process.hProcess, &exitCode);
    char buffer[4096];
    DWORD n = 0;
    while (ReadFile(read, buffer, sizeof(buffer), &n, nullptr) && n) output.append(buffer, n);
    CloseHandle(read); CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}

std::mutex g_mutex;
std::wstring g_executable;
std::map<uint64_t, PreflightOutcome> g_outcomes;
} // namespace

int preflightMain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(crashFilter);
    if (argc < 5) return fail("args", argc, 2);
    const std::wstring hipDll = argv[2];
    const uint64_t luid = wcstoull(argv[3], nullptr, 16);
    const int priority = _wtoi(argv[4]);
    char text[256];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "start luid=0x%016llX class=%d", static_cast<unsigned long long>(luid), priority);
    emit(text);

    // The same adapter as the player's device, through its LUID.
    step("d3d12");
    IDXGIFactory4* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return fail("d3d12", hr, 10);
    const LUID wanted{DWORD(luid & 0xFFFFFFFFull), LONG(luid >> 32)};
    IDXGIAdapter1* adapter = nullptr;
    hr = factory->EnumAdapterByLuid(wanted, IID_PPV_ARGS(&adapter));
    factory->Release();
    if (FAILED(hr)) return fail("d3d12", hr, 10);
    ID3D12Device* device = nullptr;
    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    adapter->Release();
    if (FAILED(hr)) return fail("d3d12", hr, 10);

    // The parent's scheduling class; WDDM keeps none before the device above exists.
    if (priority > 0) {
        step("priority");
        const HMODULE gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto set = gdi ? resolve<LONG(WINAPI*)(HANDLE, int)>(gdi, "D3DKMTSetProcessSchedulingPriorityClass") : nullptr;
        const auto get = gdi ? resolve<LONG(WINAPI*)(HANDLE, int*)>(gdi, "D3DKMTGetProcessSchedulingPriorityClass") : nullptr;
        if (!set || !get) return fail("priority", -1, 11);
        const LONG status = set(GetCurrentProcess(), priority);
        int actual = -1;
        get(GetCurrentProcess(), &actual);
        _snprintf_s(text, sizeof(text), _TRUNCATE, "info class=%d status=0x%08lX", actual, static_cast<unsigned long>(status));
        emit(text);
        if (actual != priority) return fail("priority", actual, 11);
    }

    step("load");
    const HMODULE hip = LoadLibraryExW(hipDll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hip) return fail("load", GetLastError(), 12);
    using Int = int (*)(int*);
    const auto init = resolve<int (*)(unsigned)>(hip, "hipInit");
    const auto version = resolve<Int>(hip, "hipRuntimeGetVersion");
    const auto count = resolve<Int>(hip, "hipGetDeviceCount");
    const auto properties = resolve<int (*)(void*, int)>(hip, "hipGetDevicePropertiesR0600");
    const auto select = resolve<int (*)(int)>(hip, "hipSetDevice");
    const auto createStream = resolve<int (*)(void**)>(hip, "hipStreamCreate");
    const auto allocate = resolve<int (*)(void**, size_t)>(hip, "hipMalloc");
    const auto clear = resolve<int (*)(void*, int, size_t, void*)>(hip, "hipMemsetAsync");
    const auto wait = resolve<int (*)(void*)>(hip, "hipStreamSynchronize");
    const auto release = resolve<int (*)(void*)>(hip, "hipFree");
    const auto destroyStream = resolve<int (*)(void*)>(hip, "hipStreamDestroy");
    if (!init || !version || !count || !properties || !select || !createStream || !allocate || !clear || !wait || !release || !destroyStream)
        return fail("load", -2, 12);

    step("init");
    maybeCrash("init", priority);
    if (const int e = init(0)) return fail("init", e, 13);
    int runtime = 0;
    version(&runtime);

    step("devices");
    maybeCrash("devices", priority);
    int devices = 0;
    if (const int e = count(&devices)) return fail("devices", e, 14);
    std::vector<rdna3::hip::Device> list;
    std::vector<char> props(rdna3::hip::kPropertiesBytes);
    for (int i = 0; i < devices; ++i) {
        std::fill(props.begin(), props.end(), char(0));
        if (const int e = properties(props.data(), i)) return fail("devices", e, 14);
        list.push_back(rdna3::hip::parseProperties(i, props.data(), props.size()));
        _snprintf_s(text, sizeof(text), _TRUNCATE, "info runtime=%d device=%d luid=0x%016llX arch=%s name=%s", runtime, i,
                    static_cast<unsigned long long>(list.back().luid), list.back().arch.c_str(), list.back().name.c_str());
        emit(text);
    }
    // As the runtime does: the LUID decides; a lone device without one is the fallback.
    auto index = rdna3::hip::indexForLuid(list, luid);
    if (!index && list.size() == 1 && list[0].luid == 0) index = 0;
    if (!index) return fail("devices", devices, 14);

    step("select");
    maybeCrash("select", priority);
    if (const int e = select(*index)) return fail("select", e, 15);

    step("stream");   // the call that crashed in issue 88
    maybeCrash("stream", priority);
    void* stream = nullptr;
    if (const int e = createStream(&stream)) return fail("stream", e, 16);

    step("memory");
    maybeCrash("memory", priority);
    void* memory = nullptr;
    if (const int e = allocate(&memory, size_t(1) << 20)) return fail("memory", e, 17);
    if (const int e = clear(memory, 0, size_t(1) << 20, stream)) return fail("memory", e, 17);
    if (const int e = wait(stream)) return fail("memory", e, 17);
    release(memory);

    step("release");
    if (const int e = destroyStream(stream)) return fail("release", e, 18);
    emit("done status=ok");
    // HIP and the device are left to the system: some HIP builds fault in their
    // own unload, which would turn a pass into a crash report.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

std::vector<int> ladder(int current) {
    if (current < 0) return {0};
    std::vector<int> classes{current};
    if (current > 4) classes.push_back(4);
    if (current > 2) classes.push_back(2);
    return classes;
}

PreflightAttempt parseOutput(const std::string& output, uint32_t exitCode, int priority) {
    PreflightAttempt attempt;
    attempt.priority = priority;
    attempt.exitCode = exitCode;
    bool done = false;
    size_t start = 0;
    while (start < output.size()) {
        const size_t end = std::min(output.find('\n', start), output.size());
        std::string line = output.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        constexpr std::string_view prefix = "amd-hip-preflight ";
        if (line.rfind(prefix, 0) != 0) continue;
        const std::string rest = line.substr(prefix.size());
        if (rest.rfind("step=", 0) == 0) attempt.step = rest.substr(5);
        else if (rest.rfind("info ", 0) == 0) attempt.info += (attempt.info.empty() ? "" : " | ") + rest.substr(5);
        else if (rest == "done status=ok") done = true;
        else if (rest.rfind("crash ", 0) == 0 || rest.rfind("failed ", 0) == 0) attempt.detail = rest;
    }
    attempt.ok = done && exitCode == 0 && attempt.detail.empty();
    if (!attempt.ok && attempt.detail.empty())
        attempt.detail = std::format("ended without a result exit=0x{:08X}", exitCode);
    return attempt;
}

std::string summarize(const std::vector<PreflightAttempt>& attempts) {
    std::string text;
    for (const auto& a : attempts) {
        if (!text.empty()) text += "; ";
        text += a.priority > 0 ? std::format("class {}: ", a.priority) : std::string("default class: ");
        text += a.ok ? std::string("ok") : a.detail + (a.detail.find("step=") == std::string::npos ? " step=" + a.step : std::string());
    }
    return text.empty() ? std::string("not run") : text;
}

void setPreflightExecutable(const std::wstring& path) { std::lock_guard lock(g_mutex); g_executable = path; }
std::wstring preflightExecutable() { std::lock_guard lock(g_mutex); return g_executable; }

PreflightOutcome runLadder(uint64_t adapterLuid, int currentPriority) {
    PreflightOutcome outcome;
    const std::wstring executable = preflightExecutable();
    wchar_t dll[MAX_PATH]{};
    const std::wstring hipDll = GetEnvironmentVariableW(L"VEYRA_TEST_AMD_HIP_DLL", dll, MAX_PATH) ? std::wstring(dll) : std::wstring(L"amdhip64_7.dll");
    for (const int priority : ladder(currentPriority)) {
        const std::wstring command = std::format(L"\"{}\" --amd-hip-preflight \"{}\" {:016X} {}", executable, hipDll, adapterLuid, priority);
        std::string output;
        DWORD exitCode = 0;
        bool timedOut = false;
        PreflightAttempt attempt;
        if (!runChild(executable, command, output, exitCode, timedOut)) {
            attempt.priority = priority;
            attempt.detail = std::format("launch failed win32={}", GetLastError());
        } else {
            attempt = parseOutput(output, exitCode, priority);
            if (timedOut) { attempt.ok = false; attempt.detail = std::format("no result within {} ms", kChildTimeoutMs); }
        }
        veyra::log::info("amd-hip-preflight", std::format("adapter=0x{:016X} class={} result={} step={} exit=0x{:08X} {}{}{}",
            adapterLuid, priority, attempt.ok ? "ok" : "failed", attempt.step.empty() ? "none" : attempt.step, attempt.exitCode, attempt.detail,
            attempt.info.empty() ? "" : " info: ", attempt.info));
        outcome.attempts.push_back(attempt);
        if (attempt.ok) { outcome.ok = true; outcome.priority = priority; break; }
        // A lower class cannot help before HIP is reached (launch, adapter, library).
        if (attempt.step.empty() || attempt.step == "start" || attempt.step == "d3d12" || attempt.step == "load") break;
    }
    outcome.summary = summarize(outcome.attempts);
    return outcome;
}

PreflightOutcome preflightOnce(uint64_t adapterLuid, int currentPriority) {
    static std::mutex runMutex;
    std::lock_guard lock(runMutex);
    {
        std::lock_guard cache(g_mutex);
        if (const auto it = g_outcomes.find(adapterLuid); it != g_outcomes.end()) return it->second;
    }
    auto outcome = runLadder(adapterLuid, currentPriority);
    std::lock_guard cache(g_mutex);
    g_outcomes[adapterLuid] = outcome;
    return outcome;
}
} // namespace veyra::pipeline::amdhip
