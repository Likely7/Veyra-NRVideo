// Apply rectangular/elliptical exclusions in display-linear light.
// Generated subframes hold enhanced A for Only FG, then raw A for All Effects.
#include "HdrColor.hlsli"
#include "NrProtection.hlsli"

Texture2D<float4> enhancedTex : register(t0);
Texture2D<float4> originalTex : register(t1);
Texture2D<float4> heldTex : register(t2);
Texture2D<float4> hudMask : register(t3);
RWTexture2D<float4> outputTex : register(u0);
RWTexture2D<float4> uiTex : register(u1);
SamplerState linearClamp : register(s1);
cbuffer Constants : register(b0) {
    uint width, height;
    float feather, outputMode; // 0 sRGB, 1 scRGB, 2 PQ
    float sdrWhite, ditherStep;
    uint flags, regionModes;
    float4 regions[4];
    uint4 hudBounds;
};
static const uint WriteUi=1, Generated=2, WriteReal=4, HasHud=8, HistoryValid=16;
float3 decodeSrgb(float3 value) {
    return select(value<=0.04045,value/12.92,pow((max(value,0)+0.055)/1.055,2.4));
}
float3 encodeSrgb(float3 value) {
    value=max(value,0);
    return select(value<=0.0031308,value*12.92,1.055*pow(value,1.0/2.4)-0.055);
}
float3 decode(float3 value) {
    if(outputMode>1.5)return HdrDecodePq(value);
    return outputMode>0.5?value:decodeSrgb(value);
}
float3 encode(float3 value) {
    if(outputMode>1.5)return HdrEncodePq(value);
    return outputMode>0.5?value:encodeSrgb(value);
}
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=width||id.y>=height)return;
    const bool generated=(flags&Generated)!=0, writeUi=(flags&WriteUi)!=0;
    const float2 pixel=float2(id.xy)+0.5;
    const uint2 size=uint2(width,height);
    float allWeight=0, fgWeight=0;
    for(uint i=0;i<4;++i) {
        const uint mode=(regionModes>>(i*2))&3;
        if(mode==0)allWeight=max(allWeight,ProtectionRegionWeight(pixel,size,feather,regions[i]));
        if(mode==1&&(generated||writeUi))fgWeight=max(fgWeight,ProtectionRegionWeight(pixel,size,feather,regions[i]));
    }
    const float4 enhanced=enhancedTex.Load(int3(id.xy,0));
    const float3 held=(fgWeight>0&&(flags&HistoryValid)!=0)?heldTex.Load(int3(id.xy,0)).rgb:enhanced.rgb;
    const float3 original=allWeight>0?originalTex.SampleLevel(linearClamp,pixel/float2(size),0).rgb*sdrWhite:0;

    if((flags&WriteReal)!=0) {
        float3 color=enhanced.rgb;
        if(generated&&fgWeight>0) {
            color=fgWeight>=1?held:encode(lerp(decode(color),decode(held),fgWeight));
        }
        if(allWeight>0) {
            color=encode(lerp(decode(color),original,allWeight));
            if(outputMode<0.5||outputMode>1.5)color+=OutputDither(id.xy,ditherStep);
        }
        outputTex[id.xy]=float4(color,enhanced.a);
    }

    // XeSS receives a premultiplied UI layer alongside the separate HUD-less
    // source. Overlap follows the same order as the graph's generated frames.
    if(writeUi) {
        float4 layer=0;
        if((flags&HasHud)!=0&&all(id.xy>=hudBounds.xy)&&all(id.xy<hudBounds.zw)) {
            const float4 mask=hudMask.Load(int3(id.xy-hudBounds.xy,0));
            const float alpha=max(mask.r,mask.b);
            layer=float4(enhanced.rgb*alpha,alpha);
        }
        layer=layer*(1-fgWeight)+float4(held,1)*fgWeight;
        layer=layer*(1-allWeight)+float4(encode(original),1)*allWeight;
        uiTex[id.xy]=layer;
    }
}
