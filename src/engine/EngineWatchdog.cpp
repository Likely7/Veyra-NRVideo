#include "veyra/engine/EngineWatchdog.h"

#include <windows.h>
#include <vector>
#include <dbghelp.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>

#include "veyra/Log.h"
#include "veyra/gfx/PresentationHooks.h"

namespace veyra::engine {
namespace {
// Long enough for graph rebuilds and runtime/model loading inside one loop
// iteration; a healthy loop beats every few milliseconds.
constexpr uint64_t kReportMs = 8000;
constexpr unsigned kMaxFrames = 48;
constexpr size_t kMaxCaptureDumpsPerSession = 16;
using source::CaptureDriverStage;

const char* phaseName(CaptureDriverStage phase) {
    switch (phase) {
    case CaptureDriverStage::Stop: return "stop";
    case CaptureDriverStage::Enumerate: return "enumerate";
    case CaptureDriverStage::Configure: return "configure";
    case CaptureDriverStage::Run: return "run";
    case CaptureDriverStage::Release: return "release";
    case CaptureDriverStage::None: return "none";
    }
    return "none";
}

std::mutex g_dumpMutex;
std::wstring g_dumpDirectory;
std::atomic<bool> g_engineDumpRequested{false};
std::atomic<unsigned> g_captureDumpSerial{0};

struct StackSample { uintptr_t pc[kMaxFrames]; unsigned count; };
struct CaptureDumpSignature { CaptureDriverStage phase; StackSample stack; };

bool sameStack(const StackSample& a, const StackSample& b) {
    return a.count == b.count && std::memcmp(a.pc, b.pc, sizeof(uintptr_t) * a.count) == 0;
}

// The owner is suspended while this runs: no allocation, no locks, no logging.
__declspec(noinline) void unwindSuspended(CONTEXT* context, StackSample* out) {
    __try {
        while (out->count < kMaxFrames && context->Rip) {
            out->pc[out->count++] = context->Rip;
            DWORD64 imageBase = 0;
            const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context->Rip, &imageBase, nullptr);
            if (function) {
                PVOID handlerData = nullptr; DWORD64 establisherFrame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context->Rip, function, context, &handlerData, &establisherFrame, nullptr);
            } else {
                context->Rip = *reinterpret_cast<const DWORD64*>(context->Rsp);
                context->Rsp += 8;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool sampleThread(HANDLE thread, StackSample& out) {
    out.count = 0;
    if (SuspendThread(thread) == DWORD(-1)) return false;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    const bool ok = GetThreadContext(thread, &context) != FALSE;
    if (ok) unwindSuspended(&context, &out);
    ResumeThread(thread);
    return ok;
}

uint64_t threadCpuMs(HANDLE thread) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(thread, &created, &exited, &kernel, &user)) return 0;
    const auto value = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return (value(kernel) + value(user)) / 10000;
}

// Nearest named export at or below rva: names the system/driver call a thread is
// parked in (NtWaitForSingleObject, NtUser*, ...) without symbol files.
__declspec(noinline) bool nearestExport(const uint8_t* base, uintptr_t rva, char* name, size_t size, uintptr_t* offset) {
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!directory.VirtualAddress || !directory.Size) return false;
        const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
        const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
        const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
        const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
        DWORD best = 0, bestName = 0;
        for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
            const DWORD function = functions[ordinals[i]];
            const bool forwarded = function >= directory.VirtualAddress && function < directory.VirtualAddress + directory.Size;
            if (!forwarded && function <= rva && function > best) { best = function; bestName = names[i]; }
        }
        if (!bestName || rva - best > 0x10000) return false;
        strncpy_s(name, size, reinterpret_cast<const char*>(base + bestName), _TRUNCATE);
        *offset = rva - best;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string describe(uintptr_t pc) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(pc), &module) || !module)
        return std::format("module=? address=0x{:X}", pc);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    const uintptr_t rva = pc - reinterpret_cast<uintptr_t>(module);
    std::string text = std::format("module={} rva=0x{:X}", std::filesystem::path(path).filename().string(), rva);
    char name[128]{}; uintptr_t offset = 0;
    if (nearestExport(reinterpret_cast<const uint8_t*>(module), rva, name, sizeof(name), &offset))
        text += std::format(" near={}+0x{:X}", name, offset);
    return text;
}

void logStack(const char* category, const char* label, const StackSample& sample) {
    for (unsigned i = 0; i < sample.count; ++i)
        log::error(category, std::format("{} frame={} {}", label, i, describe(sample.pc[i])));
}

