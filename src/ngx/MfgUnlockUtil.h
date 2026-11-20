#pragma once
// Parsing helpers shared by the RTX 40 (AdaMfgUnlock) and RTX 30 (AmpereMfgUnlock)
// multi-frame unlock ports, kept in one place so a bounds fix reaches both.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra::ngx {

inline uint16_t readU16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
inline uint32_t readU32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint64_t readU64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

// Uppercase SHA-256 comparison via the platform BCrypt (no new dependency;
// veyra_base already links bcrypt). Used for the identity gates ported from
// the upstream midpoint_fix profiles.
bool sha256Equals(const uint8_t* bytes, size_t count, const char* expected);

// Plain LZ4 block format, as emitted by the CUDA fatbin packer.
bool lz4BlockDecompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);

// True when `value` is the address of the NUL-terminated `expected` inside the image.
bool pointsToCString(const uint8_t* base, size_t imageSize, uint64_t value, const char* expected);

} // namespace veyra::ngx
