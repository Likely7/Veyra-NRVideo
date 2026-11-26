#include "HudProtect.hlsli"
#include "HdrColor.hlsli"

Texture2D<float4> generatedTex : register(t0);
Texture2D<float4> heldTex : register(t1);
Texture2D<float4> maskTex : register(t2);
Texture2D<float4> pairMask : register(t3);
Texture2D<float4> heldClean : register(t4);
Texture2D<float4> otherTex : register(t5);
Texture2D<uint> currentCounts : register(t6);
Texture2D<uint> previousCounts : register(t7);
RWTexture2D<float4> scratchTex : register(u0);

float3 decode(float3 c) {
    if(outputMode==2)return HdrDecodePq(c);
    if(outputMode==1)return c;
    return select(c<=0.04045,c/12.92,pow((max(c,0)+0.055)/1.055,2.4));
}
float3 encode(float3 c) {
    if(outputMode==2)return HdrEncodePq(c);
    if(outputMode==1)return c;
    return saturate(select(c<=0.0031308,c*12.92,1.055*pow(max(c,0),1.0/2.4)-0.055));
}
// The fallback follows the estimated centre mark. Manual bounds restrict
// the same detector, and never stand in for a detected stroke.
float centeredFallback(uint2 p,float4 box) {
    const float2 distance=abs(float2(p)+0.5-(box.xy+box.zw)*0.5);
    const float feather=max(1.0,float(frameSize.y)/1080.0);
    const uint shape=currentCounts.Load(int3(hudCounter(0,HudCentredMark),0))|
                         previousCounts.Load(int3(hudCounter(0,HudCentredMark),0));
    if(!shape)return 0;
    if((shape&HudDotMark)!=0) {
        const float reach=hudDotProbeRadius()+1.5;
        return 1-smoothstep(reach,reach+feather,length(distance));
    }
    const float width=max(1.0,float(frameSize.y)/720.0);
    const float reach=hudCenterReach();
    if(min(distance.x,distance.y)>width+feather||
       max(distance.x,distance.y)>reach+feather)return 0;
    return (1-smoothstep(width,width+feather,min(distance.x,distance.y)))*
           (1-smoothstep(reach,reach+feather,max(distance.x,distance.y)));
}
float manualBlend(uint r,uint2 p) {
    if(regionFeather<=0)return 1;
    const float4 box=regionBox(r);
    const float2 radius=(box.zw-box.xy)*0.5;
    const float2 distance=abs(float2(p)+0.5-hudCenter(box));
    const float feather=min(regionFeather,min(radius.x,radius.y));
    if(hudRegions[r].x>hudRegions[r].z) {
        const float edge=length(distance/radius);
        return 1-smoothstep(max(0.,1.-feather/min(radius.x,radius.y)),1.,edge);
    }
    const float2 inset=radius-distance;
    return smoothstep(0.,feather,min(inset.x,inset.y));
}
float holdWeight(uint2 p) {
    const uint r=0,minimum=3;
    const float4 box=regionBox(r);
    if(any(box.zw<=box.xy)||hudRegion(int2(p))!=0)return 0;
    if(any(abs(float2(p)+0.5-hudCenter(box))>hudCenterReach()+3))return 0;
    const float coverage=centeredFallback(p,box);
    if(coverage<=0)return 0;
    const float limit=allowedDensity(r);
    const uint aCore=currentCounts.Load(int3(hudCounter(r,HudCore),0));
    const uint bCore=previousCounts.Load(int3(hudCounter(r,HudCore),0));
    const uint aEvidence=currentCounts.Load(int3(hudCounter(r,HudEvidence),0));
    const uint bEvidence=previousCounts.Load(int3(hudCounter(r,HudEvidence),0));
    const bool aValid=currentCounts.Load(int3(hudCounter(r,HudDensity),0))<=limit;
    const bool bValid=previousCounts.Load(int3(hudCounter(r,HudDensity),0))<=limit;
    const bool anyStroke=(aValid&&(aCore>=minimum||(aEvidence>=minimum&&aEvidence<=limit)))||
                         (bValid&&(bCore>=minimum||(bEvidence>=minimum&&bEvidence<=limit)));
    if(!anyStroke)return 0;
    const bool aClean=aValid&&aCore>=minimum&&currentCounts.Load(int3(hudCounter(r,HudRepaired),0))>=aCore&&
        aCore*10>=currentCounts.Load(int3(hudCounter(r,HudRemembered),0))*9;
    const bool bClean=bValid&&bCore>=minimum&&previousCounts.Load(int3(hudCounter(r,HudRepaired),0))>=bCore&&
        bCore*10>=previousCounts.Load(int3(hudCounter(r,HudRemembered),0))*9;
    return aClean&&bClean?0:coverage;
}