bool writeDump(const char* category, bool captureDriver) {
    std::wstring directory;
    { std::lock_guard lock(g_dumpMutex); directory = g_dumpDirectory; }
    if (directory.empty() || (!captureDriver && g_engineDumpRequested.exchange(true))) return false;
    using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                    PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    // Keep one system DLL reference while the process uses the watchdog.
    static const HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto write = dbghelp ? reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!write) { log::error(category, "minidump unavailable (dbghelp)"); return false; }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    auto name = std::format(L"veyra-hang-{:04}{:02}{:02}-{:02}{:02}{:02}-{}", st.wYear, st.wMonth, st.wDay,
                            st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    if (captureDriver) name += L"-capture-" + std::to_wstring(++g_captureDumpSerial);
    const auto path = std::filesystem::path(directory) / (name + L".dmp");
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log::error(category, std::format("minidump open failed error={}", GetLastError()));
        return false;
    }
    // Thread stacks, module list and handle table (which event/mutex a wait is
    // on); no heap, so the file stays small enough to send with the logs.
    const auto type = MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules);
    const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type, nullptr, nullptr, nullptr);
    LARGE_INTEGER bytes{};
    GetFileSizeEx(file, &bytes);
    CloseHandle(file);
    log::error(category, std::format("minidump written={} file={} bytes={}", ok != FALSE, path.filename().string(), bytes.QuadPart));
    if (!ok) DeleteFileW(path.c_str());
    return ok != FALSE;
}
} // namespace

struct EngineWatchdog::State {
    Notify notify;
    std::atomic<uint64_t> lastBeat{0};
    std::atomic<uint64_t> session{0};
    std::atomic<CaptureDriverStage> captureStage{CaptureDriverStage::None};
    std::atomic<bool> armed{false}, reporting{false};
    std::atomic<const char*> stage{nullptr};
    std::atomic<DWORD> ownerId{0};
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, exited = false;  // mutex
    HANDLE owner = nullptr;                 // mutex
    std::mutex notifyMutex;                 // held while the callback runs
    ~State() { if (owner) CloseHandle(owner); }

    void tell(uint64_t stalledMs, bool captureDriver) {
        std::lock_guard lock(notifyMutex);
        if (notify) notify(stalledMs, captureDriver);
    }

    void touchOwner() {
        const DWORD id = GetCurrentThreadId();
        if (ownerId.load(std::memory_order_relaxed) == id) return;
        std::lock_guard lock(mutex);
        if (owner) CloseHandle(owner);
        owner = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
        ownerId = id;
    }

