// Ported unchanged from SilyNoMeta/DLSSG-Transfusion (MIT) tag v1.4.5.3-rtx20-30-40,
// commit b56bd2deed114507ad2c88f986d90ed50ffb4639, source/native/quality_fix.h.
// Copyright (c) 2026 Michael Robles (TonyJoaca/DLSSG-Transfusion), fork by SilyNoMeta; MIT, see LICENSE.txt.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include "quality_explained_warp.h"

namespace quality_fix
{
// Valid-warp policy (technique invented by Tony Joaca for Transfusion) inserted
// before the UIR flag load; restart required to change.
enum class Policy : uint8_t
{
    Transfusion,   // Tony's own tuning, QUALITY_VALID_WARP_V4: warped candidates agree (default)
    ExplainedWarp, // dlssg_for_sm86's tuning (its V3): the warp explains the frame change
};

// Registers for thin-geometry protection with tuned shadow rejection
// RenoDx Release 1.0 Validated Warp Blend: dual-track confidence boost and thin geometry recovery
inline constexpr std::string_view kRegisters = R"ptx(
.reg .pred %qv<7>;
.reg .f32 %qf<12>;
)ptx";

inline constexpr std::string_view kPolicy = R"ptx(
// QUALITY_VALID_WARP_V4
// MFGUNLOCK_VALIDATED_WARP_BLEND_V1_TUNED
cvt.rn.f32.u32 %qf0, %r10;
cvt.rn.f32.u32 %qf1, %r11;
rcp.approx.ftz.f32 %qf0, %qf0;
rcp.approx.ftz.f32 %qf1, %qf1;
mul.ftz.f32 %qf0, %qf0, 0f3F000000;
mul.ftz.f32 %qf1, %qf1, 0f3F000000;
sub.ftz.f32 %qf2, 0f3F800000, %qf0;
sub.ftz.f32 %qf3, 0f3F800000, %qf1;
setp.ge.f32 %qv0, %f123, %qf0;
setp.le.f32 %qv2, %f123, %qf2;
and.pred %qv0, %qv0, %qv2;
setp.ge.f32 %qv2, %f124, %qf1;
and.pred %qv0, %qv0, %qv2;
setp.le.f32 %qv2, %f124, %qf3;
and.pred %qv0, %qv0, %qv2;
not.pred %qv2, %p17;
and.pred %qv0, %qv0, %qv2;
setp.ge.f32 %qv1, %f129, %qf0;
setp.le.f32 %qv2, %f129, %qf2;
and.pred %qv1, %qv1, %qv2;
setp.ge.f32 %qv2, %f130, %qf1;
and.pred %qv1, %qv1, %qv2;
setp.le.f32 %qv2, %f130, %qf3;
and.pred %qv1, %qv1, %qv2;
not.pred %qv2, %p16;
and.pred %qv1, %qv1, %qv2;
abs.f32 %qf4, %f125;
abs.f32 %qf5, %f126;
abs.f32 %qf6, %f127;
add.f32 %qf4, %qf4, %qf5;
add.f32 %qf4, %qf4, %qf6;
setp.lt.f32 %qv2, %qf4, 0f7F800000;
and.pred %qv0, %qv0, %qv2;
abs.f32 %qf5, %f131;
abs.f32 %qf6, %f132;
abs.f32 %qf7, %f133;
add.f32 %qf5, %qf5, %qf6;
add.f32 %qf5, %qf5, %qf7;
setp.lt.f32 %qv2, %qf5, 0f7F800000;
and.pred %qv1, %qv1, %qv2;

and.pred %qv3, %qv0, %qv1;

// Unwarped L1 difference (%qf6 = |Unwarped0 - Unwarped1|)
sub.f32 %qf6, %f115, %f119;
sub.f32 %qf7, %f116, %f120;
sub.f32 %qf8, %f117, %f121;
abs.f32 %qf6, %qf6;
abs.f32 %qf7, %qf7;
abs.f32 %qf8, %qf8;
add.f32 %qf6, %qf6, %qf7;
add.f32 %qf6, %qf6, %qf8;

