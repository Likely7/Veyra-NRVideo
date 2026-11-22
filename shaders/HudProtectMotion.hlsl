// Match background in current -> previous work pixels. Candidates are
// compared on the surrounding image, away from the covered HUD.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float2> denseFlow : register(t2);
Texture2D<float4> previousMotion : register(t3);
RWTexture2D<float4> motionTex : register(u0); // xy displacement, z: moving +1, still -1, unknown 0; w cut
RWTexture2D<uint> counts : register(u1);

static const int kTaps=128;
static const int kWide=25;
static const int kCandidates=kWide*kWide;
static const float kMissingCost=1.;
groupshared int2 points[kTaps];
groupshared float3 currentColors[kTaps];
groupshared float2 flowSamples[kTaps];
groupshared int currentSupport;
groupshared float zeroError, cutError;
groupshared float coarseErrors[kCandidates];
groupshared float errors[kTaps];
groupshared int2 offsets[kTaps];
groupshared float primaryError, alternativeError;
groupshared int2 primaryShift, alternativeShift;

// Trim the influence of moving objects that cover part of the ring.
// Keeping the actual colour residual also rejects exposure jumps.
float matchError(int2 shift,bool unclipped=false,int stride=1) {
    float sum=0;
    uint valid=0;
    [loop] for(int i=0;i<kTaps;i+=stride) {
        const int2 p=points[i],q=p+shift;
        if(!frameContains(p)||!frameContains(q))continue;
        const float3 delta=comparisonColor(previousTex.Load(int3(q,0)).rgb)-currentColors[i];
        const float error=dot(delta,delta)/3;
        sum+=unclipped?error:min(error,0.0144);
        ++valid;
    }
    const uint coverage=valid*stride;
    if(coverage<uint(currentSupport)*3/4||coverage<24)return kMissingCost;
    return sum/valid+0.000025*max(0,currentSupport-int(coverage));
}
float offsetCost(float error,int2 offset) {
    return error+1e-10*dot(float2(offset),float2(offset));
}
void reduce(uint lane) {
    GroupMemoryBarrierWithGroupSync();
    [unroll] for(uint span=kTaps/2;span>0;span/=2) {
        if(lane<span&&offsetCost(errors[lane+span],offsets[lane+span])<offsetCost(errors[lane],offsets[lane])) {
            errors[lane]=errors[lane+span];
            offsets[lane]=offsets[lane+span];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
int2 gridOffset(int index,int step) {
    return (int2(index%kWide,index/kWide)-kWide/2)*step;
}
bool separated(int2 first,int2 second,int radius) {
    const int2 distance=abs(first-second);
    return max(distance.x,distance.y)>=radius;
}
void testOffset(int2 candidate,inout float best,inout int2 offset) {
    const float error=matchError(candidate);
    if(offsetCost(error,candidate)<offsetCost(best,offset)){best=error;offset=candidate;}
}
void refine(uint lane,int2 center,int radius,int step) {
    // Always evaluate the center, even when the search edge is not a
    // multiple of the stride.
    float best=kMissingCost;
    int2 offset=center;
    const int side=2*(radius/step)+1;
    [loop] for(int i=int(lane);i<side*side;i+=kTaps)
        testOffset(center+(int2(i%side,i/side)-side/2)*step,best,offset);
    errors[lane]=best;
    offsets[lane]=offset;
    reduce(lane);
}

[numthreads(kTaps,1,1)]
void main(uint3 group : SV_GroupID,uint lane : SV_GroupIndex) {
    const uint region=group.x;
    const float4 box=regionBox(region);
    const uint2 dst=uint2(region,0);
    if(lane==0)counts[dst]=0;
    if(resetHistory||any(box.zw<=box.xy)) {
        if(lane==0)motionTex[dst]=0;
        return;
    }
    const int step=clamp(int(round(float(frameSize.y)/180)),4,16);
    const uint side=(lane/8)%4, ring=lane/32;
    const float t=(float(lane%8)+0.31+0.13*float(ring))/8.;
    const float margin=(1.+2.5*ring)*step;
    const float2 lo=box.xy-margin,hi=box.zw+margin;
    const float2 path=side==0?float2(t,0):side==1?float2(1,t):side==2?float2(t,1):float2(0,t);
    const int2 p=int2(floor(lerp(lo,hi,path)));
    const bool valid=frameContains(p);
    points[lane]=p;
    currentColors[lane]=valid?comparisonColor(currentTex.Load(int3(p,0)).rgb):0;
    if(readDenseFlow)flowSamples[lane]=valid?clamp(denseFlow.Load(int3(p,0)),-4096.,4096.):65536.;
    GroupMemoryBarrierWithGroupSync();

    if(lane==0) {
        currentSupport=0;
        [loop] for(int i=0;i<kTaps;++i)currentSupport+=frameContains(points[i])?1:0;
        zeroError=matchError(int2(0,0));
        cutError=matchError(int2(0,0),true);
    }
    GroupMemoryBarrierWithGroupSync();
    if(currentSupport<24||zeroError<=0.000055) {
        // A verified still background may support screen-space tracking,
        // but it must never be used to overwrite interpolator inputs.
        if(lane==0)motionTex[dst]=float4(0,0,currentSupport>=24?-1:0,0);
        return;
    }
    if(readDenseFlow) {
        // Component-wise median, followed by the same image checks as
        // every other candidate. HUD and outlier flows cannot win alone.
        [unroll] for(uint size=2;size<=kTaps;size*=2) {
            [unroll] for(uint stride=size/2;stride>0;stride/=2) {
                const uint other=lane^stride;
                if(lane<other) {
                    const float2 a=flowSamples[lane],b=flowSamples[other];
                    const bool forward=(lane&size)==0;
                    flowSamples[lane]=forward?min(a,b):max(a,b);
                    flowSamples[other]=forward?max(a,b):min(a,b);
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }
    }

    float best=kMissingCost;
    int2 offset=0;
    [loop] for(int i=int(lane);i<kCandidates;i+=kTaps) {
        const int2 shift=gridOffset(i,step);
        const float error=matchError(shift,false,2);
        coarseErrors[i]=error;
        if(offsetCost(error,shift)<offsetCost(best,offset)){best=error;offset=shift;}
    }
    const float4 prior=previousMotion.Load(int3(dst,0));
    if(lane==0&&readDenseFlow)testOffset(int2(round(flowSamples[(currentSupport-1)/2])),best,offset);
    if(lane==1&&prior.z>0)testOffset(int2(prior.xy),best,offset);
    errors[lane]=best;
    offsets[lane]=offset;
    reduce(lane);
    const int2 coarse=offsets[0];
    GroupMemoryBarrierWithGroupSync();
    best=kMissingCost;
    offset=0;
    [loop] for(int i=int(lane);i<kCandidates;i+=kTaps) {
        const int2 shift=gridOffset(i,step);
        if(!separated(shift,coarse,3*step))continue;
        const float error=coarseErrors[i];
        if(offsetCost(error,shift)<offsetCost(best,offset)){best=error;offset=shift;}
    }
    errors[lane]=best;
    offsets[lane]=offset;
    reduce(lane);
    const int2 runnerUp=offsets[0];
    GroupMemoryBarrierWithGroupSync();
    const int fineStep=max(step/4,1);
    refine(lane,coarse,step,fineStep);
    if(fineStep>1) {
        const int2 seed=offsets[0];
        GroupMemoryBarrierWithGroupSync();
        refine(lane,seed,fineStep,1);
    }
    if(lane==0){primaryShift=offsets[0];primaryError=errors[0];}
    GroupMemoryBarrierWithGroupSync();
    refine(lane,runnerUp,step,fineStep);
    if(fineStep>1) {
        const int2 seed=offsets[0];
        GroupMemoryBarrierWithGroupSync();
        refine(lane,seed,fineStep,1);
    }
    if(lane==0){alternativeShift=offsets[0];alternativeError=errors[0];}
    GroupMemoryBarrierWithGroupSync();
    if(lane==0) {
        const bool swapped=alternativeError<primaryError;
        const int2 shift=swapped?alternativeShift:primaryShift;
        const float error=min(primaryError,alternativeError);
        const float competitor=max(primaryError,alternativeError);
        const bool separate=separated(primaryShift,alternativeShift,max(3,step/2));
        const bool unique=!separate||error+0.000002<competitor*0.78;
        const bool consistent=prior.z>0&&!separated(shift,int2(prior.xy),max(3,step));
        const bool trusted=(unique||(consistent&&error+0.000004<competitor*0.93))
            &&error<min(0.003,zeroError*0.64)&&dot(float2(shift),float2(shift))>2.25;
        motionTex[dst]=float4(trusted?float2(shift):0,trusted?1:0,!trusted&&cutError>0.024?1:0);
    }
}
