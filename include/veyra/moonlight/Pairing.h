// SPDX-License-Identifier: GPL-3.0-only
// The GameStream pairing handshake (ported from moonlight-qt's NvPairingManager).
//
// The client shows a four digit PIN that the user types into the host (for
// Sunshine: its web UI). Both sides derive an AES key from the salted PIN,
// exchange and verify challenges, and swap certificates; the client keeps the
// host's certificate and pins it for every later HTTPS request.
#pragma once
#include <atomic>
#include <string>

#include "veyra/moonlight/Client.h"

namespace veyra::moonlight {

enum class PairResult {
    Paired,
    Failed,
    PinWrong,
    AlreadyInProgress,     // the host is busy pairing another client
    Cancelled,
};

struct PairOutcome {
    PairResult result = PairResult::Failed;
    std::string serverCertPem;   // valid only when Paired
    std::string detail;          // which step failed, for diagnostics (no secrets)
};

// Four digits, zero padded, from the system random source.
std::string generatePin();

// `client` must already know the host's HTTPS port (call serverInfo() first) and
// is not modified: the handshake works on a copy. Blocks until the user has
// entered the PIN on the host (or `cancel` is set).
PairOutcome pairWithHost(const ServerClient& client, const Identity& identity, const ServerInfo& host,
                         const std::string& pin, const std::atomic<bool>* cancel);

} // namespace veyra::moonlight
