// R5.3 standalone RGBA16F grade. Reuse the same colour algorithm/tables as
// fused ingress; only the working-domain scale and resource binding differ.
#include "ColorGrade.hlsli"
cbuffer Params : register(b0) {
    uint width; uint height; float workingToScene; float exposureOnly;
    float4 row0; float4 row1; float4 row2; float4 controls; float4 flags;
};
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if(p.x>=width||p.y>=height)return;
    float4 color=source[p.xy];
    if(flags.x>0.5){
        if(exposureOnly>0.5){
            // Pure exposure is a linear gain in both scene and scRGB units.
            // Avoid otherwise-neutral table/gamma and working-scale round trips.
            // The legacy fused ingress shader is deliberately unchanged.
            color.rgb*=exp2(controls.x);
        }else{
            ColorGradeParams grade={row0,row1,row2,controls,flags};
            color.rgb=ColorGradeApply(color.rgb*workingToScene,grade)/workingToScene;
        }
    }
    target[p.xy]=color;
}
