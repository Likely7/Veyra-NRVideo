// Estimate the background on HUD pixels. Keep the opaque body and
// the cleaned translucent fringe separate for the final composite.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float> confidenceTex : register(t2);
Texture2D<float4> previousMask : register(t3);
Texture2D<float4> motionTex : register(t4);
Texture2D<float4> appearanceTex : register(t5);
Texture2D<float4> previousClean : register(t6);
RWTexture2D<float4> cleanedTex : register(u0);
RWTexture2D<float4> maskTex : register(u1); // opacity, cleaned, dynamic pair, unused
RWTexture2D<uint> counts : register(u2);

static const int tileSide=16;
static const int halo=5;
static const int cacheSide=tileSide+2*halo;
groupshared uint localMask[cacheSide*cacheSide];
groupshared uint tileHasMask;

uint maskAt(int2 p) {
    if(!roiContains(p))return 0;
    const uint age=uint(round(appearanceTex.Load(int3(p,0)).a*255.0));
    if((age&128u)!=0)return 2;
    return age==0&&confidenceTex.Load(int3(p,0))>=0.61?1:0;
}
bool coreAt(int2 p) { return maskAt(p)==1; }
bool cachedCore(int2 p) {
    return localMask[p.y*cacheSide+p.x]==1;
}
bool nearPair(int2 p,int region) {
    [loop] for(int y=-2;y<=2;++y) {
        [loop] for(int x=-2;x<=2;++x) {
            const int2 q=p+int2(x,y);
            if(maskAt(q)==2&&
               hudRegion(q+int2(roiOrigin))==region)return true;
        }
    }
    return false;
}
bool nearestCore(int2 cached,int radius,out int2 offset) {
    offset=0;
    int best=2*radius*radius+1;
    [loop] for(int y=-radius;y<=radius;++y) {
        [loop] for(int x=-radius;x<=radius;++x) {
            const int distance=x*x+y*y;
            if(distance>=best||!cachedCore(cached+int2(x,y)))continue;
            best=distance;
            offset=int2(x,y);
        }
    }
    return best<=2*radius*radius;
}
bool temporalBackground(int2 p,int2 shift,out float4 background) {
    background=0;
    const int2 q=p+shift,local=q-int2(roiOrigin);
    if(!frameContains(q))return false;
    if(roiContains(local)) {
        const float3 mask=previousMask.Load(int3(local,0)).rgb;
        if(mask.y>0) {
            background=previousClean.Load(int3(q,0));
            return true;
        }
        if(mask.x>0||mask.z>0||coreAt(local))return false;
    }
    background=previousTex.Load(int3(q,0));
    return true;
}
bool estimateBackground(int2 p,int2 shift,int feather,out float4 background) {
    if(temporalBackground(p,shift,background))return true;

    // If the shifted sample still lies on HUD, bridge only a thin stroke.
    // Verify both adjacent samples against the moving frame before filling.
    const int2 directions[4]={int2(1,0),int2(0,1),int2(1,1),int2(1,-1)};
    [loop] for(int radius=feather+1;radius<=2*feather+2;radius+=2) {
        float best=0.055;
        bool found=false;
        [unroll] for(int i=0;i<4;++i) {
            const int2 lo=p-radius*directions[i],hi=p+radius*directions[i];
            float4 a,b;
            if(!frameContains(lo)||!frameContains(hi)||
               !temporalBackground(lo,shift,a)||!temporalBackground(hi,shift,b))continue;
            const float3 ca=comparisonColor(a.rgb),cb=comparisonColor(b.rgb);
            const float span=colorError(ca,cb);
            if(span>=best||colorError(ca,comparisonColor(currentTex.Load(int3(lo,0)).rgb))>0.035||
               colorError(cb,comparisonColor(currentTex.Load(int3(hi,0)).rgb))>0.035)continue;
            background=(a+b)*0.5;
            best=span;
            found=true;
        }
        if(found)return true;
    }
    return false;
}
bool spatialTap(int2 p,out float4 value) {
    value=0;
    if(!frameContains(p))return false;
    const int2 local=p-int2(roiOrigin);
    if(maskAt(local)!=0||coreAt(local+int2(1,0))||coreAt(local-int2(1,0))||
       coreAt(local+int2(0,1))||coreAt(local-int2(0,1)))return false;
    value=currentTex.Load(int3(p,0));
    return true;
}
bool spatialBackground(int2 p,int feather,out float4 value) {
    const int2 directions[4]={int2(1,0),int2(0,1),int2(1,1),int2(1,-1)};
    // Pull from progressively larger rings of unmasked pixels. Push the
    // closest coherent pair into the hole, favoring pairs along an edge.
    float cost=2.0;
    value=0;
    float4 sum=0;
    uint valid=0;
    [loop] for(int level=0;level<3;++level) {
        const int radius=(feather+1)<<level;
        [unroll] for(int i=0;i<4;++i) {
            float4 a,b;
            const bool lo=spatialTap(p-radius*directions[i],a);
            const bool hi=spatialTap(p+radius*directions[i],b);
            sum+=a+b;
            valid+=uint(lo)+uint(hi);
            if(!lo||!hi)continue;
            const float error=colorError(comparisonColor(a.rgb),comparisonColor(b.rgb))+0.026*level;
            if(error<cost) { cost=error; value=(a+b)*0.5; }
        }
        if(cost<0.11)break;
    }
    if(cost<2.0)return true;
    if(!valid)return false;
    value=sum/valid;
    return true;
}
bool backgroundAt(int2 p,float4 motion,int feather,out float4 value) {
    if(motion.z>0&&estimateBackground(p,int2(motion.xy),feather,value))return true;
    return spatialBackground(p,feather,value);
}
float fringeOpacity(float3 delta,float3 reference) {
    const float power=dot(reference,reference);
    if(power<0.004)return 0;
    const float alpha=dot(delta,reference)/power;
    if(alpha<=0.02||colorError(delta,0)<0.014||
       dot(delta-alpha*reference,delta-alpha*reference)>max(0.00032,0.45*dot(delta,delta)))return 0;
    return alpha>=0.73?1:alpha;
}

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID,uint3 group : SV_GroupID,uint3 thread : SV_GroupThreadID,uint lane : SV_GroupIndex) {
    if(lane==0)tileHasMask=0;
    GroupMemoryBarrierWithGroupSync();
    const int2 tileOrigin=int2(group.xy)*tileSide-halo;
    [loop] for(uint i=lane;i<cacheSide*cacheSide;i+=tileSide*tileSide) {
        const int2 p=tileOrigin+int2(i%cacheSide,i/cacheSide);
        const uint state=resetHistory?0:maskAt(p);
        localMask[i]=state;
        if(state!=0)InterlockedOr(tileHasMask,1);
    }
    GroupMemoryBarrierWithGroupSync();
    if(any(id.xy>=roiSize))return;
    maskTex[id.xy]=0;
    if(!tileHasMask)return;
    const int2 local=int2(id.xy),pixel=local+int2(roiOrigin);
    const int region=hudRegion(pixel);
    if(region<0)return;

    const uint count=counts[hudCounter(region,HudDensity)];
    if(count==0||float(count)>allowedDensity(region))return;
    const uint state=localMask[(thread.y+halo)*cacheSide+thread.x+halo];
    bool dynamic=state==2;
    if(!dynamic&&region>0&&colorError(comparisonColor(currentTex.Load(int3(pixel,0)).rgb),
        comparisonColor(previousTex.Load(int3(pixel,0)).rgb))>0.003)dynamic=nearPair(local,region);
    if(dynamic) {
        // Rail-delimited changes belong to this A/B pair, even on a cut.
        maskTex[id.xy]=float4(0,0,1,0);
        return;
    }

    const bool core=state==1;
    const int feather=clamp(int(round(float(frameSize.y)/360)),3,halo);
    int2 anchorOffset=0;
    if(!core&&!nearestCore(int2(thread.xy)+halo,feather,anchorOffset))return;
    const float4 motion=motionTex.Load(int3(region,0,0));
    float4 background;
    if(!backgroundAt(pixel,motion,feather,background)) {
        if(core)maskTex[id.xy]=float4(1,0,0,0);
        return;
    }

    const float4 real=currentTex.Load(int3(pixel,0));
    float alpha=1;
    if(!core) {
        const int2 center=pixel+anchorOffset;
        if(hudRegion(center)!=region)return;
        float4 anchorBackground;
        if(!backgroundAt(center,motion,feather,anchorBackground))return;
        const float3 current=comparisonColor(real.rgb);
        const float3 stroke=comparisonColor(currentTex.Load(int3(center,0)).rgb);
        const float3 before=comparisonColor(previousTex.Load(int3(pixel,0)).rgb);
        const float3 ref=stroke-comparisonColor(anchorBackground.rgb);
        // A solid tip along a learned stroke can have too little contrast
        // against its own background for alpha fitting.
        const bool solid=colorError(current,stroke)<0.11&&colorError(current,before)<0.065;
        alpha=solid?1:fringeOpacity(current-comparisonColor(background.rgb),ref);
        if(alpha==0)return;
    }
    cleanedTex[pixel]=float4(background.rgb,real.a);
    maskTex[id.xy]=float4(alpha,1,0,0);
    if(core)InterlockedAdd(counts[hudCounter(region,HudRepaired)],1);
}