bool stableCentredInk(int2 p,float3 held) {
    const float4 box=regionBox(0);
    const float2 distance=abs(float2(p)+0.5-(box.xy+box.zw)*0.5);
    if(min(distance.x,distance.y)>max(1.0,float(frameSize.y)/720.0)||
       max(distance.x,distance.y)>hudCenterReach())return false;
    // Retain stable pixels along the axes of a freshly observed mark.
    // Dim tips can otherwise be lost when opacity is hard to estimate.
    const uint a=currentCounts.Load(int3(hudCounter(0,HudCentredMark),0));
    const uint b=previousCounts.Load(int3(hudCounter(0,HudCentredMark),0));
    const uint kind=HudFreshMark|HudDotMark;
    const uint body=currentCounts.Load(int3(hudCounter(0,HudCore),0))+
                    previousCounts.Load(int3(hudCounter(0,HudCore),0));
    return ((a&kind)==HudFreshMark||(b&kind)==HudFreshMark)&&body>=6&&hudRegion(p)==0&&
        colorError(comparisonColor(held),comparisonColor(otherTex.Load(int3(p,0)).rgb))<=0.13;
}

bool stableManualInk(int2 p,int region,float3 held) {
    const float3 color=comparisonColor(held);
    if(colorError(color,comparisonColor(otherTex.Load(int3(p,0)).rgb))>0.075)return false;
    const int step=clamp(int(round(float(frameSize.y)/180)),3,14);
    const int2 axes[4]={int2(1,0),int2(0,1),int2(1,1),int2(1,-1)};
    bool thin=false;
    [unroll] for(int i=0;i<4;++i) {
        const int2 a=p-step*axes[i],b=p+step*axes[i];
        if(!frameContains(a)||!frameContains(b))continue;
        if(min(colorError(color,comparisonColor(heldTex.Load(int3(a,0)).rgb)),
               colorError(color,comparisonColor(heldTex.Load(int3(b,0)).rgb)))>0.075)thin=true;
    }
    if(!thin)return false;
    // Borrow only adjacent, same-colour evidence. The selected area is
    // a search boundary and does not determine opacity.
    [unroll] for(int k=0;k<4;++k) {
        [unroll] for(int sign=-1;sign<=1;sign+=2) {
            const int2 q=p+sign*axes[k],local=q-int2(roiOrigin);
            if(!roiContains(local)||hudRegion(q)!=region)continue;
            const float core=max(maskTex.Load(int3(local,0)).r,pairMask.Load(int3(local,0)).r);
            if(core>=0.75&&colorError(color,comparisonColor(heldTex.Load(int3(q,0)).rgb))<0.16)return true;
        }
    }
    return false;
}

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const uint2 p=id.xy+roiOrigin;
    const float4 generated=generatedTex.Load(int3(p,0));
    const float hold=holdWeight(p);
    if(hold>=1) {
        scratchTex[id.xy]=float4(heldTex.Load(int3(p,0)).rgb,generated.a);
        return;
    }
    const float4 mask=maskTex.Load(int3(id.xy,0));
    const float3 currentMask=pairMask.Load(int3(id.xy,0)).rgb;
    const float3 held=heldTex.Load(int3(p,0)).rgb;
    const int region=hudRegion(int2(p));
    if(region==0&&mask.x<1&&stableCentredInk(int2(p),held)) {
        // A narrow, stable tip may cross a background edge while the HUD
        // stays intact. Recover the held stroke without copying the ROI.
        scratchTex[id.xy]=float4(held,generated.a);
        return;
    }
    float3 rgb=generated.rgb;
    bool manualTip=false;
    if(region>0&&mask.x<1&&currentMask.z==0) {
        const uint count=currentCounts.Load(int3(hudCounter(region,HudCore),0))+
                         previousCounts.Load(int3(hudCounter(region,HudCore),0));
        manualTip=count>=4&&stableManualInk(int2(p),region,held);
    }
    const bool stroke=mask.x>0||currentMask.x>=1||currentMask.z>0||manualTip||mask.w>0;
    if(stroke) {
        // The current frame can corroborate a stroke that had not yet been
        // tracked when the held frame first arrived. Compare the actual
        // screen pixels before borrowing that evidence across the A/B pair.
        const bool sameStroke=currentMask.x>=1&&colorError(comparisonColor(held),
            comparisonColor(otherTex.Load(int3(p,0)).rgb))<=0.095;
        const bool opaque=mask.x>=1||currentMask.z>0||sameStroke||manualTip;
        const float3 clean=heldClean.Load(int3(p,0)).rgb;
        // The held pixel already includes the HUD. Replace just its
        // estimated background contribution with the interpolated scene.
        if(opaque)rgb=held;
        else if(mask.y>0)rgb=encode(decode(held)+(1-mask.x)*(decode(rgb)-decode(clean)));
    }
    if(mask.w>=1)rgb=held;
    else if(mask.w>0)rgb=encode(lerp(decode(rgb),decode(held),mask.w));
    if(hold>0)rgb=encode(lerp(decode(rgb),decode(held),hold));
    if(region>0&&stroke) {
        const float weight=manualBlend(region,p);
        if(weight<1)rgb=encode(lerp(decode(generated.rgb),decode(rgb),weight));
    }
    scratchTex[id.xy]=float4(rgb,generated.a);
}
