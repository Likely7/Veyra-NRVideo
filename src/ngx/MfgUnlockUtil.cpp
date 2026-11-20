#include "MfgUnlockUtil.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <climits>

namespace veyra::ngx {

bool sha256Equals(const uint8_t* bytes, size_t count, const char* expected) {
    if (bytes == nullptr || count == 0 || count > ULONG_MAX || expected == nullptr ||
        std::strlen(expected) != 64) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    std::array<uint8_t, 32> digest{};
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) {
        status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes),
                            static_cast<ULONG>(count), digest.data(),
                            static_cast<ULONG>(digest.size()));
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (status < 0) {
        return false;
    }
    static constexpr char kDigits[] = "0123456789ABCDEF";
    for (size_t index = 0; index < digest.size(); ++index) {
        if (expected[index * 2] != kDigits[digest[index] >> 4] ||
            expected[index * 2 + 1] != kDigits[digest[index] & 0x0F]) {
            return false;
        }
    }
    return true;
}

bool lz4BlockDecompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    size_t in = 0, out = 0;
    while (in < srcSize) {
        const uint8_t token = src[in++];
        size_t literals = token >> 4;
        if (literals == 15) {
            uint8_t ext = 0;
            do {
                if (in >= srcSize) return false;
                ext = src[in++];
                literals += ext;
            } while (ext == 0xFF);
        }
        if (literals > srcSize - in || literals > dstSize - out) return false;
        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;
        if (in == srcSize) break;
        if (srcSize - in < 2) return false;
        const size_t back = size_t(src[in]) | (size_t(src[in + 1]) << 8);
        in += 2;
        if (back == 0 || back > out) return false;
        size_t match = 4 + (token & 0x0F);
        if ((token & 0x0F) == 15) {
            uint8_t ext = 0;
            do {
                if (in >= srcSize) return false;
                ext = src[in++];
                match += ext;
            } while (ext == 0xFF);
        }
        if (match > dstSize - out) return false;
        for (size_t i = 0; i < match; ++i) dst[out + i] = dst[out + i - back];
        out += match;
    }
    return in == srcSize && out == dstSize;
}

bool pointsToCString(const uint8_t* base, size_t imageSize, uint64_t value, const char* expected) {
    const auto start = reinterpret_cast<uintptr_t>(base);
    if (value < start || value >= start + imageSize) return false;
    const size_t len = std::strlen(expected);
    if (value + len + 1 > start + imageSize) return false;
    return std::memcmp(reinterpret_cast<const void*>(value), expected, len + 1) == 0;
}

} // namespace veyra::ngx
