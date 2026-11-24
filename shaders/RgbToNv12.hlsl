#include "HdrColor.hlsli"
Texture2D<float4> rgb : register(t0);
RWTexture2D<float> yPlane : register(u0);
RWTexture2D<float2> uvPlane : register(u1);
cbuffer Constants : register(b0) {
    uint width; uint height; uint2 reserved;
    uint sourceWidth; uint sourceHeight; uint2 padding;
};
// reserved.x = HDR/PQ target, reserved.y = output dither step in coded units
// (asfloat), so the colour grade's quantisation error becomes noise instead of
// banding. 0 keeps the exact legacy behaviour for every ungraded path.
float3 convert(float3 c) {
    if(reserved.x){
        float y=dot(c,float3(.2627,.6780,.0593));
        return round(float3(64+876*y,512+896*(c.b-y)/1.8814,512+896*(c.r-y)/1.4746))*64.0/65535.0;
    }
    float y=dot(c,float3(0.2126,0.7152,0.0722));
    return float3((16+219*y)/255.0, (128+224*(c.b-y)/1.8556)/255.0, (128+224*(c.r-y)/1.5748)/255.0);
}
float3 inputColor(uint2 p){float3 c=rgb[p].rgb;return reserved.x==1?HdrEncodePq(c):saturate(c);}
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=width||id.y>=height)return;
    // Export leaves the source extent zero and supplies an even-sized texture.
    const uint2 last = uint2(sourceWidth ? sourceWidth : width,
                             sourceHeight ? sourceHeight : height) - 1u;
    const uint2 pixel = min(id.xy, last);
    const float ditherStep=asfloat(reserved.y);
    const float3 color = inputColor(pixel);
    yPlane[id.xy]=convert(color).x+OutputDither(pixel,ditherStep);
    if((id.x&1)==0&&(id.y&1)==0){
        const float3 c = (color + inputColor(min(pixel + uint2(1,0), last)) +
            inputColor(min(pixel + uint2(0,1), last)) +
            inputColor(min(pixel + uint2(1,1), last))) * 0.25;
        uvPlane[id.xy/2]=convert(saturate(c)).yz+OutputDither(id.xy/2,ditherStep);
    }
}
