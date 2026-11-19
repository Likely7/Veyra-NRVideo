#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Custom: static HDR output tuning for the native HDR path.
//
// Three layers, all deliberately STATIC (nothing follows the scene, so the picture
// can never breathe):
//
//   HdrCurveSettings     a fixed brightness curve: lift the shadows without
//                        touching the highlights, move the mid-gray reference,
//                        and roll off (or cap) the top end. A single exposure
//                        cannot do this - it scales shadows and highlights by the
//                        same factor, which is exactly the "dark scenes need a
//                        lift but that blows out the bright ones" problem.
//   HdrMetadataSettings  optional HDR10 swapchain metadata. Windows may not
//                        forward it and monitors may ignore it; the pixel curve
//                        does not depend on a display accepting this hint.
//   presets              one-click combinations of the two, optionally re-scaled
//                        for the actual display (peak nits) and by a single
//                        "strength" dial.
//
// New controls are opt-in: the curve and metadata override are disabled and
// strength is 100%. Existing HDR pixels and monitor hints remain unchanged.

namespace veyra::engine {

struct HdrCurveSettings {
    bool enabled=false;
    // Shadow lift in 1/100 EV, applied only below `shadowRangeNits` (the weight
    // falls to zero there), so highlights are untouched by construction.
    int shadowLiftEv100=0;              // -200..200
    // Upper bound of the lift's influence, in nits.
    unsigned shadowRangeNits=10;        // 1..100
    // Mid-gray reference in nits. 203 is the BT.2408 SDR reference white and is
    // neutral; raising it brightens the picture while the peak stays put (the
    // curve is a gamma around the peak, not a scale).
    unsigned midGrayNits=203;           // 100..500
    // Where the highlight roll-off starts (nits). 0 disables the roll-off.
    unsigned highlightStartNits=0;      // 0 or 100..2000
    // How hard the roll-off compresses: 100 = the default knee, higher = softer.
    unsigned rollOffPercent=100;        // 10..200
    // Hard ceiling in nits. 0 disables it. Also anchors the mid-gray gamma.
    unsigned peakCapNits=0;             // 0 or 100..4000
    // NOTE: every field above is the BASELINE. The dial and the display peak below
    // are stored next to it, and the engine resolves baseline + dial + peak into
    // the numbers it actually applies (resolveHdrTuning). Keeping "which preset you
    // picked" separate from "how hard you turned it" is what lets the dial move
    // without the UI falling back to "custom", and lets the sliders stay useful as
    // the baseline they are edited against.
    //
    // One dial for the whole curve: 100% applies the baseline as it is, 0% fades
    // the curve back to "no change", 200% doubles its effect.
    unsigned strengthPercent=100;       // 0..200
    // The display's own peak brightness in nits, used to re-scale the presets'
    // highlight-related numbers (see resolveHdrTuning). 0 = ask the system.
    unsigned displayPeakNits=0;         // 0 or 100..10000

    bool operator==(const HdrCurveSettings&) const = default;
    bool valid() const {
        return shadowLiftEv100>=-200 && shadowLiftEv100<=200 &&
            shadowRangeNits>=1 && shadowRangeNits<=100 &&
            midGrayNits>=100 && midGrayNits<=500 &&
            (highlightStartNits==0 || (highlightStartNits>=100 && highlightStartNits<=2000)) &&
            rollOffPercent>=10 && rollOffPercent<=200 &&
            (peakCapNits==0 || (peakCapNits>=100 && peakCapNits<=4000)) &&
            strengthPercent<=200 &&
            (displayPeakNits==0 || (displayPeakNits>=100 && displayPeakNits<=10000));
    }
};

struct HdrMetadataSettings {
    // Send HDR10 static metadata to the display. Off leaves the display with
    // whatever it can infer from the picture itself.
    bool enabled=false;
    // The first three use 0 = source value or fallback. Nonzero overrides are
    // limited to 10000 nits, within DXGI's 16-bit MaxCLL/MaxFALL range.
    unsigned masteringPeakNits=0;       // 0 or 100..10000
    unsigned maxCllNits=0;              // 0 or 1..10000
    unsigned maxFallNits=0;             // 0 or 1..10000
    // Mastering black, in 0.0001 nit units (DXGI's unit). Default 0.005 nit.
    unsigned masteringMinNitsX10000=50;

