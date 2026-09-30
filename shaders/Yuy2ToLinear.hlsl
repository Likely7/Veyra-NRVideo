// Native 4:2:2 capture. One RGBA8 texel stores Y0 U Y1 V for two pixels.
// No CPU RGB expansion, intermediate 8-bit RGB rounding, or 4:2:0 conversion.
#include "HdrColor.hlsli"
#include "ColorGrade.hlsli"
cbuffer Params : register(b0) {
    uint width; uint height; uint transfer; uint reserved;
    float limited; float matrix709; float primaries2020; float padding;
    float4 colorRow0; float4 colorRow1; float4 colorRow2; float4 colorControls; float4 colorFlags;
};
Texture2D<float4> packedYuy2 : register(t0);
RWTexture2D<float4> linearRgb : register(u0);
float decode(float v) {
    if(transfer==0)return v;
    if(transfer==3)return pow(max(v,0),2.4);
    if(transfer==2)return v<0.081?v/4.5:pow((v+0.099)/1.099,1.0/0.45);
    return v<=0.04045?v/12.92:pow((v+0.055)/1.055,2.4);
}
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if(p.x>=width||p.y>=height)return;
    const float4 pair=packedYuy2[uint2(p.x/2,p.y)];
    float y=(p.x&1)?pair.z:pair.x;
    float2 uv=pair.yw-128.0/255.0;
    if(limited>0.5){y=(y-16.0/255.0)*(255.0/219.0);uv*=255.0/224.0;}
    // Preserve head/footroom until the final RGB gamut clamp. Clipping Y
    // before the matrix changes saturated pixels with below-black luma.
    float3 rgb=matrix709>0.5?float3(y+1.5748*uv.y,y-0.187324*uv.x-0.468124*uv.y,y+1.8556*uv.x):
        float3(y+1.402*uv.y,y-0.344136*uv.x-0.714136*uv.y,y+1.772*uv.x);
    if(matrix709>1.5)rgb=float3(y+1.4746*uv.y,y-.164553*uv.x-.571353*uv.y,y+1.8814*uv.x);
    rgb=saturate(rgb);
    float3 decoded=float3(decode(rgb.r),decode(rgb.g),decode(rgb.b));
    if(primaries2020>0.5)decoded=HdrTo709(decoded);
    if(colorFlags.x>0.5){ColorGradeParams grade={colorRow0,colorRow1,colorRow2,colorControls,colorFlags};decoded=ColorGradeApplyWithFlags(decoded,grade);}
    linearRgb[p.xy]=float4(decoded,1);
}
