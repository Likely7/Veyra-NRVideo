// SPDX-License-Identifier: GPL-3.0-only
// Finds Sunshine / GameStream hosts on the local network: they advertise "_nvstream._tcp" over
// mDNS / DNS-SD. Uses the Windows DNS-SD API (dnsapi, Windows 10 2004+), so nothing is added to the
// build. Found hosts are only candidates: the caller asks each one for serverinfo before trusting it.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace veyra::moonlight {

struct DiscoveredHost {
    std::string instance;   // "Sunshine-PC._nvstream._tcp.local"
    std::string hostName;   // "DESKTOP-ABC.local"
    std::string address;    // dotted IPv4 (or the host name when no address came back)
    uint16_t port = 47989;
};

class Discovery {
public:
    // Called on a system thread pool thread, possibly more than once per host.
    using Callback = std::function<void(const DiscoveredHost&)>;

    Discovery();
    ~Discovery();
    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    // False when the DNS-SD API is unavailable or refused the browse; the caller then relies on
    // manual addresses.
    bool start(Callback callback);
    void stop();
    bool running() const;

    struct State;   // opaque; the DNS callbacks in the .cpp need the name

private:
    std::shared_ptr<State> state_;
};

} // namespace veyra::moonlight
