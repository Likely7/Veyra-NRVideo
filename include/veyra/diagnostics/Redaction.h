#pragma once
#include <regex>
#include <string>
namespace veyra::diagnostics {
inline std::string redact(std::string s){
    // Fail closed on unknown path formats: remove from a Windows absolute
    // path to end of line, even when the path contains spaces.
    s=std::regex_replace(s,std::regex(R"([A-Za-z]:[\\/][^\r\n]*)"),"[本地路径已隐藏]");
    s=std::regex_replace(s,std::regex(R"(\\\\[^\r\n]*)"),"[设备或网络路径已隐藏]");
    s=std::regex_replace(s,std::regex(R"((serial|serialnumber|devicepath|username|user)[=: ]+[^\s,;]+)",std::regex::icase),"$1=[已隐藏]");
    // Credentials that network errors can echo: auth/cookie headers to end of line,
    // bearer tokens, and secret-looking query parameters of a URL.
    s=std::regex_replace(s,std::regex(R"((authorization|proxy-authorization|cookie|set-cookie|x-api-key|api-key)[=: ]+[^\r\n]*)",std::regex::icase),"$1=[已隐藏]");
    s=std::regex_replace(s,std::regex(R"((bearer)\s+[A-Za-z0-9._~+/=-]+)",std::regex::icase),"$1 [已隐藏]");
    s=std::regex_replace(s,std::regex(R"(([?&](?:access_token|refresh_token|id_token|token|key|api_key|apikey|sig|signature|auth|password|secret)=)[^&#\s]+)",std::regex::icase),"$1[已隐藏]");
    return s;
}
}
