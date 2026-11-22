// SPDX-License-Identifier: GPL-3.0-only
// Bindings follow NVIDIA Image Scaling SDK v1.0.3 (see third_party/nis).
#define NIS_HLSL 1
#define NIS_SCALER 1
#define NIS_VIEWPORT_SUPPORT 1
#define NIS_BLOCK_WIDTH 32
#define NIS_BLOCK_HEIGHT 24
#define NIS_THREAD_GROUP_SIZE 256

cbuffer NisConstants : register(b0) {
    float kDetectRatio, kDetectThres, kMinContrastRatio, kRatioNorm;
    float kContrastBoost, kEps, kSharpStartY, kSharpScaleY;
    float kSharpStrengthMin, kSharpStrengthScale, kSharpLimitMin, kSharpLimitScale;
    float kScaleX, kScaleY, kDstNormX, kDstNormY;
    float kSrcNormX, kSrcNormY;
    uint kInputViewportOriginX, kInputViewportOriginY, kInputViewportWidth, kInputViewportHeight;
    uint kOutputViewportOriginX, kOutputViewportOriginY, kOutputViewportWidth, kOutputViewportHeight;
    float reserved0, reserved1;
};

SamplerState samplerLinearClamp : register(s1);
Texture2D<float4> in_texture : register(t0);
Texture2D<float4> coef_scaler : register(t1);
Texture2D<float4> coef_usm : register(t2);
RWTexture2D<float4> out_texture : register(u0);

float4 NisToWorking(float4 value) {
#if NIS_HDR_MODE == 0
    // Decode SDR at the store so later effects receive the linear working signal.
    const float3 rgb = max(value.rgb, 0.0);
    value.rgb = select(rgb <= 0.04045, rgb / 12.92, pow((rgb + 0.055) / 1.055, 2.4));
#endif
    return value;
}

#define NVTEX_STORE(texture, position, value) texture[position] = NisToWorking(value)
#include "../third_party/nis/NIS_Scaler.h"

[numthreads(NIS_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 groupIndex : SV_GroupID, uint3 threadIndex : SV_GroupThreadID) {
    NVScaler(groupIndex.xy, threadIndex.x);
}
