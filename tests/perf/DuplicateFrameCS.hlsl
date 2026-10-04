// Adapted from SAOG0721/Magpie, src/Magpie.Core/shaders/DuplicateFrameCS.hlsl
// commit 27c5df91177a29b33be612e98274169f3d2fca49 (v0.6.9-experimental), GPL-3.0.
// Retain 8x8 group reduction/one global atomic and 2x2 pixels per thread.
// D3D12 test adaptation: four bounded Load operations replace sampler Gather,
// so odd extents are exact and the existing sampler-free root signature works.
RWBuffer<uint> result : register(u0);
cbuffer CompareOptions : register(b0) {uint compareAlpha;};
groupshared uint groupDifferent;
Texture2D<float4> tex1 : register(t0);
Texture2D<float4> tex2 : register(t1);
[numthreads(8,8,1)]
void main(uint3 tid:SV_GroupThreadID,uint3 gid:SV_GroupID){
    if(all(tid==0))groupDifferent=0;
    GroupMemoryBarrierWithGroupSync();
    const uint2 gxy=(gid.xy<<4)+(tid.xy<<1);
    uint width,height;tex1.GetDimensions(width,height);
    bool different=false;
    [unroll]for(uint y=0;y<2;++y)[unroll]for(uint x=0;x<2;++x){
        const uint2 at=gxy+uint2(x,y);
        if(at.x<width&&at.y<height){
            const float4 a=tex1.Load(int3(at,0)),b=tex2.Load(int3(at,0));
            different=different||(compareAlpha!=0?any(a!=b):any(a.rgb!=b.rgb));
        }
    }
    if(different)InterlockedOr(groupDifferent,1u);
    GroupMemoryBarrierWithGroupSync();
    if(all(tid==0)&&groupDifferent!=0)InterlockedOr(result[0],1u);
}