// Motion detector: Scene is in motion (UnwarpedDiff > 0.03f = 0f3CF5C28F).
// Fully preserves static HUD & UI elements (HUD does not move between frames, UnwarpedDiff == 0.0f).
setp.gt.f32 %qv2, %qf6, 0f3CF5C28F;
and.pred %qv3, %qv3, %qv2;

// Candidate Agreement Firewall (High-speed Dynamic Shadow Protection):
// On moving dynamic shadows (e.g. at 138 km/h), surface motion vectors diverge from shadow motion,
// creating large candidate color disagreement (|Cand0 - Cand1| > 18%).
// Excluding disagreeing candidates protects dynamic shadows from being erased or flickering,
// allowing stock DLSS-G neural blending to resolve shadows cleanly while locking true geometry.
sub.f32 %qf6, %f125, %f131;
sub.f32 %qf7, %f126, %f132;
sub.f32 %qf8, %f127, %f133;
abs.f32 %qf6, %qf6;
abs.f32 %qf7, %qf7;
abs.f32 %qf8, %qf8;
add.f32 %qf6, %qf6, %qf7;
add.f32 %qf6, %qf6, %qf8;
max.f32 %qf4, %qf4, %qf5;
max.f32 %qf4, %qf4, 0f3F19999A;
mul.f32 %qf4, %qf4, 0f3E0F5C29;
setp.le.f32 %qv4, %qf6, %qf4;
and.pred %qv3, %qv3, %qv4;

and.pred %qv0, %qv0, %qv3;
and.pred %qv1, %qv1, %qv3;

// Full geometric motion warp floor (0.98f = 0f3F7AE148)
// Eliminates fence tearing, eliminates background gap lag, locks airplane windows, zero HUD ghosting.
max.f32 %qf0, %f148, 0f3F7AE148;
min.f32 %qf0, %qf0, 0f3F800000;

max.f32 %qf1, %f149, 0f3F7AE148;
min.f32 %qf1, %qf1, 0f3F800000;

sub.f32 %qf2, %f125, %f115;
sub.f32 %qf3, %f126, %f116;
sub.f32 %qf4, %f127, %f117;
@%qv0 fma.rn.f32 %f39, %qf0, %qf2, %f115;
@%qv0 fma.rn.f32 %f38, %qf0, %qf3, %f116;
@%qv0 fma.rn.f32 %f37, %qf0, %qf4, %f117;
sub.f32 %qf2, %f131, %f119;
sub.f32 %qf3, %f132, %f120;
sub.f32 %qf4, %f133, %f121;
@%qv1 fma.rn.f32 %f43, %qf1, %qf2, %f119;
@%qv1 fma.rn.f32 %f42, %qf1, %qf3, %f120;
@%qv1 fma.rn.f32 %f41, %qf1, %qf4, %f121;
)ptx";

inline bool ReplaceOnce(std::string& text, const std::string_view from, const std::string_view to)
{
    const auto pos = text.find(from);
    if (pos == std::string::npos || text.find(from, pos + from.size()) != std::string::npos) return false;
    text.replace(pos, from.size(), to);
    return true;
}

