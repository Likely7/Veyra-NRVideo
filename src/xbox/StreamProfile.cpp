// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/xbox/StreamProfile.h"

#include <charconv>
#include <string>

namespace veyra::xbox {

std::string deviceInfoForTier(QualityTier tier) {
    // The P1080 string is the historical kDeviceInfo constant, byte for byte. The other tiers
    // change only dev.os.name and the display dimensions, as the reference clients do.
    const char* name = tier == QualityTier::P1080 ? "Windows 11" : osNameForTier(tier);
    const int w = tier == QualityTier::P1080 ? 1920 : 4096;
    const int h = tier == QualityTier::P1080 ? 1080 : 2160;
    std::string s = R"({"appInfo":{"env":{"clientAppId":"Microsoft.GamingApp","clientAppType":"native","clientAppVersion":"2203.1001.4.0","clientSdkVersion":"8.5.2","httpEnvironment":"prod","sdkInstallId":""}},"dev":{"hw":{"make":"Microsoft","model":"Surface Pro","sdktype":"native"},"os":{"name":")";
    s += name;
    s += R"(","ver":"22631.2715","platform":"desktop"},"displayInfo":{"dimensions":{"widthInPixels":)";
    s += std::to_string(w);
    s += R"(,"heightInPixels":)";
    s += std::to_string(h);
    s += R"(},"pixelDensity":{"dpiX":1,"dpiY":1}}}})";
    return s;
}

bool parseIceServerUrl(const std::string& url, std::string* host, uint16_t* port, bool* turn) {
    const auto colon = url.find(':');
    if (colon == std::string::npos) return false;
    const std::string scheme = url.substr(0, colon);
    if (scheme != "stun" && scheme != "turn") return false;
    std::string rest = url.substr(colon + 1);
    // Strip any transport query the reference URLs never carry; keep it simple and strict.
    std::string h;
    uint16_t p = 3478;
    if (!rest.empty() && rest.front() == '[') {
        const auto close = rest.find(']');
        if (close == std::string::npos) return false;
        h = rest.substr(1, close - 1);
        rest = rest.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return false;
            rest = rest.substr(1);
        }
    } else {
        const auto portColon = rest.rfind(':');
        if (portColon != std::string::npos) {
            h = rest.substr(0, portColon);
            rest = rest.substr(portColon + 1);
        } else {
            h = rest;
            rest.clear();
        }
    }
    if (!rest.empty()) {
        unsigned long value = 0;
        const auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
        if (ec != std::errc{} || ptr != rest.data() + rest.size() || value == 0 || value > 65535) return false;
        p = uint16_t(value);
    }
    if (h.empty()) return false;
    if (host) *host = h;
    if (port) *port = p;
    if (turn) *turn = scheme == "turn";
    return true;
}

} // namespace veyra::xbox