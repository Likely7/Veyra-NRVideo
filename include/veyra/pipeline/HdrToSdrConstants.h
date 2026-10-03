#pragma once
// Custom addition: the HDR->SDR tone-map controls the RGB ingress shaders need.
//
// The YUV ingress (YuvToLinearRgb.hlsl) already carries them inside its existing
// toneMapParams float4 (x=source peak, y=SDR white; z/w were unused and now hold
// exposure and shoulder), so it needs no extra constants. The RGB ingress
// (RgbToLinear.hlsl) has no spare lane, so its settings are appended after the
// colour-grade block and the shader declares them in that order:
//
//   [8 + kColorGradeConstantCount + 0]  sdrWhiteNits
//   [8 + kColorGradeConstantCount + 1]  exposureEv
//   [8 + kColorGradeConstantCount + 2]  shoulder (1.0 = the previous fixed knee)
//
// Defaults reproduce the numbers that used to be hard-coded, so an untouched
// install renders identically.
namespace veyra::pipeline {

inline constexpr int kHdrToSdrConstantCount = 3;

}
