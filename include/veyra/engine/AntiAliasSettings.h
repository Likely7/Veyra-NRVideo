#pragma once
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace veyra::engine {
// Spatial anti-aliasing effect (issue #53). One instance runs at source
// resolution in front of any enlargement; a node chain may hold a second one
// behind super resolution.
inline constexpr uint32_t kMaxAntiAliasInstances = 2;

enum class AntiAliasMethod : uint8_t { Fxaa = 0, Smaa = 1, Mlaa = 2 };
// Experimental temporal accumulation on top of SMAA (captured frames cannot be
// jittered, so these are the "no jitter" approximations of SMAA T2x / 4x).
enum class AntiAliasTemporal : uint8_t { Off = 0, T2x = 1, X4 = 2 };

// Quality tiers per method. FXAA: 0 medium (preset 3), 1 high (4), 2 ultra (5).
// SMAA: 0 low, 1 medium, 2 high, 3 ultra. MLAA has no tiers.
constexpr uint32_t antiAliasQualityCount(AntiAliasMethod method) {
    return method == AntiAliasMethod::Fxaa ? 3u : method == AntiAliasMethod::Smaa ? 4u : 1u;
}
constexpr bool validAntiAliasMethod(AntiAliasMethod method) {
    return method == AntiAliasMethod::Fxaa || method == AntiAliasMethod::Smaa || method == AntiAliasMethod::Mlaa;
}

struct AntiAliasSettings {
    bool enabled = false;
    AntiAliasMethod method = AntiAliasMethod::Smaa;
    uint8_t quality = 2;
    // MLAA only.
    float mlaaThreshold = 0.08f; // perceptual luma difference that counts as an edge, 0.02 .. 0.30
    float mlaaStrength = 1.0f;   // 0 .. 1
    // SMAA only, experimental, default off.
    AntiAliasTemporal temporal = AntiAliasTemporal::Off;
    float historyWeight = 0.5f;    // 0 .. 0.95
    float historyRejection = 8.0f; // 0 .. 32
    bool operator==(const AntiAliasSettings&) const = default;

    // Untouched defaults: the list chain then carries no anti-aliasing node at all, so
    // presets and sessions saved without it keep their older format version.
    bool configured() const { return *this != AntiAliasSettings{}; }
    bool usesMlaaParameters() const { return method == AntiAliasMethod::Mlaa; }
    bool usesTemporal() const { return method == AntiAliasMethod::Smaa && temporal != AntiAliasTemporal::Off; }
    // The values that matter to the running effect: a disabled instance and the
    // parameters of a method that is not selected compare equal to the defaults.
    AntiAliasSettings active() const {
        if (!enabled) return {};
        AntiAliasSettings value = *this;
        const AntiAliasSettings defaults;
        if (!usesMlaaParameters()) { value.mlaaThreshold = defaults.mlaaThreshold; value.mlaaStrength = defaults.mlaaStrength; }
        if (method == AntiAliasMethod::Mlaa) value.quality = 0;
        if (!usesTemporal()) {
            value.temporal = AntiAliasTemporal::Off;
            value.historyWeight = defaults.historyWeight;
            value.historyRejection = defaults.historyRejection;
        }
        return value;
    }
    // Anything that changes which shaders or textures exist needs a graph
    // rebuild; thresholds and history weights are uniform-only.
    bool sameStructure(const AntiAliasSettings& other) const {
        const auto a = active(), b = other.active();
        return a.enabled == b.enabled && a.method == b.method && a.quality == b.quality && a.temporal == b.temporal;
    }
    std::string validate() const {
        if (!validAntiAliasMethod(method)) return "invalid anti-aliasing method";
        if (quality >= antiAliasQualityCount(method)) return "invalid anti-aliasing quality";
        if (!std::isfinite(mlaaThreshold) || mlaaThreshold < 0.02f || mlaaThreshold > 0.30f) return "MLAA threshold out of range";
        if (!std::isfinite(mlaaStrength) || mlaaStrength < 0.0f || mlaaStrength > 1.0f) return "MLAA strength out of range";
        if (temporal != AntiAliasTemporal::Off && temporal != AntiAliasTemporal::T2x && temporal != AntiAliasTemporal::X4)
            return "invalid temporal anti-aliasing mode";
        if (temporal != AntiAliasTemporal::Off && method != AntiAliasMethod::Smaa) return "temporal anti-aliasing requires SMAA";
        if (!std::isfinite(historyWeight) || historyWeight < 0.0f || historyWeight > 0.95f) return "temporal history weight out of range";
        if (!std::isfinite(historyRejection) || historyRejection < 0.0f || historyRejection > 32.0f) return "temporal history rejection out of range";
        return {};
    }
};

// Preset library v14 / chain session v9: one appended block per anti-aliasing node.
inline void writeAntiAlias(std::ostream& out, const AntiAliasSettings& a) {
    out << (a.enabled ? 1 : 0) << ' ' << int(a.method) << ' ' << int(a.quality) << ' ' << a.mlaaThreshold << ' ' << a.mlaaStrength
        << ' ' << int(a.temporal) << ' ' << a.historyWeight << ' ' << a.historyRejection;
}
inline bool readAntiAlias(std::istream& in, AntiAliasSettings& a) {
    int enabled = 0, method = 0, quality = 0, temporal = 0;
    if (!(in >> enabled >> method >> quality >> a.mlaaThreshold >> a.mlaaStrength >> temporal >> a.historyWeight >> a.historyRejection)) return false;
    if (enabled < 0 || enabled > 1 || method < 0 || method > 2 || quality < 0 || quality > 255 || temporal < 0 || temporal > 2) return false;
    a.enabled = enabled != 0;
    a.method = static_cast<AntiAliasMethod>(method);
    a.quality = static_cast<uint8_t>(quality);
    a.temporal = static_cast<AntiAliasTemporal>(temporal);
    return a.validate().empty();
}
} // namespace veyra::engine
