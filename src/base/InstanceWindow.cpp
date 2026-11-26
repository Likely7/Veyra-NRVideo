#include "veyra/base/InstanceWindow.h"

#include "veyra/base/PortableFiles.h"
#include <tlhelp32.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace veyra::instance {
namespace {

using ProcessHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
using Path = std::filesystem::path;
constexpr auto ownerName = L".veyra-instance.v1";
constexpr auto endingName = L".veyra-exit-request.v1";

uint64_t fileTime(const FILETIME& value) {
    return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

uint64_t processStart(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    return GetProcessTimes(process, &created, &exited, &kernel, &user) ? fileTime(created) : 0;
}

std::string identityText(DWORD pid, uint64_t created) {
    return std::to_string(pid) + " " + std::to_string(created);
}

std::string utf8Path(const Path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::optional<DWORD> threadCount(DWORD pid) {
    const HANDLE raw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (raw == INVALID_HANDLE_VALUE) return {};
    const ProcessHandle snapshot(raw, CloseHandle);
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot.get(), &entry); ok; ok = Process32NextW(snapshot.get(), &entry)) {
        if (entry.th32ProcessID == pid) return entry.cntThreads;
    }
    return {};
}

bool runningExecutable(HANDLE process, const std::filesystem::path& expected) {
    if (expected.empty()) return false;
    std::wstring image(32768, L'\0');
    DWORD size = DWORD(image.size());
    return QueryFullProcessImageNameW(process, 0, image.data(), &size)
        && CompareStringOrdinal(expected.c_str(), int(expected.native().size()),
                                image.data(), int(size), TRUE) == CSTR_EQUAL;
}

struct Owner {
    DWORD pid = 0;
    uint64_t created = 0;
    bool windowReady = false;
    Path logs;
    ProcessHandle process{nullptr, CloseHandle};
};

std::optional<Owner> liveOwner(const Path& directory) {
    // The stamp survives the UI thread and the mutex. A reused PID cannot
    // carry ownership into a new process.
    const auto stamp = readOwnerStamp(directory);
    if (!stamp) return {};
    Owner owner;
    owner.pid = stamp->pid;
    owner.created = stamp->created;
    owner.windowReady = stamp->windowReady;
    owner.logs = stamp->logs;
    owner.process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, owner.pid));
    if (!owner.process || processStart(owner.process.get()) != owner.created
        || WaitForSingleObject(owner.process.get(), 0) != WAIT_TIMEOUT) return {};
    return owner;
}

bool alreadyEnding(const Path& directory, DWORD pid, uint64_t created) {
    const auto file = directory / endingName;
    return created && portable::plainPath(file, false)
        && portable::readSmallFile(file) == identityText(pid, created);
}

} // namespace

ProcessExit waitForExit(HANDLE process, bool endRequested) {
    const DWORD result = WaitForSingleObject(process, endRequested ? 5000 : 0);
    if (result == WAIT_OBJECT_0) return ProcessExit::Exited;
    if (result == WAIT_TIMEOUT && endRequested) return ProcessExit::Pending;
    return ProcessExit::Failed;
}

Profile::Profile(Path directory, Path logDirectory)
    : directory_(std::move(directory)), logDirectory_(std::move(logDirectory)), key_(profileKey(directory_)) {}

bool Profile::rememberOwner(bool windowReady) const {
    if (key_.empty()) return false;
    const auto created = processStart(GetCurrentProcess());
    if (!created) return false;
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    if (ec) return false;
    const auto logs = std::filesystem::absolute(logDirectory_, ec).lexically_normal();
    return portable::writeSmallFile(directory_ / ownerName,
        identityText(GetCurrentProcessId(), created) + (windowReady ? "\n1\n" : "\n0\n")
        + (ec ? std::string{} : utf8Path(logs)));
}

void Profile::clearOwner() const {
    clearOwnerStamp(directory_);
}

bool Profile::ownerAlive() const {
    return liveOwner(directory_).has_value();
}

