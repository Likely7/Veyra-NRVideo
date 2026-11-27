#include "veyra/ui/NetworkHistory.h"

#include "veyra/media/NetworkScheme.h"

#include <windows.h>
#include <wincrypt.h>
#include <cstring>
#include <utility>
#include <vector>

namespace veyra::ui {
namespace {

constexpr size_t maxAddressUnits = 32 * 1024;
constexpr size_t maxStorageBytes = 128 * 1024;

bool validAddress(std::wstring_view address) {
    return address.size() <= maxAddressUnits && address.find(L'\0') == std::wstring_view::npos &&
           media::networkMediaInput(address);
}

struct ProtectedBlob {
    DATA_BLOB data{};
    ~ProtectedBlob() {
        if (data.pbData) {
            SecureZeroMemory(data.pbData, data.cbData);
            LocalFree(data.pbData);
        }
    }
};

}

ProtectedNetworkAddress::ProtectedNetworkAddress(std::wstring address, std::string storage)
    : address_(std::move(address)), storage_(std::move(storage)) {}

std::optional<ProtectedNetworkAddress> ProtectedNetworkAddress::protect(std::wstring_view address) {
    if (!validAddress(address)) return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(address.size() * sizeof(wchar_t)),
                    reinterpret_cast<BYTE*>(const_cast<wchar_t*>(address.data()))};
    ProtectedBlob protectedBytes;
    if (!CryptProtectData(&input, L"Veyra network playback history", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &protectedBytes.data)) return std::nullopt;

    constexpr DWORD format = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    DWORD length = 0;
    const auto& blob = protectedBytes.data;
    if (!CryptBinaryToStringA(blob.pbData, blob.cbData, format, nullptr, &length) || length > maxStorageBytes)
        return std::nullopt;
    std::string storage(length, '\0');
    if (!CryptBinaryToStringA(blob.pbData, blob.cbData, format, storage.data(), &length)) return std::nullopt;
    storage.resize(length);
    return ProtectedNetworkAddress(std::wstring(address), std::move(storage));
}

std::optional<ProtectedNetworkAddress> ProtectedNetworkAddress::restore(std::string_view storage) {
    if (storage.empty() || storage.size() > maxStorageBytes) return std::nullopt;
    constexpr DWORD format = CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT;
    const auto size = static_cast<DWORD>(storage.size());
    DWORD length = 0;
    if (!CryptStringToBinaryA(storage.data(), size, format, nullptr, &length, nullptr, nullptr) || length == 0)
        return std::nullopt;
    std::vector<BYTE> bytes(length);
    if (!CryptStringToBinaryA(storage.data(), size, format, bytes.data(), &length, nullptr, nullptr))
        return std::nullopt;

    DATA_BLOB input{length, bytes.data()};
    ProtectedBlob plaintext;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plaintext.data)) return std::nullopt;
    const auto& blob = plaintext.data;
    const auto units = blob.cbData / sizeof(wchar_t);
    if (units == 0 || units > maxAddressUnits || blob.cbData % sizeof(wchar_t) != 0) return std::nullopt;
    std::wstring address(units, L'\0');
    std::memcpy(address.data(), blob.pbData, blob.cbData);
    if (!validAddress(address)) return std::nullopt;
    return ProtectedNetworkAddress(std::move(address), std::string(storage));
}

}
