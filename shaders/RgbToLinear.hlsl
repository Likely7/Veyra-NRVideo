#include "HdrColor.hlsli"
#include "HdrToSdr.hlsli"
#include "ColorGrade.hlsli"
cbuffer RgbParams : register(b0) { uint width; uint height; uint transfer; uint limited; float2 reserved; float primaries2020; float padding;
    float4 colorRow0; float4 colorRow1; float4 colorRow2; float4 colorControls; float4 colorFlags;
    // Custom: HDR->SDR tone-map tuning, appended after the colour-grade block
    // (contract in veyra/pipeline/HdrToSdrConstants.h). The defaults equal the
    // values that used to be hard-coded here.
    float sdrWhiteNits; float exposureEv; float shoulder; };
Texture2D<float4> sourceRgb : register(t0);
RWTexture2D<float4> linearRgb : register(u0);
float decode(float v) {
    if(transfer==3)return pow(max(v,0),2.4);
    if (transfer == 2) return v < 0.081 ? v / 4.5 : pow((v + 0.099) / 1.099, 1.0 / 0.45);
    if (transfer == 0) return v;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= width || p.y >= height) return;
    float4 c=sourceRgb[p.xy];
    if(limited!=0)c.rgb=saturate((c.rgb-16.0/255.0)*(255.0/219.0));
    // Present onto the player's opaque black canvas; never discard RGB chroma.
    float3 decoded=float3(decode(c.r),decode(c.g),decode(c.b));
    if(primaries2020>0.5)decoded=HdrTo709(decoded);
    const bool scRgb=transfer==0&&reserved.x>0.5;
    if(colorFlags.x>0.5){ColorGradeParams grade={colorRow0,colorRow1,colorRow2,colorControls,colorFlags};decoded=scRgb?ColorGradeApplyWithFlags(decoded*(80.0/203.0),grade)*(203.0/80.0):ColorGradeApplyWithFlags(decoded,grade);}
    if(scRgb&&reserved.x>1.5)decoded=HdrToSdr(decoded*80.0,reserved.y,sdrWhiteNits,exposureEv,shoulder);
    linearRgb[p.xy]=float4(decoded*c.a,1);
}
