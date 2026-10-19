#pragma once
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace veyra::engine {
// Beauty node: two independent sub-functions on the picture at the node's position.
//
//   Face restore  ("人脸原画保护"): inside the detected faces the node blends the
//                 same frame's original back in (the picture before every effect
//                 node: colour grades, SR, every NR layer, earlier beauty nodes):
//                 P = lerp(C, O, strength * faceMask).
//   Skin smoothing ("美颜"): SkinSoft's skin selection, detail protection and
//                 texture restore around an edge-preserving local mean (GPUPixel's
//                 variance-adaptive blend, run as a fast guided filter) on P.
//
// Only `enabled` changes the graph (the node exists or not); every other value is
// a uniform, so tuning never rebuilds NGX / NR / FG. Faces come from one detector
// shared by every beauty node of the chain (BeautyDetection).
inline constexpr uint32_t kMaxBeautyInstances = 6;

enum class BeautyRestoreScope : uint8_t { Face = 0, Skin = 1 };  // whole face estimate / its skin only
enum class BeautyScope : uint8_t { Faces = 0, AllSkin = 1 };     // smoothing inside faces / every skin-coloured pixel

struct BeautySettings {
    bool enabled = false;
    // Face restore.
    bool restore = false;
    float restoreStrength = 1.0f;          // 0 .. 1
    BeautyRestoreScope restoreScope = BeautyRestoreScope::Face;
    float regionExpand = 0.05f;            // -0.10 .. 0.30 of the face size
    float feather = 0.08f;                 // 0 .. 0.20 of the face width
    // Skin smoothing.
    bool smooth = false;
    float strength = 1.0f;                 // 0 .. 1, the whole smoothing sub-function
    // Defaults tuned on lit photos and dark game footage (docs/BEAUTY_IMPLEMENTATION_PLAN):
    // visible on skin texture, eyes / lashes / lips kept.
    float smoothAmount = 0.50f;            // 0 .. 1
    float radius = 4.0f;                   // 0 .. 12: Gaussian sigma in 1080p pixels, scaled with the node's size
    float detail = 0.65f;                  // 0 .. 1 detail protection
    float texture = 0.30f;                 // 0 .. 1 texture restore (back to P, never to the effects restore removed)
    float brighten = 0.0f;                 // 0 .. 1 skin brightening
    BeautyScope scope = BeautyScope::Faces;
    // Skin range on the display-referred picture (SkinSoft's HSV / luma selection).
    float hueCenter = 0.08f;               // 0 .. 1 (hue turns)
    float hueRange = 0.10f;                // 0.01 .. 0.50
    // SkinSoft's 0.12 / 0.18 floors drop dim or dark skin (night game scenes measured
    // 0.03-0.17 luma); inside a detected face the lower floors are safe.
    float satMin = 0.08f, satMax = 0.80f;  // 0 .. 1
    float lumaMin = 0.04f, lumaMax = 0.95f;
    float softness = 0.35f;                // 0 .. 1
    float minConfidence = 0.6f;            // 0.3 .. 0.95: faces below it do not count for this node
    bool showMask = false;                 // tint the restore (blue) and smoothing (magenta) areas
    bool operator==(const BeautySettings&) const = default;

    // Untouched defaults add no node, so presets saved without beauty keep their format.
    bool configured() const { return *this != BeautySettings{}; }
    bool restoreActive() const { return enabled && restore && restoreStrength > 0.0f; }
    bool smoothActive() const { return enabled && smooth && strength > 0.0f && (smoothAmount > 0.0f || brighten > 0.0f); }
    // Anything to do this frame (the node itself exists whenever it is enabled).
    bool effective() const { return restoreActive() || smoothActive() || (enabled && showMask); }
    // The values that matter to the running effect: a disabled node compares equal
    // to the defaults, so editing it never counts as a change.
    BeautySettings active() const { return enabled ? *this : BeautySettings{}; }
    bool sameStructure(const BeautySettings& other) const { return enabled == other.enabled; }
    std::string validate() const {
        const auto in = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        if (!in(restoreStrength, 0, 1)) return "beauty restore strength out of range";
        if (restoreScope != BeautyRestoreScope::Face && restoreScope != BeautyRestoreScope::Skin) return "invalid beauty restore scope";
        if (!in(regionExpand, -0.10f, 0.30f)) return "beauty region expansion out of range";
        if (!in(feather, 0, 0.20f)) return "beauty feather out of range";
        if (!in(strength, 0, 1) || !in(smoothAmount, 0, 1) || !in(detail, 0, 1) || !in(texture, 0, 1) || !in(brighten, 0, 1))
            return "beauty smoothing value out of range";
        if (!in(radius, 0, 12)) return "beauty radius out of range";
        if (scope != BeautyScope::Faces && scope != BeautyScope::AllSkin) return "invalid beauty scope";
        if (!in(hueCenter, 0, 1) || !in(hueRange, 0.01f, 0.5f) || !in(softness, 0, 1)) return "beauty skin hue out of range";
        if (!in(satMin, 0, 1) || !in(satMax, 0, 1) || satMin >= satMax) return "beauty skin saturation range invalid";
        if (!in(lumaMin, 0, 1) || !in(lumaMax, 0, 1) || lumaMin >= lumaMax) return "beauty skin brightness range invalid";
        if (!in(minConfidence, 0.3f, 0.95f)) return "beauty face confidence out of range";
        return {};
    }
};

// One face detector serves every beauty node of a chain: these values are the
// chain's (list and node mode keep their own), saved with it and frozen for export.
// The detector runs on every new frame at any quality.
struct BeautyDetection {
    uint8_t quality = 1;                   // detector input: 0 performance (320 px), 1 balanced (512 px), 2 fine (640 px)
    uint8_t maxFaces = 4;                  // 1 .. 8
    float stability = 0.5f;                // 0 .. 1: smoothing of a face at rest (a moving face is followed closely at any value)
    bool operator==(const BeautyDetection&) const = default;
    uint32_t inputLongSide() const { return quality == 0 ? 320u : quality == 2 ? 640u : 512u; }
    std::string validate() const {
        if (quality > 2) return "invalid beauty detection quality";
        if (maxFaces < 1 || maxFaces > 8) return "beauty face count out of range";
        if (!std::isfinite(stability) || stability < 0.0f || stability > 1.0f) return "beauty tracking stability out of range";
        return {};
    }
};

// Preset library v15 / chain session v10: one block per beauty node, one per chain.
inline void writeBeauty(std::ostream& out, const BeautySettings& b) {
    out << (b.enabled ? 1 : 0) << ' ' << (b.restore ? 1 : 0) << ' ' << b.restoreStrength << ' ' << int(b.restoreScope) << ' '
        << b.regionExpand << ' ' << b.feather << ' ' << (b.smooth ? 1 : 0) << ' ' << b.strength << ' ' << b.smoothAmount << ' '
        << b.radius << ' ' << b.detail << ' ' << b.texture << ' ' << b.brighten << ' ' << int(b.scope) << ' '
        << b.hueCenter << ' ' << b.hueRange << ' ' << b.satMin << ' ' << b.satMax << ' ' << b.lumaMin << ' ' << b.lumaMax << ' '
        << b.softness << ' ' << b.minConfidence << ' ' << (b.showMask ? 1 : 0);
}
inline bool readBeauty(std::istream& in, BeautySettings& b) {
    int enabled = 0, restore = 0, restoreScope = 0, smooth = 0, scope = 0, mask = 0;
    if (!(in >> enabled >> restore >> b.restoreStrength >> restoreScope >> b.regionExpand >> b.feather >> smooth >> b.strength >>
          b.smoothAmount >> b.radius >> b.detail >> b.texture >> b.brighten >> scope >> b.hueCenter >> b.hueRange >> b.satMin >>
          b.satMax >> b.lumaMin >> b.lumaMax >> b.softness >> b.minConfidence >> mask)) return false;
    const auto flag = [](int v) { return v == 0 || v == 1; };
    if (!flag(enabled) || !flag(restore) || !flag(smooth) || !flag(mask) || restoreScope < 0 || restoreScope > 1 || scope < 0 || scope > 1)
        return false;
    b.enabled = enabled != 0; b.restore = restore != 0; b.smooth = smooth != 0; b.showMask = mask != 0;
    b.restoreScope = static_cast<BeautyRestoreScope>(restoreScope);
    b.scope = static_cast<BeautyScope>(scope);
    return b.validate().empty();
}
inline void writeBeautyDetection(std::ostream& out, const BeautyDetection& d) {
    out << int(d.quality) << ' ' << int(d.maxFaces) << ' ' << d.stability;
}
inline bool readBeautyDetection(std::istream& in, BeautyDetection& d) {
    int quality = 0, faces = 0;
    if (!(in >> quality >> faces >> d.stability) || quality < 0 || quality > 2 || faces < 1 || faces > 8) return false;
    d.quality = uint8_t(quality); d.maxFaces = uint8_t(faces);
    return d.validate().empty();
}
} // namespace veyra::engine
