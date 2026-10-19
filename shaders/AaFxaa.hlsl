// FXAA medium / high / ultra (FXAA_PRESET 3 / 4 / 5, set by the build) for
// Veyra's anti-aliasing effect (#53).
//
// Ported from SAOG0721/Magpie d687997 src/Effects/FXAA (FXAA.hlsli and
// FXAA_{Medium,High,Ultra}.hlsl, GPL-3.0), itself ported from
// https://github.com/libretro/slang-shaders/blob/master/anti-aliasing/shaders/fxaa.slang
// Veyra changes: the luma comes from the perceptual luma texture
// (AntiAlias.hlsli), scaled to FxaaLuma()'s 0..2.963 range for neutral colours
// so the thresholds keep their meaning; the neighbourhood average and the final
// blend use the linear working colour; one pixel per thread instead of 2x2
// gather blocks; the centre's alpha is kept. Magpie's copy omitted the NVIDIA
// notice of the libretro source; it is restored here:
/**
 * @license
 * Copyright (c) 2011 NVIDIA Corporation. All rights reserved.
 *
 * TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
 * *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
 * OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, NONINFRINGEMENT,IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA
 * OR ITS SUPPLIERS BE  LIABLE  FOR  ANY  DIRECT, SPECIAL,  INCIDENTAL,  INDIRECT,  OR
 * CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS
 * OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY
 * OTHER PECUNIARY LOSS) ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE,
 * EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 */

/*
FXAA_PRESET - Choose compile-in knob preset 0-5.
------------------------------------------------------------------------------
FXAA_EDGE_THRESHOLD - The minimum amount of local contrast required
                      to apply algorithm.
                      1.0/3.0  - too little
                      1.0/4.0  - good start
                      1.0/8.0  - applies to more edges
                      1.0/16.0 - overkill
------------------------------------------------------------------------------
FXAA_EDGE_THRESHOLD_MIN - Trims the algorithm from processing darks.
                          Perf optimization.
                          1.0/32.0 - visible limit (smaller isn't visible)
                          1.0/16.0 - good compromise
                          1.0/12.0 - upper limit (seeing artifacts)
------------------------------------------------------------------------------
FXAA_SEARCH_STEPS - Maximum number of search steps for end of span.
------------------------------------------------------------------------------
FXAA_SEARCH_THRESHOLD - Controls when to stop searching.
                        1.0/4.0 - seems to be the best quality wise
------------------------------------------------------------------------------
FXAA_SUBPIX_TRIM - Controls sub-pixel aliasing removal.
                   1.0/2.0 - low removal
                   1.0/3.0 - medium removal
                   1.0/4.0 - default removal
                   1.0/8.0 - high removal
                   0.0 - complete removal
------------------------------------------------------------------------------
FXAA_SUBPIX_CAP - Insures fine detail is not completely removed.
                  This is important for the transition of sub-pixel detail,
                  like fences and wires.
                  3.0/4.0 - default (medium amount of filtering)
                  7.0/8.0 - high amount of filtering
                  1.0 - no capping of sub-pixel aliasing removal
*/
#include "AntiAlias.hlsli"

#ifndef FXAA_PRESET
#define FXAA_PRESET 5
#endif
#if (FXAA_PRESET == 3)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/16.0)
#define FXAA_SEARCH_STEPS        16
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
#if (FXAA_PRESET == 4)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        24
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
#if (FXAA_PRESET == 5)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        32
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif

#define FXAA_SUBPIX_TRIM_SCALE (1.0/(1.0 - FXAA_SUBPIX_TRIM))

Texture2D<float4> source : register(t0); // linear working colour
Texture2D<float> luma : register(t1);    // AaLuma output
RWTexture2D<float4> target : register(u0);

// FxaaLuma() of white: rgb.y * (0.587 / 0.299) + rgb.x with rgb = 1.
static const float kFxaaLumaScale = 2.963210702;

float FxaaLumaAt(int2 p) { return luma.Load(int3(AaClamp(p), 0)) * kFxaaLumaScale; }
float FxaaLumaSample(float2 uv) { return luma.SampleLevel(aaLinearSampler, uv, 0) * kFxaaLumaScale; }
float3 FxaaColorAt(int2 p) { return source.Load(int3(AaClamp(p), 0)).rgb; }

