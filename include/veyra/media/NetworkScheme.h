#pragma once
#include <string>
#include <string_view>
namespace veyra::media {
// Scheme test only. Kept out of NetworkInput.h so playlist and recent-file
// code can recognise an address without including FFmpeg.
inline bool startsWithScheme(std::wstring_view text, std::wstring_view scheme) {
    if (text.size() < scheme.size()) return false;
    for (size_t i = 0; i < scheme.size(); ++i) {
        wchar_t ch = text[i];
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
        if (ch != scheme[i]) return false;
    }
    return true;
}
inline bool datagramMediaInput(std::wstring_view path) {
    return startsWithScheme(path, L"rtp:") || startsWithScheme(path, L"udp:");
}
inline bool networkMediaInput(std::wstring_view path) {
    const auto colon = path.find(L':');
    if (colon == std::wstring_view::npos || colon > 5) return false;
    std::wstring scheme(path.substr(0, colon));
    for (auto& ch : scheme) if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
    return scheme == L"http" || scheme == L"https" || scheme == L"rtsp" || datagramMediaInput(path);
}
}
