// SPDX-License-Identifier: GPL-3.0-only
// The GameStream host speaks plain HTTP on one port and HTTPS (mutual TLS, with
// the client certificate from pairing and a self-signed server certificate that
// is pinned at pairing time) on another. This is the small client for both:
// Winsock + OpenSSL, blocking with a deadline and a cancel flag, one request per
// connection (the host does not cope with persistent connections).
#pragma once
#include <atomic>
#include <string>

#include "veyra/moonlight/Crypto.h"
#include "veyra/moonlight/Types.h"

namespace veyra::moonlight {

struct HttpTarget {
    std::string host;
    uint16_t port = 0;
    bool tls = false;
};

struct HttpOptions {
    int timeoutMs = 5000;                         // 0: no deadline, only `cancel` ends the wait
    const std::atomic<bool>* cancel = nullptr;
    size_t maxBodyBytes = size_t(8) << 20;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

class HttpClient {
public:
    // `identity` supplies the TLS client certificate; `pinnedServerCertPem` is the
    // only server certificate accepted over TLS. HTTPS without both is refused.
    HttpClient(const Identity* identity, std::string pinnedServerCertPem);

    // GET path?query. Throws TransportError. A TLS failure (handshake, pinned
    // certificate mismatch, rejected client certificate) has Kind::Tls.
    HttpResponse get(const HttpTarget& target, const std::string& pathAndQuery, const HttpOptions& options) const;

    void setPinnedServerCertificate(std::string pem) { pinned_ = std::move(pem); }
    const std::string& pinnedServerCertificate() const { return pinned_; }

private:
    const Identity* identity_;
    std::string pinned_;
};

} // namespace veyra::moonlight
