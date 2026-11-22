// Estimate integer current -> previous displacement using a ring around each ROI.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
RWTexture2D<float4> motionTex : register(u0); // xy displacement, z trusted, w cut
RWTexture2D<uint> counts : register(u1);

static const int kSize=128;
static const int kTaps=16;
groupshared int2 points[kTaps];
groupshared float3 currentColors[kTaps];
groupshared float errors[kSize];
groupshared int2 displacements[kSize];
groupshared int2 coarseBest;
groupshared float staticError;

float matchError(int2 shift) {
    float sum=0;
    uint valid=0;
    [unroll] for(int i=0;i<kTaps;++i) {
        const int2 q=points[i]+shift;
        if(!frameContains(q))continue;
        const float3 a=comparisonColor(previousTex.Load(int3(q,0)).rgb);
        const float3 diff=a-currentColors[i];
        sum+=dot(diff,diff)/3;
        ++valid;
    }
    // Ignore candidates that lose most of their support at frame edges.
    return valid>=8?sum/valid+0.000002*(kTaps-valid):1e5;
}
float matchCost(int2 shift) {
    return matchError(shift)+1e-9*dot(float2(shift),float2(shift));
}
void reduce(uint lane) {
    GroupMemoryBarrierWithGroupSync();
    [unroll] for(uint halfSize=kSize/2;halfSize>0;halfSize/=2) {
        if(lane<halfSize&&errors[lane+halfSize]<errors[lane]) {
            errors[lane]=errors[lane+halfSize];
            displacements[lane]=displacements[lane+halfSize];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
[numthreads(kSize,1,1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
    const uint region=group.x;
    const float4 b=regionBox(region);
    if(lane==0)counts[uint2(region,0)]=0;
    if(resetHistory||any(b.zw<=b.xy)) {
        if(lane==0)motionTex[uint2(region,0)]=0;
        return;
    }
    const int step=clamp(int(round(float(frameSize.y)/180)),4,16);
    if(lane<kTaps) {
        // Nonuniform ring taps reduce aliasing on tiled and repeated textures.
        const float t=((lane%4)*0.23+0.13);
        const float2 lo=b.xy-2.7*step;
        const float2 hi=b.zw+2.7*step;
        const float2 p=lerp(lo,hi,lane<4?float2(t,0):lane<8?float2(1,t):lane<12?float2(t,1):float2(0,t));
        points[lane]=boundPixel(int2(p));
        currentColors[lane]=comparisonColor(currentTex.Load(int3(points[lane],0)).rgb);
    }
    GroupMemoryBarrierWithGroupSync();
    if(lane==0)staticError=matchError(int2(0,0));
    GroupMemoryBarrierWithGroupSync();
    if(staticError<=0.00008) {
        if(lane==0)motionTex[uint2(region,0)]=0;
        return;
    }
    float best=1e6;
    int2 shift=0;
    // About 1/15 of the frame height in either direction, capped at 192 px.
    const int radius=12;
    const int wide=radius*2+1;
    for(int i=int(lane);i<wide*wide;i+=kSize) {
        const int2 candidate=(int2(i%wide,i/wide)-radius)*step;
        const float error=matchCost(candidate);
        if(error<best){ best=error; shift=candidate; }
    }
    errors[lane]=best;
    displacements[lane]=shift;
    reduce(lane);
    if(lane==0)coarseBest=displacements[0];
    GroupMemoryBarrierWithGroupSync();

    const int fine=step*2-1;
    best=1e6;
    shift=0;
    for(int i=int(lane);i<fine*fine;i+=kSize) {
        const int2 candidate=coarseBest+int2(i%fine,i/fine)-(step-1);
        const float error=matchCost(candidate);
        if(error<best){ best=error; shift=candidate; }
    }
    errors[lane]=best;
    displacements[lane]=shift;
    reduce(lane);
    if(lane==0) {
        const float2 motion=float2(displacements[0]);
        const bool trusted=errors[0]<min(0.004,staticError*0.58) && dot(motion,motion)>2.25;
        motionTex[uint2(region,0)]=float4(trusted?motion:0,trusted?1:0,!trusted&&staticError>0.024?1:0);
    }
}
