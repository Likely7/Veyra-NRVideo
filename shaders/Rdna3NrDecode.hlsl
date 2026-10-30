// Same shared colour math, with a GPU-written same-frame completion guard.
#define main ParityDecodeBody
#include "ParityDecode.hlsl"
#undef main
ByteAddressBuffer completedResult : register(t8);
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=parityDimensions.x||id.y>=parityDimensions.y)return;
    if(completedResult.Load(0)!=1) { finalTex[id.xy]=originalTex[id.xy];return; }
    ParityDecodeBody(id);
}