bool Profile::reportStalledOwner(const Notify& notify) const {
    const auto owner = liveOwner(directory_);
    if (!owner) return false;
    const auto count = threadCount(owner->pid);
    if ((!owner->windowReady || !count || *count > 1) && !alreadyEnding(directory_, owner->pid, owner->created))
        return false;
    if (WaitForSingleObject(owner->process.get(), 0) != WAIT_TIMEOUT) return false;
    notify(Notice::EndPending, owner->pid,
           newestHangDump(owner->logs.empty() ? logDirectory_ : owner->logs, owner->pid, owner->created));
    return true;
}

Handoff Profile::handoff(HWND window, const std::wstring& target, const Path& executable, const Notify& notify) const {
    DWORD pid = 0;
    if (!window || !GetWindowThreadProcessId(window, &pid) || !GetPropW(window, key_.c_str()))
        return Handoff::NoWindow;
    const auto sameWindow = [&] {
        DWORD current = 0;
        GetWindowThreadProcessId(window, &current);
        return current == pid && GetPropW(window, key_.c_str());
    };
    const ProcessHandle observed(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid), CloseHandle);
    const auto created = observed ? processStart(observed.get()) : 0;
    const auto tell = [&](Notice notice) {
        const auto known = liveOwner(directory_);
        const auto& logs = known && known->pid == pid && known->created == created && !known->logs.empty()
            ? known->logs : logDirectory_;
        notify(notice, pid, newestHangDump(logs, pid, created));
    };
    if (alreadyEnding(directory_, pid, created) && WaitForSingleObject(observed.get(), 0) == WAIT_TIMEOUT) {
        tell(Notice::EndPending);
        return Handoff::Done;
    }
    const auto threadTotal = threadCount(pid);
    const bool lowThreadTimeout = threadTotal && *threadTotal <= 1 && timedOut(window, 3000);
    if (!lowThreadTimeout && sameWindow() && forwardLaunch(window, pid, target)) return Handoff::Done;
    if (!sameWindow()) {
        return reportStalledOwner(notify) ? Handoff::Done : Handoff::NoWindow;
    }
    if (!lowThreadTimeout && !timedOut(window)) {
        if (!sameWindow()) return Handoff::NoWindow;
        notify(Notice::AlreadyOpen, 0, {});
        return Handoff::Done;
    }
    const auto remaining = threadCount(pid);
    if ((lowThreadTimeout || (remaining && *remaining <= 1))
        && observed && WaitForSingleObject(observed.get(), 0) == WAIT_TIMEOUT) {
        tell(Notice::EndPending);
        return Handoff::Done;
    }
    ProcessHandle owner(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                    FALSE, pid), CloseHandle);
    if (!owner || !created || processStart(owner.get()) != created || !runningExecutable(owner.get(), executable)) {
        tell(Notice::Hung);
        return Handoff::Done;
    }
    // A concurrent launcher must not ask about or end the same process twice.
    const ProcessHandle recovery(CreateMutexW(nullptr, FALSE, (key_ + L".Recovery").c_str()), CloseHandle);
    if (!recovery) {
        tell(Notice::Hung);
        return Handoff::Done;
    }
    const DWORD acquired = WaitForSingleObject(recovery.get(), 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
        notify(Notice::StartingOrClosing, 0, {});
        return Handoff::Done;
    }
    struct Release { HANDLE lock; ~Release() { ReleaseMutex(lock); } } unlock{recovery.get()};
    if (alreadyEnding(directory_, pid, created)) {
        tell(Notice::EndPending);
        return Handoff::Done;
    }
    if (!notify(Notice::ConfirmEnd, pid, {})) return Handoff::Done;
    if (WaitForSingleObject(owner.get(), 0) == WAIT_OBJECT_0) return Handoff::Released;
    if (sameWindow() && timedOut(window) && sameWindow()) {
        const bool requested = TerminateProcess(owner.get(), 0) != FALSE;
        switch (waitForExit(owner.get(), requested)) {
        case ProcessExit::Exited: return Handoff::Released;
        case ProcessExit::Pending:
            portable::writeSmallFile(directory_ / endingName, identityText(pid, created));
            tell(Notice::EndPending);
            break;
        case ProcessExit::Failed: tell(Notice::EndFailed); break;
        }
        return Handoff::Done;
    }
    if (!sameWindow()) return Handoff::NoWindow;
    if (forwardLaunch(window, pid, target)) return Handoff::Done;
    tell(Notice::Hung);
    return Handoff::Done;
}

} // namespace veyra::instance
