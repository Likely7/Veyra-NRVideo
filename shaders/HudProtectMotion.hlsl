// Match background in current -> previous work pixels. Candidates are
// compared on the surrounding image, away from the covered HUD.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float2> denseFlow : register(t2);
Texture2D<float4> previousMotion : register(t3);
Texture2D<uint> previousCounts : register(t4);
RWTexture2D<float4> motionTex : register(u0); // xy displacement, z: moving +1, still -1, unknown 0; w cut
RWTexture2D<uint> counts : register(u1);

static const int kTaps=128;
static const int kFlowStride=4;
static const int kFlowTaps=kTaps/kFlowStride;
static const int kWide=25;
static const int kCandidates=kWide*kWide;
static const float kMissingCost=1.;
groupshared int2 points[kTaps];
groupshared float3 currentColors[kTaps];
groupshared float2 flowSamples[kFlowTaps];
groupshared int currentSupport,denseSupport;
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
int coarseStep(int index,int step) {
    const int distance=abs(index);
    // Dense near zero, sparsely sampled on the quarter-resolution grid
    // farther away. The far grid reaches 396 work pixels at 2160p.
    return sign(index)*(min(distance,5)+4*max(distance-5,0))*step;
}
int2 gridOffset(int index,int step) {
    const int2 rank=int2(index%kWide,index/kWide)-kWide/2;
    return int2(coarseStep(rank.x,step),coarseStep(rank.y,step));
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

float3 colorAt(Texture2D<float4> image,int2 p) {
    return comparisonColor(image.Load(int3(boundPixel(p),0)).rgb);
}
bool strokeInk(int2 p,int2 normal,int radius,out float3 color) {
    color=colorAt(currentTex,p);
    const float3 lo=colorAt(currentTex,p-normal*radius),hi=colorAt(currentTex,p+normal*radius);
    // Test both sides of the mark. A single background edge is insufficient.
    return min(colorError(lo,color),colorError(hi,color))>0.10;
}
uint centredMark(float4 box) {
    const int2 center=int2((box.xy+box.zw)*0.5);
    const int step=clamp(int(round(float(frameSize.y)/1080.0)),1,4);
    const int side=clamp(int(round(float(frameSize.y)/540.0)),2,8);
    uint pairedHorizontal=0,pairedVertical=0;
    uint singleHorizontal=0,singleVertical=0;
    const int2 horizontal=int2(1,0),vertical=int2(0,1);
    [loop] for(int i=3;i<=18;++i) {
        const int d=i*step;
        const int2 l=center-horizontal*d,r=center+horizontal*d;
        const int2 t=center-vertical*d,b=center+vertical*d;
        float3 lc,rc,tc,bc;
        const bool left=strokeInk(l,vertical,side,lc),right=strokeInk(r,vertical,side,rc);
        const bool top=strokeInk(t,horizontal,side,tc),bottom=strokeInk(b,horizontal,side,bc);
        pairedHorizontal+=uint(left&&right&&colorError(lc,rc)<0.085);
        pairedVertical+=uint(top&&bottom&&colorError(tc,bc)<0.085);
        singleHorizontal+=uint(left)+uint(right);
        singleVertical+=uint(top)+uint(bottom);
        if((pairedHorizontal>=2&&pairedVertical>=2)||
           (pairedHorizontal>=4&&singleVertical>=4)||(pairedVertical>=4&&singleHorizontal>=4))return HudFreshMark;
    }

    // A small isolated centre dot also works without opposing arms. Use
    // several directions to distinguish it from an ordinary textured edge.
    const float3 dotColor=colorAt(currentTex,center);
    const float high=max(dotColor.r,max(dotColor.g,dotColor.b));
    const float low=min(dotColor.r,min(dotColor.g,dotColor.b));
    if(high<0.58&&high-low<0.16)return 0;
    if(colorError(dotColor,colorAt(previousTex,center))>0.075)return 0;
    const int2 directions[8]={horizontal,-horizontal,vertical,-vertical,
                              int2(1,1),int2(-1,1),int2(1,-1),int2(-1,-1)};
    const int legacyRadius=max(3,side);
    uint edges=0;
    [unroll] for(int k=0;k<8;++k)
        edges+=uint(colorError(dotColor,colorAt(currentTex,center+legacyRadius*directions[k]))>0.12);
    if(edges>=7)return HudFreshMark;

    // The wider probe is a second path for dots with a larger footprint.
    // Keep the existing thin-mark path and its stroke-tip recovery intact.
    const int dotRadius=hudDotProbeRadius();
    if(dotRadius<=legacyRadius)return 0;
    edges=0;
    [unroll] for(int k=0;k<8;++k)
        edges+=uint(colorError(dotColor,colorAt(currentTex,center+dotRadius*directions[k]))>0.12);
    return edges>=7?(HudFreshMark|HudDotMark):0;
}

[numthreads(kTaps,1,1)]
void main(uint3 group : SV_GroupID,uint lane : SV_GroupIndex) {
    const uint region=group.x;
    float4 box=regionBox(region);
    const uint2 dst=uint2(region,0);
    if(lane<HudCounterCount&&lane!=HudTrusted&&lane!=HudCentredMark)counts[hudCounter(region,lane)]=0;
    if(lane==0) {
        uint shape=0;
        if(!resetHistory&&region==0&&all(box.zw>box.xy)) {
            const uint last=previousCounts.Load(int3(hudCounter(region,HudCentredMark),0));
            // Preserve a learned outline over a brief occlusion or drop in
            // contrast. Appearance must still match before a pixel is used.
            const uint age=last&HudMarkAgeMask;
            const uint fresh=centredMark(box);
            shape=fresh?(HudMarkLifetime|fresh):(age>1?((age-1)|(last&HudDotMark)):0);
        }
        counts[hudCounter(region,HudCentredMark)]=shape;
    }
    if(lane==0)counts[hudCounter(region,HudTrusted)]=0;
    if(resetHistory||any(box.zw<=box.xy)) {
        if(lane==0)motionTex[dst]=0;
        return;
    }
    const int step=4*clamp(int(round(float(frameSize.y)/720)),1,4);
    if(region==0) {
        const float2 center=(box.xy+box.zw)*0.5;
        const float extent=clamp(float(frameSize.y)/42.,16.,96.);
        box=float4(max(box.xy,center-extent),min(box.zw,center+extent));
    }
    const uint side=(lane/8)%4, ring=lane/32;
    const float t=(float(lane%8)+0.31+0.13*float(ring))/8.;
    const float margin=(1.+float(ring)*(tightRegion(region)?0.50:0.90))*step;
    const float2 lo=box.xy-margin,hi=box.zw+margin;
    const float2 path=side==0?float2(t,0):side==1?float2(1,t):side==2?float2(t,1):float2(0,t);
    const int2 p=int2(floor(lerp(lo,hi,path)));
    const bool valid=frameContains(p);
    points[lane]=p;
    currentColors[lane]=valid?comparisonColor(currentTex.Load(int3(p,0)).rgb):0;
    if(readDenseFlow&&lane%kFlowStride==0)
        flowSamples[lane/kFlowStride]=valid?clamp(denseFlow.Load(int3(p,0)),-4096.,4096.):65536.;
    GroupMemoryBarrierWithGroupSync();

    if(lane==0) {
        currentSupport=0;
        denseSupport=0;
        [loop] for(int i=0;i<kTaps;++i) {
            const bool valid=frameContains(points[i]);
            currentSupport+=int(valid);
            denseSupport+=int(valid&&i%kFlowStride==0);
        }
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
        // Take two evenly spaced flow samples per side and ring. The
        // resulting median is a proposal; all 128 colour samples still
        // decide whether the resulting motion is trustworthy.
        [unroll] for(uint size=2;size<=kFlowTaps;size*=2) {
            [unroll] for(uint stride=size/2;stride>0;stride/=2) {
                const uint other=lane^stride;
                if(lane<kFlowTaps&&lane<other) {
                    const float2 a=flowSamples[lane],b=flowSamples[other];
                    const bool forward=(lane&size)==0;
                    flowSamples[lane]=forward?min(a,b):max(a,b);
                    flowSamples[other]=forward?max(a,b):min(a,b);
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }
    }

    // Dense flow already supplies an independent proposal. Coarse ranking
    // can use fewer taps on that path; the two finalists are still refined
    // and checked against the full ring before accepting any reprojection.
    const int searchStride=readDenseFlow?kFlowStride:2;
    float best=kMissingCost;
    int2 offset=0;
    [loop] for(int i=int(lane);i<kCandidates;i+=kTaps) {
        const int2 shift=gridOffset(i,step);
        const float error=matchError(shift,false,searchStride);
        coarseErrors[i]=error;
        if(offsetCost(error,shift)<offsetCost(best,offset)){best=error;offset=shift;}
    }
    const float4 prior=previousMotion.Load(int3(dst,0));
    if(lane==0&&readDenseFlow&&denseSupport>0)testOffset(int2(round(flowSamples[(denseSupport-1)/2])),best,offset);
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
    const bool wide=any(abs(coarse)>5*step);
    refine(lane,coarse,wide?2*step:step,fineStep);
    if(fineStep>1) {
        const int2 seed=offsets[0];
        GroupMemoryBarrierWithGroupSync();
        refine(lane,seed,fineStep,1);
    }
    if(lane==0){primaryShift=offsets[0];primaryError=errors[0];}
    GroupMemoryBarrierWithGroupSync();
    const bool wideRunnerUp=any(abs(runnerUp)>5*step);
    refine(lane,runnerUp,wideRunnerUp?2*step:step,fineStep);
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
        counts[hudCounter(region,HudTrusted)]=trusted?1:0;
    }
}
