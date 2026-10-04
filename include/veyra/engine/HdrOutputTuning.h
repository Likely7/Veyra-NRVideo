#pragma once

#include <algorithm>
#include <cmath>
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
//   HdrMetadataSettings  the HDR10 static metadata the display receives. Most
//                        displays run their own dynamic tone mapping; without
//                        metadata they have to guess the content peak, and a
//                        lower reported mastering peak makes many of them map the
//                        picture brighter overall.
//   presets              one-click combinations of the two, optionally re-scaled
//                        for the actual display (peak nits) and by a single
//                        "strength" dial.
//
// Everything defaults to "no change": the curve is disabled, the strength is 100%
// and every metadata field says "follow the source", so an untouched install
// renders exactly as before.

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
    bool enabled=true;
    // All four are 0 = "use the source's own value"; a non-zero value overrides
    // it. MaxCLL/MaxFALL are capped at 65535 because the DXGI structure stores
    // them in 16 bits.
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
    {"standard",        {false,   0,   10, 203,    0, 100,   0}, {true,     0,    0,   0, 50}},
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
inline void resolveHdrTuning(const HdrCurveSettings& base, const HdrMetadataSettings& baseMetadata,
                             HdrCurveSettings& curve, HdrMetadataSettings& metadata) {
    curve = base;
    metadata = baseMetadata;
    const unsigned strengthPercent = base.strengthPercent;
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

} // namespace veyra::engine
