#pragma once

#include <cwctype>
#include <string_view>

namespace veyra::media {

inline bool hasMediaScheme(std::wstring_view address, std::wstring_view scheme) {
    if (address.size() <= scheme.size() || address[scheme.size()] != L':') return false;
    for (size_t i = 0; i < scheme.size(); ++i) {
        if (std::towlower(address[i]) != static_cast<wint_t>(scheme[i])) return false;
    }
    return true;
}

inline bool datagramMediaInput(std::wstring_view address) {
    return hasMediaScheme(address, L"rtp") || hasMediaScheme(address, L"udp");
}

inline bool networkMediaInput(std::wstring_view address) {
    return hasMediaScheme(address, L"http") || hasMediaScheme(address, L"https") ||
           hasMediaScheme(address, L"rtsp") || datagramMediaInput(address);
}

}