    HANDLE duplicateOwner(DWORD& id) {
        std::lock_guard lock(mutex);
        id = ownerId.load();
        HANDLE copy = nullptr;
        if (owner) DuplicateHandle(GetCurrentProcess(), owner, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
        return copy;
    }

    void watch() {
        uint64_t beatAtReport = 0, resampleAt = 0, notifiedAt = 0, cpuAtReport = 0;
        unsigned samples = 0;
        StackSample first{};
        auto reportedPhase = CaptureDriverStage::None;
        uint64_t reportedSession = 0, dumpSession = 0;
        std::vector<CaptureDumpSignature> captureDumps;
        for (;;) {
            {
                std::unique_lock lock(mutex);
                if (wake.wait_for(lock, std::chrono::milliseconds(250), [&] { return stopping; })) break;
            }
            const uint64_t now = GetTickCount64();
            const auto phase = captureStage.load();
            const uint64_t sessionId = session.load();
            const uint64_t beatAt = lastBeat.load();
            const uint64_t stalled = now > beatAt ? now - beatAt : 0;
            const bool captureDriver = phase != CaptureDriverStage::None;
            const bool active = captureDriver || armed.load();
            if (reporting.load()) {
                const bool reportedCapture = reportedPhase != CaptureDriverStage::None;
                const auto category = reportedCapture ? "capture-driver-hang" : "engine-hang";
                if (!active || beatAt != beatAtReport || phase != reportedPhase || sessionId != reportedSession) {
                    log::warn(category, std::format("owner moved again after {} ms (stage={})",
                                                    now - beatAtReport, reportedCapture ? phaseName(reportedPhase) : "playback"));
                    reporting = false;
                    tell(0, reportedCapture);
                    continue;
                }
                if (now - notifiedAt >= 1000) { tell(stalled, reportedCapture); notifiedAt = now; }
                if (samples < 3 && now >= resampleAt) {
                    DWORD id = 0;
                    const HANDLE thread = duplicateOwner(id);
                    StackSample again{};
                    if (thread && sampleThread(thread, again)) {
                        const bool same = sameStack(again, first);
                        log::error(category, std::format("resample owner={} stalledMs={} cpuMsSinceReport={} stack={}", id, stalled,
                                                        threadCpuMs(thread) - cpuAtReport, same ? "unchanged" : "changed"));
                        if (!same) logStack(category, "resample", again);
                        Logger::instance().flush();
                    }
                    if (thread) CloseHandle(thread);
                    ++samples;
                    resampleAt = now + 20000;
                }
                continue;
            }
            if (!active || stalled < kReportMs) continue;
            DWORD id = 0;
            const HANDLE thread = duplicateOwner(id);
            first.count = 0;
            const bool sampled = thread && sampleThread(thread, first);
            cpuAtReport = thread ? threadCpuMs(thread) : 0;
            if (thread) CloseHandle(thread);
            const auto stillBlocked = [&] {
                return (captureDriver || armed.load()) && lastBeat.load() == beatAt
                    && captureStage.load() == phase && session.load() == sessionId;
            };
            if (!stillBlocked()) continue;
            reporting = true;
            beatAtReport = beatAt;
            reportedPhase = phase;
            reportedSession = sessionId;
            const auto category = captureDriver ? "capture-driver-hang" : "engine-hang";
            if (captureDriver) {
                log::warn(category, std::format("owner={} stalledMs={} phase={} cpuMs={} stackFrames={}",
                                               id, stalled, phaseName(phase), cpuAtReport, sampled ? first.count : 0));
            } else {
                const auto lastStage = stage.load();
                log::error(category, std::format("owner={} stalledMs={} lastStage={} cpuMs={} stackFrames={} nvidiaOverlay={} rtss={} gamePP={}",
                                                id, stalled, lastStage ? lastStage : "none", cpuAtReport, sampled ? first.count : 0,
                                                gfx::nvidiaOverlayRunning(), gfx::rivaTunerRunning(), gfx::gamePPRunning() || gfx::gamePPInjected()));
            }
            if (stillBlocked()) tell(stalled, captureDriver);
            notifiedAt = now;
            if (sampled) logStack(category, "stack", first);
            Logger::instance().flush();
            if (captureDriver) {
                if (dumpSession != sessionId) {
                    dumpSession = sessionId;
                    captureDumps.clear();
                }
                const bool duplicate = std::ranges::any_of(captureDumps, [&](const CaptureDumpSignature& entry) {
                    return entry.phase == phase && sameStack(entry.stack, first);
                });
                if (duplicate || captureDumps.size() >= kMaxCaptureDumpsPerSession) {
                    log::info(category, duplicate ? "minidump skipped: duplicate phase and stack" : "minidump skipped: session limit");
                } else if (writeDump(category, true)) {
                    captureDumps.push_back({phase, first});
                }
            } else {
                writeDump(category, false);
            }
            Logger::instance().flush();
            samples = 1;
            resampleAt = now + 10000;
        }
        std::lock_guard lock(mutex);
        exited = true;
        wake.notify_all();
    }
};

EngineWatchdog::EngineWatchdog(Notify notify) : state_(std::make_shared<State>()) {
    state_->notify = std::move(notify);
    std::thread([state = state_] { state->watch(); }).detach();
}

EngineWatchdog::~EngineWatchdog() {
    { std::lock_guard lock(state_->notifyMutex); state_->notify = nullptr; }
    std::unique_lock lock(state_->mutex);
    state_->stopping = true;
    state_->wake.notify_all();
    // A dump in progress may take a moment; never hold shutdown on it (the
    // detached thread keeps its own reference to the state).
    state_->wake.wait_for(lock, std::chrono::seconds(2), [&] { return state_->exited; });
}

void EngineWatchdog::beat() {
    auto& s = *state_;
    s.touchOwner();
    s.lastBeat.store(GetTickCount64(), std::memory_order_relaxed);
    s.armed.store(true, std::memory_order_release);
}

void EngineWatchdog::beginSession() {
    state_->lastBeat = GetTickCount64();
    ++state_->session;
}

void EngineWatchdog::setCaptureDriverStage(CaptureDriverStage phase) {
    auto& s = *state_;
    if (phase != CaptureDriverStage::None) s.touchOwner();
    s.lastBeat = GetTickCount64();
    s.captureStage = phase;
}

void EngineWatchdog::disarm() { state_->armed.store(false, std::memory_order_release); }
std::atomic<const char*>* EngineWatchdog::stage() { return &state_->stage; }
bool EngineWatchdog::hung() const { return state_->reporting.load(); }

void EngineWatchdog::setDumpDirectory(const std::wstring& directory) {
    std::lock_guard lock(g_dumpMutex);
    g_dumpDirectory = directory;
}
}
