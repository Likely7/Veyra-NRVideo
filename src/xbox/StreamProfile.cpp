// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/xbox/StreamProfile.h"

#include <charconv>
#include <string>

namespace veyra::xbox {

std::string deviceInfoForTier(QualityTier tier) {
    // The reference clients identify as the xbox.com browser player, not the native
    // GamingApp: the service picks its encoder policy from this identity. Matching
    // it byte-for-byte (www.xbox.com / browser / chrome) is what makes the console
    // deliver the same stream it gives XStreaming. os.name follows the quality tier.
    const char* name = osNameForTier(tier);
    const int w = tier == QualityTier::P720 ? 1280 : 1920;
    const int h = tier == QualityTier::P720 ? 720 : 1080;
    std::string s = R"({"appInfo":{"env":{"clientAppId":"www.xbox.com","clientAppType":"browser","clientAppVersion":"26.1.97","clientSdkVersion":"10.3.7","httpEnvironment":"prod","sdkInstallId":""}},"dev":{"hw":{"make":"Microsoft","model":"unknown","sdktype":"web"},"os":{"name":")";
    s += name;
    s += R"(","ver":"22631.2715","platform":"desktop"},"displayInfo":{"dimensions":{"widthInPixels":)";
    s += std::to_string(w);
    s += R"(,"heightInPixels":)";
    s += std::to_string(h);
    s += R"(},"pixelDensity":{"dpiX":1,"dpiY":1}},"browser":{"browserName":"chrome","browserVersion":"130.0"}}})";
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