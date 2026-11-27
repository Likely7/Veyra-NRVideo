#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace veyra::ui {

// The encoded address remains exact for playback, including user info and
// signed queries. Only its DPAPI representation is written to either store.
class ProtectedNetworkAddress {
public:
    static std::optional<ProtectedNetworkAddress> protect(std::wstring_view address);
    static std::optional<ProtectedNetworkAddress> restore(std::string_view storage);
    const std::wstring& address() const { return address_; }
    const std::string& storage() const { return storage_; }

private:
    ProtectedNetworkAddress(std::wstring address, std::string storage);
    std::wstring address_;
    std::string storage_;
};

}
