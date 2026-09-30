// SPDX-License-Identifier: GPL-3.0-only
// Client identity and the small set of OpenSSL operations the GameStream
// pairing handshake needs (ported from moonlight-qt identitymanager/nvpairingmanager).
#pragma once
#include <string>
#include <string_view>

namespace veyra::moonlight {

// The client's long-lived identity: a self-signed RSA-2048 certificate (CN
// "NVIDIA GameStream Client", 20 years) and its key, both PEM, plus the
// unique id that the host knows this client by.
struct Identity {
    std::string certPem;
    std::string keyPem;               // secret: never logged, stored with DPAPI
    std::string uniqueId;             // 16 lowercase hex characters

    // Both PEMs parse, belong together, and the id is 16 hex characters.
    bool valid() const;
};

Identity createIdentity();

namespace crypto {

std::string randomBytes(size_t count);

// Raw digest: SHA-256 for GameStream generation 7 and later, SHA-1 before.
std::string digest(std::string_view data, bool sha256);

// AES-128-ECB without padding; input length must be a multiple of 16, else empty.
std::string aesEcbEncrypt(std::string_view plain, std::string_view key);
std::string aesEcbDecrypt(std::string_view cipher, std::string_view key);

// RSA-SHA256 signature of `message` with the identity's key.
std::string sign(const Identity& identity, std::string_view message);
// Verifies an RSA-SHA256 signature against the public key in `certPem`.
bool verify(std::string_view message, std::string_view signature, std::string_view certPem);

// The certificate's own signature bits (what the pairing hashes mix in).
std::string certificateSignature(std::string_view certPem);

// DER form, used to pin a server certificate; empty when the PEM does not parse.
std::string certificateDer(std::string_view certPem);

} // namespace crypto
} // namespace veyra::moonlight
