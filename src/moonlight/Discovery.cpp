// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/Discovery.h"

// windns.h picks the wide record layout under UNICODE; the DNS-SD callbacks deliver wide names.
#ifndef UNICODE
#define UNICODE
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace veyra::moonlight {
namespace {

std::string narrow(const wchar_t* text) {
    if (!text) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string out(size_t(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
    return out;
}

} // namespace

struct Discovery::State {
    struct Job {
        std::wstring name;
        DNS_SERVICE_CANCEL cancel{};
    };
    Callback callback;
    std::mutex mutex;
    bool stopped = false;
    bool browsing = false;
    DNS_SERVICE_CANCEL browseCancel{};
    std::set<std::wstring> seen;
    std::vector<std::unique_ptr<Job>> jobs;
};

namespace {

// Callbacks carry a raw key; the registry maps it back to a live State, so a late callback after
// stop() finds nothing and does nothing.
std::mutex g_registryMutex;
std::map<const void*, std::weak_ptr<Discovery::State>> g_registry;

std::shared_ptr<Discovery::State> lookup(const void* key) {
    std::lock_guard lock(g_registryMutex);
    const auto it = g_registry.find(key);
    return it == g_registry.end() ? nullptr : it->second.lock();
}

VOID WINAPI resolved(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance) {
    auto state = lookup(context);
    if (!state) {
        if (instance) DnsServiceFreeInstance(instance);
        return;
    }
    if (status == ERROR_SUCCESS && instance) {
        DiscoveredHost host;
        host.instance = narrow(instance->pszInstanceName);
        host.hostName = narrow(instance->pszHostName);
        host.port = 47989;   // the HTTP port; the advertised port is the same on Sunshine, and serverinfo corrects the rest
        if (instance->wPort != 0) host.port = instance->wPort;
        if (instance->ip4Address) {
            char text[INET_ADDRSTRLEN] = {};
            IN_ADDR address{};
            address.S_un.S_addr = *instance->ip4Address;
            if (InetNtopA(AF_INET, &address, text, sizeof(text))) host.address = text;
        }
        if (host.address.empty()) host.address = host.hostName;
        if (!host.address.empty()) {
            bool stopped;
            { std::lock_guard lock(state->mutex); stopped = state->stopped; }
            if (!stopped && state->callback) state->callback(host);
        }
    }
    if (instance) DnsServiceFreeInstance(instance);
}

VOID WINAPI browsed(DWORD status, PVOID context, PDNS_RECORD records) {
    auto state = lookup(context);
    if (state && status == ERROR_SUCCESS) {
        for (PDNS_RECORD record = records; record; record = record->pNext) {
            if (record->wType != DNS_TYPE_PTR || !record->Data.PTR.pNameHost) continue;
            const std::wstring name = record->Data.PTR.pNameHost;
            std::lock_guard lock(state->mutex);
            if (state->stopped || !state->seen.insert(name).second) continue;
            auto job = std::make_unique<Discovery::State::Job>();
            job->name = name;
            DNS_SERVICE_RESOLVE_REQUEST request{};
            request.Version = DNS_QUERY_REQUEST_VERSION1;
            request.InterfaceIndex = 0;
            request.QueryName = job->name.data();
            request.pResolveCompletionCallback = resolved;
            request.pQueryContext = state.get();   // keyed by the State: one registry entry covers every resolve
            const DNS_STATUS result = DnsServiceResolve(&request, &job->cancel);
            if (result != DNS_REQUEST_PENDING) {
                state->seen.erase(name);   // try again when it is announced next
                continue;
            }
            state->jobs.push_back(std::move(job));
        }
    }
    if (records) DnsRecordListFree(records, DnsFreeRecordList);
}

} // namespace

Discovery::Discovery() = default;
Discovery::~Discovery() { stop(); }

bool Discovery::start(Callback callback) {
    stop();
    auto state = std::make_shared<State>();
    state->callback = std::move(callback);
    {
        std::lock_guard lock(g_registryMutex);
        g_registry[state.get()] = state;
    }
    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = L"_nvstream._tcp.local";
    request.pBrowseCallback = browsed;
    request.pQueryContext = state.get();
    const DNS_STATUS result = DnsServiceBrowse(&request, &state->browseCancel);
    if (result != DNS_REQUEST_PENDING) {
        std::lock_guard lock(g_registryMutex);
        g_registry.erase(state.get());
        return false;
    }
    state->browsing = true;
    state_ = std::move(state);
    return true;
}

void Discovery::stop() {
    auto state = std::move(state_);
    if (!state) return;
    {
        std::lock_guard lock(g_registryMutex);
        g_registry.erase(state.get());   // late callbacks now find nothing
    }
    std::vector<std::unique_ptr<State::Job>> jobs;
    bool browsing;
    {
        std::lock_guard lock(state->mutex);
        state->stopped = true;
        browsing = state->browsing;
        state->browsing = false;
        jobs = std::move(state->jobs);
    }
    // Cancelling can wait for a callback that is running; it must not hold the lock that callback takes.
    if (browsing) DnsServiceBrowseCancel(&state->browseCancel);
    for (auto& job : jobs) DnsServiceResolveCancel(&job->cancel);
}

bool Discovery::running() const { return state_ != nullptr; }

} // namespace veyra::moonlight
