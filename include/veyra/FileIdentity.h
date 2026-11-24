#pragma once

#include <cstdint>
#include <string>

namespace veyra {

struct FileIdentity {
    std::wstring path;
    uint64_t sizeBytes = 0;
    std::string sha256Upper;        // 64 uppercase hex characters when computed
    std::wstring fileVersion;       // "310.8.0.0" style, empty when absent
    std::wstring productVersion;
    bool signatureValid = false;
    uint32_t winTrustError = 0;
    std::wstring signerSubject;     // certificate subject when signature valid
    bool signerIsNvidia = false;
};

enum class IdentityError : int32_t {
    None = 0,
    FileOpen = 1,
    Read = 2,
    Hash = 3,
    VersionInfo = 4,
    Signature = 5,
};

const char* identityErrorString(IdentityError error);

// Computes size + SHA-256 + version strings + Authenticode status + signer
// subject for one file. Returns false and fills `error` when a stage fails;
// partial results remain in `out` for logging.
bool computeFileIdentity(const std::wstring& path, FileIdentity& out, IdentityError& error);

// Size and uppercase SHA-256, without version or signature inspection.
bool computeFileSha256(const std::wstring& path, std::string& hex, uint64_t& size);

// Uppercase hex SHA-256 over an in-memory buffer (diagnostics/statistics).
std::string sha256Hex(const uint8_t* data, size_t size);

} // namespace veyra