    bool operator==(const HdrMetadataSettings&) const = default;
    bool valid() const {
        return (masteringPeakNits==0 || (masteringPeakNits>=100 && masteringPeakNits<=10000)) &&
            (maxCllNits==0 || (maxCllNits>=1 && maxCllNits<=10000)) &&
            (maxFallNits==0 || (maxFallNits>=1 && maxFallNits<=10000)) &&
            masteringMinNitsX10000<=1000000;
    }
};

// One-click presets: each entry sets the curve and the metadata together. The ids
// are stable (the UI maps them to translated labels); a UI value that matches no
// entry is reported as "custom".
//
// The highlight numbers are written for a 1000 nit display and are re-scaled by
// the display's own peak (resolveHdrTuning), which is what turns "highlight
// protection" from a guess into a calculation. The shadow and mid-gray numbers are
// deliberately NOT scaled: they describe the content and the viewing environment,
// not the display's ceiling.
struct HdrTuningPreset {
    const char* id;
    HdrCurveSettings curve;
    HdrMetadataSettings metadata;
};

inline constexpr HdrTuningPreset kHdrTuningPresets[] = {
    // id                enabled lift range mid  start roll cap   metaOn peak  cll  fall min
    {"standard",        {false,   0,   10, 203,    0, 100,   0}, {false,    0,    0,   0, 50}},
    {"darkLift",        {true,   50,   35, 225,    0, 100,   0}, {true,   700,  800, 350, 50}},
    {"highlightGuard",  {true,    0,   10, 203,  450, 120,1000}, {true,  1000, 1000, 400, 50}},
    {"darkAndBright",   {true,   50,   35, 220,  450, 120,1000}, {true,   700,  800, 350, 50}},
    {"brightRoom",      {true,   30,   30, 250,    0, 100,   0}, {true,     0,    0,   0, 50}},
    {"darkRoom",        {true,   40,   35, 185,  600, 130, 800}, {true,   600,  700, 300, 50}},
    {"punch",           {true,    0,   10, 175,  450, 110,1200}, {true,     0,    0,   0, 50}},
};
inline constexpr std::size_t kHdrTuningPresetCount = sizeof(kHdrTuningPresets) / sizeof(kHdrTuningPresets[0]);

// The peak the presets are authored against.
inline constexpr unsigned kHdrPresetReferencePeakNits = 1000;

inline const HdrTuningPreset* findHdrTuningPreset(const char* id) {
    if (!id) return nullptr;
    for (std::size_t i = 0; i < kHdrTuningPresetCount; ++i)
        if (std::strcmp(kHdrTuningPresets[i].id, id) == 0) return &kHdrTuningPresets[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// One-click recommendation and the highlight warning (custom).
//
// The file itself declares how bright it is: mastering display peak, the
// brightest single frame (MaxCLL) and the average of the brightest frames
// (MaxFALL). Those numbers arrive with the container, so they exist even for
// hardware-decoded files - unlike a measured histogram, which the D3D12 decode
// path never fills (it has no CPU luma at all).
//
// Both helpers below are pure functions of those numbers and the display peak,
// so the UI button, the tests and this comment cannot drift apart.
// ---------------------------------------------------------------------------
struct HdrTuningRecommendation {
    const char* presetId = "standard";
    unsigned strengthPercent = 100;
    unsigned displayPeakNits = 0;
    // Translation key for the "why this preset" line:
    // dark-content / bright-content / narrow-range / balanced / no-metadata.
    const char* reasonKey = "balanced";
    unsigned cllNits = 0;
    unsigned fallNits = 0;
    bool available = false;
};

inline HdrTuningRecommendation recommendHdrTuning(unsigned masteringPeakNits, unsigned maxCllNits,
                                                  unsigned maxFallNits, unsigned displayPeakNits) {
    HdrTuningRecommendation r;
    r.displayPeakNits = displayPeakNits;
    const unsigned cll = maxCllNits ? maxCllNits : masteringPeakNits;
    // No metadata at all (screen capture, some streams): say so instead of guessing.
    if (cll == 0) {
        r.reasonKey = "no-metadata";
        return r;
    }
    const unsigned fall = maxFallNits ? std::min(maxFallNits, cll) : cll;
    r.cllNits = cll;
    r.fallNits = fall;
    r.available = true;
    // MaxFALL/MaxCLL describes the shape of the film. A low ratio means long dark
    // stretches with a few bright highlights (night exteriors, space, thrillers);
    // a high ratio means the bright end is in use all the time (sports, fireworks,
    // animation). A very high MaxCLL alone means the highlight range is extreme
    // enough that protection matters regardless of the average.
    const double ratio = double(fall) / double(cll);
    // The display's own ceiling matters as much as the film's peak: content reaching far
    // beyond what the panel can show needs the highlight ceiling even when its average is
    // ordinary. A displayPeakNits of 0 means "unknown" (the caller could not tell), so the
    // branch is skipped rather than guessed at.
    const bool beyondDisplay = displayPeakNits > 0 && cll > displayPeakNits + displayPeakNits / 2;
    // Order matters: a dark film wants the lift even when its ceiling is low, and a low
    // ceiling means "punch" before it means "protect the highlights" - an old transfer has
    // no extreme highlights left to protect.
    if (ratio <= 0.20) {
        // A dark film on a dim display needs both ends: lift the shadows and cap the top.
        r.presetId = beyondDisplay ? "darkAndBright" : "darkLift";
        r.reasonKey = beyondDisplay ? "dark-and-bright" : "dark-content";
    } else if (masteringPeakNits && masteringPeakNits < 600) {
        r.presetId = "punch";
        r.reasonKey = "narrow-range";
    } else if (beyondDisplay || cll >= 4000 || ratio >= 0.45) {
        r.presetId = "highlightGuard";
        r.reasonKey = beyondDisplay ? "beyond-display" : "bright-content";
    } else {
        r.presetId = "standard";
        r.reasonKey = "balanced";
    }
    return r;
}

// Does the stored baseline still match this preset? The strength dial and the
// display peak are deliberately ignored: they only scale the baseline when it is
// applied, so moving either of them keeps you on the same preset. Editing the
// baseline sliders is what moves the UI to "custom".
inline bool hdrPresetMatches(const HdrCurveSettings& curve, const HdrMetadataSettings& metadata,
                             const HdrTuningPreset& preset) {
    HdrCurveSettings c = curve;
    c.strengthPercent = preset.curve.strengthPercent;
    c.displayPeakNits = preset.curve.displayPeakNits;
    return c == preset.curve && metadata == preset.metadata;
}

// Turn "preset + strength + display peak" into the settings the engine applies.
//
// Two steps, in this order:
//   1. Peak scaling: the preset's highlight numbers are authored for a 1000 nit
//      display, so they are scaled by `displayPeakNits / 1000`. A 600 nit display
//      starts rolling off earlier and caps lower; a 2000 nit display gets to use
//      brightness the preset would otherwise have thrown away. 0 = leave them.
//   2. Strength: everything that has a neutral value is interpolated towards it
//      (lift, range -> 0; mid-gray -> 203; roll-off -> 100), and the roll-off start
//      moves inversely because a *lower* start means a *stronger* effect. The hard
//      cap is a protection value, so it does not follow the dial.
inline unsigned resolveHdrDisplayPeakNits(unsigned manualPeakNits, unsigned reportedPeakNits, bool plausible) {
    if (manualPeakNits != 0) return manualPeakNits;
    return plausible ? std::clamp(reportedPeakNits, 100u, 10000u) : 0u;
}

inline void resolveHdrTuning(const HdrCurveSettings& base, const HdrMetadataSettings& baseMetadata,
                             HdrCurveSettings& curve, HdrMetadataSettings& metadata) {
    curve = base;
    metadata = baseMetadata;
    const unsigned displayPeakNits = base.displayPeakNits;

    // 1) display peak
    if (displayPeakNits > 0) {
        const double scale = double(displayPeakNits) / double(kHdrPresetReferencePeakNits);
        const auto scaleNits = [scale](unsigned v, unsigned lo, unsigned hi) {
            if (v == 0) return 0u;
            const auto scaled = unsigned(std::lround(double(v) * scale));
            return std::clamp(scaled, lo, hi);
        };
        curve.highlightStartNits = scaleNits(curve.highlightStartNits, 100u, 2000u);
        curve.peakCapNits = scaleNits(curve.peakCapNits, 100u, 4000u);
        metadata.masteringPeakNits = scaleNits(metadata.masteringPeakNits, 100u, 10000u);
        if (metadata.maxCllNits) metadata.maxCllNits = scaleNits(metadata.maxCllNits, 1u, 10000u);
        if (metadata.maxFallNits) metadata.maxFallNits = scaleNits(metadata.maxFallNits, 1u, 10000u);
    }

    // 2) strength
    const double k = double(curve.strengthPercent) / 100.0;
    if (curve.enabled && k <= 0.001) {
        curve.enabled = false;   // faded all the way out
        return;
    }
    if (k == 1.0) return;

    const auto blend = [k](int base, int neutral) {
        return int(std::lround(double(neutral) + double(base - neutral) * k));
    };
    curve.shadowLiftEv100 = std::clamp(blend(curve.shadowLiftEv100, 0), -200, 200);
    curve.shadowRangeNits = static_cast<unsigned>(std::clamp(int(std::lround(double(curve.shadowRangeNits) * k)), 1, 100));
    curve.midGrayNits = static_cast<unsigned>(std::clamp(blend(int(curve.midGrayNits), 203), 100, 500));
    curve.rollOffPercent = static_cast<unsigned>(std::clamp(blend(int(curve.rollOffPercent), 100), 10, 200));
    if (curve.highlightStartNits) {
        // A lower start compresses more, so strength scales it inversely.
        curve.highlightStartNits = static_cast<unsigned>(
            std::clamp(int(std::lround(double(curve.highlightStartNits) / k)), 100, 2000));
    }
}

// Convenience overload: resolve a preset as the baseline, overriding the dial and
// the display peak (used by the tests and by anything that starts from a preset).
inline void resolveHdrTuning(const HdrTuningPreset& preset, unsigned strengthPercent,
                             unsigned displayPeakNits, HdrCurveSettings& curve,
                             HdrMetadataSettings& metadata) {
    HdrCurveSettings base = preset.curve;
    base.strengthPercent = std::min(200u, strengthPercent);
    base.displayPeakNits = displayPeakNits;
    resolveHdrTuning(base, preset.metadata, curve, metadata);
}

// The curve's effective hard cap after the display-peak rescale and the strength
// dial (0 = the curve caps nothing). The highlight warning compares this with the
// source's own peak, which is what "how much of this film gets clipped" means.
inline unsigned hdrEffectivePeakCapNits(const HdrCurveSettings& base, const HdrMetadataSettings& baseMetadata) {
    HdrCurveSettings curve;
    HdrMetadataSettings metadata;
    resolveHdrTuning(base, baseMetadata, curve, metadata);
    return (curve.enabled && curve.strengthPercent > 0) ? curve.peakCapNits : 0u;
}

// The highlight warning as a pure function, so the guard below is covered by the
// unit tests instead of only by the UI. A source without metadata reports peak 0
// while an enabled curve can still carry a cap: dividing there killed the UI
// thread once (0xC0000094), which is exactly what this keeps from coming back.
struct HdrPeakWarning {
    bool valid = false;
    unsigned sourcePeakNits = 0;
    unsigned capNits = 0;
    unsigned keptPercent = 100;
    bool clipped = false;
};

inline HdrPeakWarning hdrPeakWarning(unsigned sourcePeakNits, bool sourceIsHdr, bool curveEnabled,
                                     const HdrCurveSettings& curve, const HdrMetadataSettings& metadata) {
    HdrPeakWarning w;
    w.sourcePeakNits = sourcePeakNits;
    w.capNits = hdrEffectivePeakCapNits(curve, metadata);
    w.valid = sourceIsHdr && sourcePeakNits > 0 && curveEnabled;
    w.keptPercent = (w.capNits && sourcePeakNits)
        ? unsigned(std::min<unsigned long long>(100, w.capNits * 100ull / sourcePeakNits))
        : 100;
    w.clipped = w.valid && w.capNits > 0 && w.capNits < sourcePeakNits;
    return w;
}

// ---------------------------------------------------------------------------
// Content x environment presets and the plain-language dials (custom).
//
// The legacy seven presets stay exactly as they are (kHdrTuningPresets); this is
// the second, more explainable way to pick a curve: the content axis says what the
// film looks like, the environment axis says where you watch. Environment only
// moves the mid-gray reference - the highlight behaviour belongs to the content,
// so one axis can never undo the other.
//
// The three dials are the same idea for manual tuning: each level maps onto the
// existing parameters, and the inverse lookup picks the nearest level, so the
// panel can show "暗部提亮 3" instead of "shadowLiftEv100 30". The exact technical
// values stay visible under the advanced section.
// ---------------------------------------------------------------------------
struct HdrContentPreset {
    const char* id;
    HdrCurveSettings curve;   // strengthPercent/displayPeakNits are filled by the caller
};

inline constexpr HdrContentPreset kHdrContentPresets[] = {
    // Curves carried over from the legacy presets so the two systems agree on what
    // "dark" or "highlight" means; the environment adds only the mid-gray shift.
    {"standard",     {true,   0,  10, 203,   0, 100,    0}},
    {"dark",         {true,  50,  35, 225,   0, 100,    0}},
    {"highlight",    {true,   0,  10, 203, 450, 120, 1000}},
    {"darkAndBright",{true,  50,  35, 220, 450, 120, 1000}},
    {"punch",        {true,   0,  10, 175, 450, 110, 1200}},
};

// The environment axis now names what it does rather than a time of day: the ids are
// the action ("compensate" raises the picture for a bright room, "plain" leaves the
// content's own baseline alone). The old ids are still mapped below so a preference
// stored by an earlier build keeps its meaning.
inline constexpr const char* kHdrEnvironmentIds[] = {"compensate", "plain"};
inline constexpr unsigned kHdrEnvironmentBrightShift = 45;   // mid-gray up in daylight
inline constexpr unsigned kHdrEnvironmentDarkShift = 0;      // no compensation: the content baseline stands
// Ambient light raises the display black level (veiling glare), which buries dark
// detail, so daylight also gets a small range-limited shadow lift - this mirrors what
// ambient-adaptive HDR does (a very dark scene in a very bright room gets the most
// correction, a dark room needs little). It stays at the bottom end on purpose: the
// highlight fields remain the content axis business.
// Matched to the legacy "明亮环境" preset (0.30 EV over 30 nits) so the two preset
// systems describe a bright room the same way.
inline constexpr int kHdrEnvironmentBrightLiftEv100 = 30;
inline constexpr unsigned kHdrEnvironmentBrightLiftRangeNits = 30;

inline const HdrContentPreset* findHdrContentPreset(const char* id) {
    if (!id) return nullptr;
    for (const auto& p : kHdrContentPresets)
        if (std::string_view(p.id) == id) return &p;
    return nullptr;
}

// The curve for one axis pair. Unknown ids fall back to the neutral content, which
// keeps the UI honest if a stored choice disappears in a later version.
inline HdrCurveSettings hdrAxesCurve(const char* content, const char* environment) {
    const HdrContentPreset* preset = findHdrContentPreset(content);
    if (!preset) preset = findHdrContentPreset("standard");
    HdrCurveSettings curve = preset->curve;
    // Legacy ids: "bright" was the compensation and "dark" the no-compensation case
    // before the rename, so both keep their old meaning here.
    std::string_view env = environment ? std::string_view(environment) : std::string_view("compensate");
    if (env == "bright") env = "compensate";
    else if (env == "dark") env = "plain";
    const bool compensate = env != "plain";
    const int shift = compensate ? int(kHdrEnvironmentBrightShift) : -int(kHdrEnvironmentDarkShift);
    curve.midGrayNits = unsigned(std::clamp(int(curve.midGrayNits) + shift, 100, 500));
    if (compensate) {
        curve.shadowLiftEv100 = std::clamp(curve.shadowLiftEv100 + kHdrEnvironmentBrightLiftEv100, -200, 200);
        curve.shadowRangeNits = std::clamp(std::max(curve.shadowRangeNits, kHdrEnvironmentBrightLiftRangeNits), 1u, 100u);
    }
    return curve;
}

// The recommendation names a legacy preset, but in the content x environment mode
// the same finding has to name a content. One mapping, so the two systems can never
// disagree about which finding means what. The environment presets carry no content
// statement, so they map to the neutral content.
inline const char* hdrAxesContentForPreset(const char* presetId) {
    if (!presetId) return "standard";
    const std::string_view id(presetId);
    if (id == "darkLift") return "dark";
    if (id == "highlightGuard") return "highlight";
    if (id == "darkAndBright") return "darkAndBright";
    if (id == "punch") return "punch";
    return "standard";
}

// ------------------------------------------------------------------ the dials
// Step tables: the inverse lookup below finds the nearest entry, so a curve edited
// with the technical sliders still shows a sensible dial position.
struct HdrShadowLiftStep { unsigned level; int liftEv100; unsigned rangeNits; };
inline constexpr HdrShadowLiftStep kHdrShadowLiftSteps[] = {
    {0, 0, 10}, {1, 10, 23}, {2, 20, 26}, {3, 30, 29}, {4, 40, 32}, {5, 50, 35},
    {6, 60, 38}, {7, 70, 41}, {8, 80, 44}, {9, 90, 47}, {10, 100, 50},
};
inline constexpr unsigned kHdrBrightnessStepCount = 11;   // level 0..10 -> 100..500 nit
inline constexpr unsigned kHdrHighlightStepCount = 4;     // off / weak / medium / strong

struct HdrHighlightStep { unsigned level; unsigned startNits; unsigned rollOffPercent; unsigned capNits; };
inline constexpr HdrHighlightStep kHdrHighlightSteps[] = {
    {0, 0, 100, 0}, {1, 600, 110, 1200}, {2, 450, 120, 1000}, {3, 300, 130, 800},
};

inline unsigned hdrBrightnessNits(unsigned level) {
    return std::min(500u, 100u + std::min(level, kHdrBrightnessStepCount - 1) * 40u);
}

inline unsigned hdrNearestLevel(unsigned value, unsigned count, const unsigned* values) {
    unsigned best = 0, bestDistance = ~0u;
    for (unsigned level = 0; level < count; ++level) {
        const unsigned distance = value > values[level] ? value - values[level] : values[level] - value;
        if (distance < bestDistance) { bestDistance = distance; best = level; }
    }
    return best;
}

inline unsigned hdrShadowLiftLevel(const HdrCurveSettings& curve) {
    unsigned values[std::size(kHdrShadowLiftSteps)];
    for (std::size_t i = 0; i < std::size(kHdrShadowLiftSteps); ++i)
        values[i] = unsigned(std::max(0, kHdrShadowLiftSteps[i].liftEv100));
    return hdrNearestLevel(unsigned(std::max(0, curve.shadowLiftEv100)), unsigned(std::size(values)), values);
}

inline void setHdrShadowLiftLevel(HdrCurveSettings& curve, unsigned level) {
    const auto& step = kHdrShadowLiftSteps[std::min(level, unsigned(std::size(kHdrShadowLiftSteps)) - 1)];
    curve.shadowLiftEv100 = step.liftEv100;
    curve.shadowRangeNits = step.rangeNits;
}

inline unsigned hdrBrightnessLevel(const HdrCurveSettings& curve) {
    unsigned values[kHdrBrightnessStepCount];
    for (unsigned i = 0; i < kHdrBrightnessStepCount; ++i) values[i] = hdrBrightnessNits(i);
    return hdrNearestLevel(curve.midGrayNits, kHdrBrightnessStepCount, values);
}

inline void setHdrBrightnessLevel(HdrCurveSettings& curve, unsigned level) {
    curve.midGrayNits = hdrBrightnessNits(level);
}

inline unsigned hdrHighlightLevel(const HdrCurveSettings& curve) {
    // Matched on the pair that gives the step its character: the ceiling and the
    // roll-off distance. A curve with no ceiling is "off" by definition.
    unsigned best = 0, bestDistance = ~0u;
    for (const auto& step : kHdrHighlightSteps) {
        const unsigned distance = (curve.peakCapNits > step.capNits ? curve.peakCapNits - step.capNits
                                                                    : step.capNits - curve.peakCapNits) +
                                  (curve.rollOffPercent > step.rollOffPercent
                                       ? curve.rollOffPercent - step.rollOffPercent
                                       : step.rollOffPercent - curve.rollOffPercent);
        if (distance < bestDistance) { bestDistance = distance; best = step.level; }
    }
    return best;
}

inline void setHdrHighlightLevel(HdrCurveSettings& curve, unsigned level) {
    for (const auto& step : kHdrHighlightSteps)
        if (step.level == std::min(level, kHdrHighlightStepCount - 1)) {
            curve.highlightStartNits = step.startNits;
            curve.rollOffPercent = step.rollOffPercent;
            curve.peakCapNits = step.capNits;
            return;
        }
}

// ---------------------------------------------------------------------------
// Curve sampling for the panel graph (custom).
//
// This is a line-by-line port of HdrCurveLuminance in shaders/PresentBlit.hlsl, so
// the drawn curve cannot promise something the GPU does not do. Keep the two in
// step: every branch here mirrors the shader's branch, including the defaults
// (lift 0, mid 203, start 0, cap 0 all being no-ops).
// ---------------------------------------------------------------------------
inline double hdrCurveRollOffScaleNits(const HdrCurveSettings& curve) {
    // Mirrors the presenter: peak - start, scaled by the roll-off percentage.
    const double peak = curve.peakCapNits ? double(curve.peakCapNits) : 1000.0;
    const double start = double(curve.highlightStartNits);
    return std::max(1.0, peak - start) * double(curve.rollOffPercent) / 100.0;
}

inline double hdrCurveLuminance(const HdrCurveSettings& curve, double nits) {
    const double liftEv = double(curve.shadowLiftEv100) / 100.0;
    const double range = double(curve.shadowRangeNits);
    const double mid = double(curve.midGrayNits);
    const double start = double(curve.highlightStartNits);
    const double scale = hdrCurveRollOffScaleNits(curve);
    const double cap = double(curve.peakCapNits);
    if (liftEv != 0.0 && range > 0.0) {
        const double w = std::clamp(1.0 - nits / range, 0.0, 1.0);
        nits *= 1.0 + w * w * (std::exp2(liftEv) - 1.0);
    }
    if (mid > 0.0 && std::abs(mid - 203.0) > 0.5) {
        const double peak = cap > 0.0 ? cap : 1000.0;
        if (peak > mid + 1.0) {
            const double g = std::log(mid / peak) / std::log(203.0 / peak);
            nits = peak * std::pow(std::max(nits / peak, 0.0), g);
        }
    }
    if (start > 0.0 && scale > 0.0 && nits > start)
        nits = start + (nits - start) / (1.0 + (nits - start) / scale);
    if (cap > 0.0) nits = std::min(nits, cap);
    return nits;
}

// Sample points for the panel graph: a fixed logarithmic sweep of the input, low to
// high, so the graph shows the same shape regardless of the panel size.
inline constexpr unsigned kHdrCurveGraphPoints = 48;
inline constexpr double kHdrCurveGraphMinNits = 0.5;
inline constexpr double kHdrCurveGraphMaxNits = 4000.0;

inline double hdrCurveGraphInputNits(unsigned index) {
    const double t = double(index) / double(kHdrCurveGraphPoints - 1);
    return kHdrCurveGraphMinNits * std::pow(kHdrCurveGraphMaxNits / kHdrCurveGraphMinNits, t);
}
// ---------------------------------------------------------------------------
// Live coverage of one measured frame (custom).
//
// This is what the panel's live meter shows: of the luminances just measured (the
// presenter feeds it a 64x36 probe of the frame), how many are above the curve's
// ceiling (the picture is being crushed flat there), how many are inside the
// roll-off (compressed but not flat) and how many are inside the shadow-lift range
// (being raised). Callers pass an already resolved curve, so the display rescale
// and the strength dial are included in the thresholds.
//
// Pure on purpose: the numbers the panel displays are defined here and covered by
// the unit tests, not computed inside the render loop.
// ---------------------------------------------------------------------------
struct HdrCurveCoverage {
    unsigned samples = 0;
    unsigned aboveCeiling = 0;
    unsigned inRollOff = 0;
    unsigned inLift = 0;
    bool valid = false;
    double aboveCeilingPercent() const { return samples ? 100.0 * double(aboveCeiling) / double(samples) : 0.0; }
    double inRollOffPercent() const { return samples ? 100.0 * double(inRollOff) / double(samples) : 0.0; }
    double inLiftPercent() const { return samples ? 100.0 * double(inLift) / double(samples) : 0.0; }
};

inline HdrCurveCoverage hdrCurveCoverage(const double* nits, unsigned count, const HdrCurveSettings& curve) {
    HdrCurveCoverage coverage;
    coverage.valid = curve.enabled && nits != nullptr && count > 0;
    if (!coverage.valid) return coverage;
    coverage.samples = count;
    const double cap = double(curve.peakCapNits);
    const double start = double(curve.highlightStartNits);
    const double range = double(curve.shadowRangeNits);
    for (unsigned i = 0; i < count; ++i) {
        const double value = nits[i];
        if (!std::isfinite(value) || value <= 0.0) continue;
        if (curve.shadowLiftEv100 != 0 && range > 0.0 && value < range) ++coverage.inLift;
        // Reproduce each stage before the final hard cap. A soft knee may
        // already bring values below the cap, so raw input > cap is not clipping.
        double mapped=value;
        const double w=range>0?std::clamp(1-value/range,0.0,1.0):0.0;
        mapped*=1+w*w*(std::exp2(double(curve.shadowLiftEv100)/100)-1);
        const double peak=cap>0?cap:1000;
        if(curve.midGrayNits>0&&std::abs(double(curve.midGrayNits)-203)>.5&&peak>curve.midGrayNits+1)
            mapped=peak*std::pow(std::max(mapped/peak,0.0),std::log(curve.midGrayNits/peak)/std::log(203/peak));
        if(start>0&&mapped>start){++coverage.inRollOff;const double scale=hdrCurveRollOffScaleNits(curve);mapped=start+(mapped-start)/(1+(mapped-start)/scale);}
        if(cap>0&&mapped>cap)++coverage.aboveCeiling;
    }
    return coverage;
}
// SMPTE ST 2084 (PQ) EOTF: an encoded code value back to absolute nits. The coverage
// probe renders into the swapchain's 10-bit UNORM format, which can only carry PQ
// code values - writing linear nits there clamped everything above 1.0 (= 80 nits)
// flat, so no cycle could ever report a highlight bucket. Keeping the encode on and
// decoding here preserves the whole 0..10000 nit range.
inline double pqCodeToNits(double normalized) {
    const double n = std::clamp(normalized, 0.0, 1.0);
    const double m1 = 2610.0 / 16384.0;
    const double m2 = 2523.0 / 4096.0 * 128.0;
    const double c1 = 3424.0 / 4096.0;
    const double c2 = 2413.0 / 4096.0 * 32.0;
    const double c3 = 2392.0 / 4096.0 * 32.0;
    const double p = std::pow(n, 1.0 / m2);
    const double numerator = std::max(p - c1, 0.0);
    const double denominator = c2 - c3 * p;
    if (denominator <= 0.0) return 10000.0;
    return 10000.0 * std::pow(numerator / denominator, 1.0 / m1);
}
// Optional per-frame metadata is sticky within one source epoch only. The
// presenter owns this state; reopening it or switching sources must clear it.
inline unsigned hdrMetadataNits(float value){
    return std::isfinite(value)&&value>0?unsigned(std::lround(std::clamp(value,1.0f,10000.0f))):0u;
}
struct HdrSourceMetadata {
    uint64_t epoch=0;
    bool hasEpoch=false;
    unsigned peak=0,cll=0,fall=0;
    void observe(uint64_t sourceEpoch,bool hdr,float sourcePeak,float sourceCll,float sourceFall) {
        if(!hasEpoch||epoch!=sourceEpoch||!hdr){peak=cll=fall=0;epoch=sourceEpoch;hasEpoch=true;}
        if(!hdr)return;
        const auto update=[](unsigned& value,float incoming){
            if(std::isfinite(incoming)&&incoming>0)
                value=std::clamp(unsigned(std::lround(std::min(incoming,10000.0f))),1u,10000u);
        };
        update(peak,sourcePeak);update(cll,sourceCll);update(fall,sourceFall);
    }
};

struct HdrMetadataLuminance {
    unsigned peak=1000,minX10000=50,cll=1000,fall=1000;
    bool operator==(const HdrMetadataLuminance&) const = default;
};

inline HdrMetadataLuminance resolveHdrMetadataLuminance(const HdrMetadataSettings& m,const HdrSourceMetadata& source) {
    HdrMetadataLuminance result;
    result.peak=m.masteringPeakNits?m.masteringPeakNits:source.peak?source.peak:1000;
    result.cll=m.maxCllNits?m.maxCllNits:source.cll?source.cll:result.peak;
    result.fall=std::min(m.maxFallNits?m.maxFallNits:source.fall?source.fall:result.cll,result.cll);
    result.minX10000=std::min(m.masteringMinNitsX10000,result.peak*10000u);
    return result;
}

// The brightness curve runs in the present shader, so what a display should be told is
// the *output*, not the source: with a cap active the source peak describes a level the
// picture no longer reaches, and the display's own tone mapping would then work from a
// value that is too high. Bound the content light levels by the cap the shader really
// applies (hdrEffectivePeakCapNits resolves the same curve and strength, and returns 0
// when the curve is inactive, in which case the source values stand as they are).
inline HdrMetadataLuminance clampMetadataToCurve(HdrMetadataLuminance light,
                                                 const HdrCurveSettings& curve,
                                                 const HdrMetadataSettings& settings) {
    const unsigned cap=hdrEffectivePeakCapNits(curve,settings);
    if(cap){
        light.cll=std::min(light.cll,cap);
        light.fall=std::min(light.fall,light.cll);
    }
    return light;
}

} // namespace veyra::engine
