#include "HudProtect.hlsli"
#include "HdrColor.hlsli"

Texture2D<float4> generatedTex : register(t0);
Texture2D<float4> heldTex : register(t1);
Texture2D<float4> maskTex : register(t2);
Texture2D<float4> pairMask : register(t3);
Texture2D<float4> heldClean : register(t4);
Texture2D<float4> currentTex : register(t5);
RWTexture2D<float4> scratchTex : register(u0);

float3 decode(float3 c) {
    if(outputMode==2)return HdrDecodePq(c);
    if(outputMode==1)return c;
    return select(c<=0.04045,c/12.92,pow((max(c,0)+0.055)/1.055,2.4));
}
float3 encode(float3 c) {
    if(outputMode==2)return HdrEncodePq(c);
    if(outputMode==1)return c;
    return saturate(select(c<=0.0031308,c*12.92,1.055*pow(max(c,0),1.0/2.4)-0.055));
}
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const uint2 p=id.xy+roiOrigin;
    const float4 generated=generatedTex.Load(int3(p,0));
    const float2 mask=maskTex.Load(int3(id.xy,0)).rg;
    const float3 currentMask=pairMask.Load(int3(id.xy,0)).rgb;
    float3 rgb=generated.rgb;
    if(mask.x>=1||mask.y>0||currentMask.x>=1||currentMask.z>0) {
        const float3 held=heldTex.Load(int3(p,0)).rgb;
        // The current frame can corroborate a stroke that had not yet been
        // tracked when the held frame first arrived. Compare the actual
        // screen pixels before borrowing that evidence across the A/B pair.
        const bool sameStroke=currentMask.x>=1&&colorError(comparisonColor(held),
            comparisonColor(currentTex.Load(int3(p,0)).rgb))<=0.095;
        const bool opaque=mask.x>=1||currentMask.z>0||sameStroke;
        const float3 clean=heldClean.Load(int3(p,0)).rgb;
        // The held pixel already includes the HUD. Replace just its
        // estimated background contribution with the interpolated scene.
        if(opaque)rgb=held;
        else if(mask.y>0)rgb=encode(decode(held)+(1-mask.x)*(decode(rgb)-decode(clean)));
    }
    scratchTex[id.xy]=float4(rgb,generated.a);
}
