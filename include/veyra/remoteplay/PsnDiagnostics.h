// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace veyra::remoteplay {
struct PsnDiagnosticEvent { unsigned event=0,state=0,http=0; };
// Accept only our complete numeric marker. Server strings, IDs and JSON are
// never returned or retained, even if they contain a marker as a substring.
inline std::optional<PsnDiagnosticEvent> parsePsnDiagnostic(std::string_view text) noexcept {
    const auto field=[&](std::string_view prefix,unsigned& value) {
        if(!text.starts_with(prefix))return false;
        text.remove_prefix(prefix.size());
        const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
        if(error!=std::errc{})return false;
        text.remove_prefix(static_cast<std::size_t>(end-text.data()));
        return true;
    };
    PsnDiagnosticEvent out;
    if(!field("VEYRA_PSN_DIAG event=",out.event)||!field(" state=",out.state)||!field(" http=",out.http)||!text.empty())return {};
    if(out.event<1||out.event>10||out.state>0x7ffff||(out.http!=0&&(out.http<100||out.http>599)))return {};
    return out;
}
struct PsnWebsocketError { unsigned operation=0,curl=0; };
inline std::optional<PsnWebsocketError> parsePsnWebsocketError(std::string_view text) noexcept {
    constexpr std::string_view prefix="VEYRA_PSN_WS op=",separator=" curl=";
    if(!text.starts_with(prefix))return {};
    text.remove_prefix(prefix.size());
    PsnWebsocketError out;
    const auto [opEnd,opError]=std::from_chars(text.data(),text.data()+text.size(),out.operation);
    if(opError!=std::errc{})return {};
    text.remove_prefix(static_cast<std::size_t>(opEnd-text.data()));
    if(!text.starts_with(separator))return {};
    text.remove_prefix(separator.size());
    const auto [curlEnd,curlError]=std::from_chars(text.data(),text.data()+text.size(),out.curl);
    if(curlError!=std::errc{}||curlEnd!=text.data()+text.size())return {};
    if(out.operation<1||out.operation>6||out.curl<1||out.curl>999)return {};
    return out;
}
}
