#pragma once
#include <array>
#include <cmath>
#include "veyra/engine/ColorSettings.h"
namespace veyra::pipeline {
// CPU bake of the colour grade into the small tables the ingest shader reads.
// Everything expensive - chromatic adaptation, tone response, curve evaluation,
// per-band mixer weights, grading zones - happens here once per settings change,
// so per-pixel work stays a handful of lookups. Deliberately free of D3D12 so
// the maths can be unit-tested without a GPU.
//
// Curve domain: the working texture is scene-linear, so tonal operations are
// evaluated in a Cineon-style log domain (10 stops around 18% grey) and decoded
// back to linear. That matches the plan's "curves live in a log/display-referred
// domain" rule instead of bending linear values directly.
struct ColorGradeTables {
    static constexpr int kCurveEntries=1024;  // log-domain input -> linear rgb response
    static constexpr int kHueEntries=256;     // hue -> (shift, saturation scale, luminance scale)
    static constexpr int kLumEntries=256;     // tone zone -> (r,g,b) gain for colour grading
    std::array<float,kCurveEntries*4> curve{};
    std::array<float,kHueEntries*4> hue{};
    std::array<float,kLumEntries*4> lum{};
    // Linear-domain 3x3 (row-major): white balance + calibration, applied once
    // before the tone curve.
    std::array<float,9> matrix{1,0,0, 0,1,0, 0,0,1};
    float exposure=0;                 // EV, applied as a linear multiply
    float saturation=0,vibrance=0;    // -100..100, applied in the shader
    float lutStrength=0;              // 0..1 (0 disables the 3D LUT lookup)
    int lutInputSpace=0;
    bool blackWhite=false;            // monochrome mixer instead of the HSL mixer
    bool identity=true;               // master switch off or every parameter neutral
    bool exposureOnly=false;          // only exposure is non-neutral; use exact linear gain
    static ColorGradeTables bake(const engine::ColorSettings& settings);
    // The exact piecewise-linear response the bake uses, exposed so the UI can
    // draw the same curve the shader will apply (no second implementation).
    static float curveValue(const engine::ColorCurve& curve,float x);
    // Exposed for the tests and for callers that need the same log mapping.
    static float encodeLog(float linear);
    static float decodeLog(float encoded);
};
// Root-constant block consumed by ColorGrade.hlsli: five float4s (rows 0-2,
// controls, flags). The ingest passes apppend it after their own constants, so
// nothing existing moves.
inline constexpr int kColorGradeConstantCount=20;
inline void packColorGradeConstants(const ColorGradeTables& t,float* out){
    out[0]=t.matrix[0];out[1]=t.matrix[1];out[2]=t.matrix[2];out[3]=0;
    out[4]=t.matrix[3];out[5]=t.matrix[4];out[6]=t.matrix[5];out[7]=0;
    out[8]=t.matrix[6];out[9]=t.matrix[7];out[10]=t.matrix[8];out[11]=0;
    out[12]=t.exposure;out[13]=t.saturation;out[14]=t.vibrance;out[15]=t.identity?0.0f:t.lutStrength;
    // "identity" covers both "master switch off" and "every parameter neutral":
    // in either case the shader must return the input untouched.
    // flags.w(z) = black & white mixer active. Its per-band weights live in the
    // hue table's alpha channel, so no extra texture or constant slot is needed.
    out[16]=t.identity?0.0f:1.0f;out[17]=float(t.identity?0:t.lutInputSpace);out[18]=t.blackWhite?1.0f:0.0f;out[19]=t.exposureOnly?1.0f:0.0f;
}
} // namespace veyra::pipeline