float3 FxaaLerp3(float3 a, float3 b, float amountOfA) {
    return (a - b) * amountOfA + b;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= aaWidth || id.y >= aaHeight) return;
    const int2 p = int2(id.xy);
    const float2 inputPt = float2(aaInvWidth, aaInvHeight);
    const float2 pos = AaTexcoord(id.xy);
    const float4 center = source.Load(int3(p, 0));

    float lumaN = FxaaLumaAt(p + int2(0, -1));
    float lumaW = FxaaLumaAt(p + int2(-1, 0));
    const float lumaM = FxaaLumaAt(p);
    float lumaE = FxaaLumaAt(p + int2(1, 0));
    float lumaS = FxaaLumaAt(p + int2(0, 1));
    const float rangeMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
    const float rangeMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));

    const float range = rangeMax - rangeMin;
    if (range < max(FXAA_EDGE_THRESHOLD_MIN, rangeMax * FXAA_EDGE_THRESHOLD)) {
        target[id.xy] = center;
        return;
    }
    float3 rgbL = FxaaColorAt(p + int2(0, -1)) + FxaaColorAt(p + int2(-1, 0)) + center.rgb +
        FxaaColorAt(p + int2(1, 0)) + FxaaColorAt(p + int2(0, 1));

    const float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25;
    const float rangeL = abs(lumaL - lumaM);
    float blendL = max(0.0, (rangeL / range) - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE;
    blendL = min(FXAA_SUBPIX_CAP, blendL);

    rgbL += FxaaColorAt(p + int2(-1, -1)) + FxaaColorAt(p + int2(1, -1)) +
        FxaaColorAt(p + int2(-1, 1)) + FxaaColorAt(p + int2(1, 1));
    rgbL *= 1.0 / 9.0;

    const float lumaNW = FxaaLumaAt(p + int2(-1, -1));
    const float lumaNE = FxaaLumaAt(p + int2(1, -1));
    const float lumaSW = FxaaLumaAt(p + int2(-1, 1));
    const float lumaSE = FxaaLumaAt(p + int2(1, 1));

    const float edgeVert =
        abs((0.25 * lumaNW) + (-0.5 * lumaN) + (0.25 * lumaNE)) +
        abs((0.50 * lumaW) + (-1.0 * lumaM) + (0.50 * lumaE)) +
        abs((0.25 * lumaSW) + (-0.5 * lumaS) + (0.25 * lumaSE));
    const float edgeHorz =
        abs((0.25 * lumaNW) + (-0.5 * lumaW) + (0.25 * lumaSW)) +
        abs((0.50 * lumaN) + (-1.0 * lumaM) + (0.50 * lumaS)) +
        abs((0.25 * lumaNE) + (-0.5 * lumaE) + (0.25 * lumaSE));

    const bool horzSpan = edgeHorz >= edgeVert;
    float lengthSign = horzSpan ? -inputPt.y : -inputPt.x;

    if (!horzSpan) {
        lumaN = lumaW;
        lumaS = lumaE;
    }

    float gradientN = abs(lumaN - lumaM);
    const float gradientS = abs(lumaS - lumaM);
    lumaN = (lumaN + lumaM) * 0.5;
    lumaS = (lumaS + lumaM) * 0.5;

    if (gradientN < gradientS) {
        lumaN = lumaS;
        gradientN = gradientS;
        lengthSign *= -1.0;
    }

    float2 posN;
    posN.x = pos.x + (horzSpan ? 0.0 : lengthSign * 0.5);
    posN.y = pos.y + (horzSpan ? lengthSign * 0.5 : 0.0);

    gradientN *= FXAA_SEARCH_THRESHOLD;

    float2 posP = posN;
    const float2 offNP = horzSpan ? float2(inputPt.x, 0.0) : float2(0.0, inputPt.y);
    float lumaEndN = lumaN;
    float lumaEndP = lumaN;
    bool doneN = false;
    bool doneP = false;
    posN += offNP * float2(-1.0, -1.0);
    posP += offNP * float2(1.0, 1.0);

    for (int i = 0; i < FXAA_SEARCH_STEPS; i++) {
        if (!doneN) {
            lumaEndN = FxaaLumaSample(posN);
        }
        if (!doneP) {
            lumaEndP = FxaaLumaSample(posP);
        }

        doneN = doneN || (abs(lumaEndN - lumaN) >= gradientN);
        doneP = doneP || (abs(lumaEndP - lumaN) >= gradientN);

        if (doneN && doneP) {
            break;
        }
        if (!doneN) {
            posN -= offNP;
        }
        if (!doneP) {
            posP += offNP;
        }
    }

    float dstN = horzSpan ? pos.x - posN.x : pos.y - posN.y;
    const float dstP = horzSpan ? posP.x - pos.x : posP.y - pos.y;
    const bool directionN = dstN < dstP;
    lumaEndN = directionN ? lumaEndN : lumaEndP;

    if (((lumaM - lumaN) < 0.0) == ((lumaEndN - lumaN) < 0.0)) {
        lengthSign = 0.0;
    }

    const float spanLength = (dstP + dstN);
    dstN = directionN ? dstN : dstP;
    const float subPixelOffset = (0.5 + (dstN * (-1.0 / spanLength))) * lengthSign;
    const float3 rgbF = source.SampleLevel(aaLinearSampler, float2(
        pos.x + (horzSpan ? 0.0 : subPixelOffset),
        pos.y + (horzSpan ? subPixelOffset : 0.0)), 0).rgb;
    target[id.xy] = float4(FxaaLerp3(rgbL, rgbF, blendL), center.a);
}
