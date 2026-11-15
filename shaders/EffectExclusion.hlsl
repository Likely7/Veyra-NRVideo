// Restore raw source pixels after the image effects and frame generation.
// Generated A/B subframes hold A. Real B displays B at its own timestamp.
#include "HdrColor.hlsli"
#include "NrProtection.hlsli"
Texture2D<float4> enhancedTex : register(t0);
Texture2D<float4> originalTex : register(t1);
RWTexture2D<float4> outputTex : register(u0);
RWTexture2D<float4> uiTex : register(u1);
SamplerState linearClamp : register(s1);
cbuffer Constants : register(b0) {
    uint width, height;
    float feather, outputMode; // 0 sRGB, 1 scRGB, 2 PQ
    float sdrWhite, ditherStep, writeUi, padding;
    float4 regions[4];
};
float3 decodeSrgb(float3 value) {
    return select(value<=0.04045,value/12.92,pow((max(value,0)+0.055)/1.055,2.4));
}
float3 encodeSrgb(float3 value) {
    value=max(value,0);
    return select(value<=0.0031308,value*12.92,1.055*pow(value,1.0/2.4)-0.055);
}
float3 encoded(float3 value) {
    if(outputMode>1.5)return HdrEncodePq(value);
    return outputMode>0.5?value:encodeSrgb(value);
}
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=width||id.y>=height)return;
    const float weight=NrProtection(float2(id.xy)+0.5,uint2(width,height),1,feather,regions);
    const float4 enhanced=enhancedTex[id.xy];
    if(weight<=0){
        outputTex[id.xy]=enhanced;
        if(writeUi>0)uiTex[id.xy]=0;
        return;
    }
    const float2 uv=(float2(id.xy)+0.5)/float2(width,height);
    float3 original=originalTex.SampleLevel(linearClamp,uv,0).rgb*sdrWhite;
    float3 enhancedLinear=outputMode>1.5?HdrDecodePq(enhanced.rgb):(outputMode>0.5?enhanced.rgb:decodeSrgb(enhanced.rgb));
    float3 result=encoded(lerp(enhancedLinear,original,weight));
    if(outputMode<0.5||outputMode>1.5)result+=OutputDither(id.xy,ditherStep);
    outputTex[id.xy]=float4(result,enhanced.a);
    if(writeUi>0)uiTex[id.xy]=float4(encoded(original)*weight,weight);
}
