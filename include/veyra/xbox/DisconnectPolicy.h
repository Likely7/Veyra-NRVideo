// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>
#include <cstdint>

namespace veyra::xbox {
// Only explicitly recoverable transport/service losses. A takeover, sign-out,
// console shutdown or an unknown server kick must stay visible to the user.
constexpr bool reconnectableDisconnect(std::string_view reason) {
    return reason == "connection lost" || reason == "KickForServerShutdown" || reason == "session expired";
}
class ReconnectBudget {
public:
    bool take() { if (attempts_ >= 3) return false; ++attempts_; return true; }
    unsigned attempts() const { return attempts_; }
    // Refill only after 30 seconds of continuous decoded progress. One frame
    // after a reconnect must not enable another endless cycle of retries.
    void decoded(int64_t host100ns) {
        if (!progressStart_ || host100ns - lastProgress_ > 10000000 || host100ns < lastProgress_) progressStart_ = host100ns;
        lastProgress_ = host100ns;
        if (host100ns - progressStart_ >= 300000000) attempts_ = 0;
    }
    void disconnected() { progressStart_ = lastProgress_ = 0; }
private:
    unsigned attempts_ = 0;
    int64_t progressStart_ = 0, lastProgress_ = 0;
};
}