inline bool Patch(std::string& ptx, std::string& why, Policy policy = Policy::Transfusion)
{
    std::string normalized = ptx;
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
    while (!normalized.empty() && normalized.back() == '\0') normalized.pop_back();
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : normalized) hash = (hash ^ c) * 1099511628211ull;
    if (normalized.size() != 39638 || hash != 0x7a6f5f41105c6d85ull)
    {
        why = "quality patch skipped: unrecognized BlendCandidatesFused PTX";
        return false;
    }
    const std::string anchor = "ld.param.u8 %rs8, [%rd6+220];";
    const auto site = normalized.find(anchor);
    const auto registers = normalized.find(".reg .pred %p<260>;");
    if (site == std::string::npos || registers == std::string::npos)
    {
        why = "quality patch skipped: expected insertion sites missing";
        return false;
    }

    // Both policies read the same registers at the same point; explained-warp
    // declares its temporaries in its own scope, Transfusion at function scope.
    if (policy == Policy::ExplainedWarp)
        normalized.insert(site, kPolicyExplainedWarp);
    else
    {
        normalized.insert(site, kPolicy);
        normalized.insert(registers, kRegisters);
    }

    // Fast reciprocal for UI chroma average (div by 3.0f -> mul by 0.33333334f)
    ReplaceOnce(normalized,
        "mov.f32 %f193, 0f40400000;\n"
        "div.approx.ftz.f32 %f194, %f192, %f193;",
        "mul.ftz.f32 %f194, %f192, 0f3EAAAAAB;");

    // Full-resolution tensor output: bypass 8x redundant SFU divisions by 1.0f
    ReplaceOnce(normalized,
        "mov.f32 %f537, 0f3F800000;\n"
        "div.approx.ftz.f32 %f529, %f39, %f537;\n"
        "div.approx.ftz.f32 %f530, %f38, %f537;\n"
        "div.approx.ftz.f32 %f531, %f37, %f537;\n"
        "div.approx.ftz.f32 %f532, %f36, %f537;\n"
        "div.approx.ftz.f32 %f533, %f43, %f537;\n"
        "div.approx.ftz.f32 %f534, %f42, %f537;\n"
        "div.approx.ftz.f32 %f535, %f41, %f537;\n"
        "div.approx.ftz.f32 %f536, %f40, %f537;",
        "mov.f32 %f529, %f39;\n"
        "mov.f32 %f530, %f38;\n"
        "mov.f32 %f531, %f37;\n"
        "mov.f32 %f532, %f36;\n"
        "mov.f32 %f533, %f43;\n"
        "mov.f32 %f534, %f42;\n"
        "mov.f32 %f535, %f41;\n"
        "mov.f32 %f536, %f40;");

    // Downsampling Mode 2: replace 8x div by 4.0f with mul by 0.25f (0f3E800000)
    ReplaceOnce(normalized,
        "mov.f32 %f528, 0f40800000;\n"
        "div.approx.ftz.f32 %f520, %f44, %f528;\n"
        "div.approx.ftz.f32 %f521, %f45, %f528;\n"
        "div.approx.ftz.f32 %f522, %f46, %f528;\n"
        "div.approx.ftz.f32 %f523, %f47, %f528;\n"
        "div.approx.ftz.f32 %f524, %f48, %f528;\n"
        "div.approx.ftz.f32 %f525, %f49, %f528;\n"
        "div.approx.ftz.f32 %f526, %f50, %f528;\n"
        "div.approx.ftz.f32 %f527, %f51, %f528;",
        "mov.f32 %f528, 0f3E800000;\n"
        "mul.ftz.f32 %f520, %f44, %f528;\n"
        "mul.ftz.f32 %f521, %f45, %f528;\n"
        "mul.ftz.f32 %f522, %f46, %f528;\n"
        "mul.ftz.f32 %f523, %f47, %f528;\n"
        "mul.ftz.f32 %f524, %f48, %f528;\n"
        "mul.ftz.f32 %f525, %f49, %f528;\n"
        "mul.ftz.f32 %f526, %f50, %f528;\n"
        "mul.ftz.f32 %f527, %f51, %f528;");

    // Downsampling Mode 4: replace 8x div by 16.0f with mul by 0.0625f (0f3D800000)
    ReplaceOnce(normalized,
        "mov.f32 %f495, 0f41800000;\n"
        "div.approx.ftz.f32 %f487, %f52, %f495;\n"
        "div.approx.ftz.f32 %f488, %f53, %f495;\n"
        "div.approx.ftz.f32 %f489, %f54, %f495;\n"
        "div.approx.ftz.f32 %f490, %f55, %f495;\n"
        "div.approx.ftz.f32 %f491, %f56, %f495;\n"
        "div.approx.ftz.f32 %f492, %f57, %f495;\n"
        "div.approx.ftz.f32 %f493, %f58, %f495;\n"
        "div.approx.ftz.f32 %f494, %f59, %f495;",
        "mov.f32 %f495, 0f3D800000;\n"
        "mul.ftz.f32 %f487, %f52, %f495;\n"
        "mul.ftz.f32 %f488, %f53, %f495;\n"
        "mul.ftz.f32 %f489, %f54, %f495;\n"
        "mul.ftz.f32 %f490, %f55, %f495;\n"
        "mul.ftz.f32 %f491, %f56, %f495;\n"
        "mul.ftz.f32 %f492, %f57, %f495;\n"
        "mul.ftz.f32 %f493, %f58, %f495;\n"
        "mul.ftz.f32 %f494, %f59, %f495;");

    // Downsampling Mode 8: replace 8x div by 64.0f with mul by 0.015625f (0f3C800000)
    ReplaceOnce(normalized,
        "mov.f32 %f430, 0f42800000;\n"
        "div.approx.ftz.f32 %f422, %f68, %f430;\n"
        "div.approx.ftz.f32 %f423, %f69, %f430;\n"
        "div.approx.ftz.f32 %f424, %f70, %f430;\n"
        "div.approx.ftz.f32 %f425, %f71, %f430;\n"
        "div.approx.ftz.f32 %f426, %f72, %f430;\n"
        "div.approx.ftz.f32 %f427, %f73, %f430;\n"
        "div.approx.ftz.f32 %f428, %f74, %f430;\n"
        "div.approx.ftz.f32 %f429, %f75, %f430;",
        "mov.f32 %f430, 0f3C800000;\n"
        "mul.ftz.f32 %f422, %f68, %f430;\n"
        "mul.ftz.f32 %f423, %f69, %f430;\n"
        "mul.ftz.f32 %f424, %f70, %f430;\n"
        "mul.ftz.f32 %f425, %f71, %f430;\n"
        "mul.ftz.f32 %f426, %f72, %f430;\n"
        "mul.ftz.f32 %f427, %f73, %f430;\n"
        "mul.ftz.f32 %f428, %f74, %f430;\n"
        "mul.ftz.f32 %f429, %f75, %f430;");

    // Downsampling Mode 16: replace 8x div by 256.0f with mul by 0.00390625f (0f3B800000)
    ReplaceOnce(normalized,
        "mov.f32 %f333, 0f43800000;\n"
        "div.approx.ftz.f32 %f325, %f84, %f333;\n"
        "div.approx.ftz.f32 %f326, %f85, %f333;\n"
        "div.approx.ftz.f32 %f327, %f86, %f333;\n"
        "div.approx.ftz.f32 %f328, %f87, %f333;\n"
        "div.approx.ftz.f32 %f329, %f88, %f333;\n"
        "div.approx.ftz.f32 %f330, %f89, %f333;\n"
        "div.approx.ftz.f32 %f331, %f90, %f333;\n"
        "div.approx.ftz.f32 %f332, %f91, %f333;",
        "mov.f32 %f333, 0f3B800000;\n"
        "mul.ftz.f32 %f325, %f84, %f333;\n"
        "mul.ftz.f32 %f326, %f85, %f333;\n"
        "mul.ftz.f32 %f327, %f86, %f333;\n"
        "mul.ftz.f32 %f328, %f87, %f333;\n"
        "mul.ftz.f32 %f329, %f88, %f333;\n"
        "mul.ftz.f32 %f330, %f89, %f333;\n"
        "mul.ftz.f32 %f331, %f90, %f333;\n"
        "mul.ftz.f32 %f332, %f91, %f333;");

    ptx = std::move(normalized);
    why = policy == Policy::ExplainedWarp
        ? "explained-warp policy (dlssg_for_sm86 V3) and SFU reciprocal optimizations applied"
        : "precision-tuned thin-geometry protection and SFU reciprocal optimizations applied";
    return true;
}
}

