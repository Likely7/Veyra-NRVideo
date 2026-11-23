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
Texture2D<float4> motionTex : register(t8);
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
float holdWeight(uint2 p) {
    float weight=0;
    const float scale=float(frameSize.y)/2160.0;
    const float feather=max(4.0,16.0*scale);
    [loop] for(uint r=0;r<5;++r) {
        const float4 box=regionBox(r);
        if(any(box.zw<=box.xy))continue;
        const uint minimum=r==0?3:2;
        const float limit=allowedDensity(r);
        const uint aCore=currentCounts.Load(int3(hudCounter(r,HudCore),0));
        const uint bCore=previousCounts.Load(int3(hudCounter(r,HudCore),0));
        const uint aEvidence=currentCounts.Load(int3(hudCounter(r,HudEvidence),0));
        const uint bEvidence=previousCounts.Load(int3(hudCounter(r,HudEvidence),0));
        const bool aValid=currentCounts.Load(int3(hudCounter(r,HudDensity),0))<=limit;
        const bool bValid=previousCounts.Load(int3(hudCounter(r,HudDensity),0))<=limit;
        const bool anyStroke=(aValid&&(aCore>=minimum||(aEvidence>=minimum&&aEvidence<=limit)))||
                             (bValid&&(bCore>=minimum||(bEvidence>=minimum&&bEvidence<=limit)));
        if(!anyStroke)continue;
        const bool aClean=aValid&&aCore>=minimum&&currentCounts.Load(int3(hudCounter(r,HudRepaired),0))>=aCore&&
            aCore*10>=currentCounts.Load(int3(hudCounter(r,HudRemembered),0))*9;
        const bool bClean=bValid&&bCore>=minimum&&previousCounts.Load(int3(hudCounter(r,HudRepaired),0))>=bCore&&
            bCore*10>=previousCounts.Load(int3(hudCounter(r,HudRemembered),0))*9;
        if(aClean&&bClean)continue;
        const float4 motion=motionTex.Load(int3(r,0,0));
        const float2 reach=motion.z>0?min(abs(motion.xy)*0.5,208.0*scale):0;
        const float2 radius=max((box.zw-box.xy)*0.5,max(160.0*scale,reach));
        const float2 d=abs(float2(p)+0.5-(box.xy+box.zw)*0.5)-radius;
        weight=max(weight,1.0-smoothstep(0,feather,max(d.x,d.y)));
    }
    return weight;
}

bool stableCentredInk(int2 p,float3 held) {
    const float4 box=regionBox(0);
    const float2 distance=abs(float2(p)+0.5-(box.xy+box.zw)*0.5);
    if(min(distance.x,distance.y)>max(1.0,float(frameSize.y)/720.0)||
       max(distance.x,distance.y)>max(27.0,float(frameSize.y)/36.0))return false;
    // Retain stable pixels along the axes of a freshly observed mark.
    // Dim tips can otherwise be lost when opacity is hard to estimate.
    const uint seen=currentCounts.Load(int3(hudCounter(0,HudCentredMark),0))|
                    previousCounts.Load(int3(hudCounter(0,HudCentredMark),0));
    const uint body=currentCounts.Load(int3(hudCounter(0,HudCore),0))+
                    previousCounts.Load(int3(hudCounter(0,HudCore),0));
    return (seen&HudFreshMark)!=0&&(seen&HudDotMark)==0&&body>=6&&hudRegion(p)==0&&
        colorError(comparisonColor(held),comparisonColor(otherTex.Load(int3(p,0)).rgb))<=0.13;
}

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=composeSize))return;
    const uint2 p=id.xy+composeOrigin;
    const int2 local=int2(p)-int2(roiOrigin);
    const float4 generated=generatedTex.Load(int3(p,0));
    const float hold=holdWeight(p);
    if(hold>=1) {
        scratchTex[id.xy]=float4(heldTex.Load(int3(p,0)).rgb,generated.a);
        return;
    }
    const float2 mask=roiContains(local)?maskTex.Load(int3(local,0)).rg:0;
    const float3 currentMask=roiContains(local)?pairMask.Load(int3(local,0)).rgb:0;
    const float3 held=heldTex.Load(int3(p,0)).rgb;
    if(mask.x<1&&stableCentredInk(int2(p),held)) {
        // A narrow, stable tip may cross a background edge while the HUD
        // stays intact. Recover the held stroke without copying the ROI.
        scratchTex[id.xy]=float4(held,generated.a);
        return;
    }
    float3 rgb=generated.rgb;
    if(mask.x>=1||mask.y>0||currentMask.x>=1||currentMask.z>0) {
        // The current frame can corroborate a stroke that had not yet been
        // tracked when the held frame first arrived. Compare the actual
        // screen pixels before borrowing that evidence across the A/B pair.
        const bool sameStroke=currentMask.x>=1&&colorError(comparisonColor(held),
            comparisonColor(otherTex.Load(int3(p,0)).rgb))<=0.095;
        const bool opaque=mask.x>=1||currentMask.z>0||sameStroke;
        const float3 clean=heldClean.Load(int3(p,0)).rgb;
        // The held pixel already includes the HUD. Replace just its
        // estimated background contribution with the interpolated scene.
        if(opaque)rgb=held;
        else if(mask.y>0)rgb=encode(decode(held)+(1-mask.x)*(decode(rgb)-decode(clean)));
    }
    if(hold>0)rgb=encode(lerp(decode(rgb),decode(held),hold));
    scratchTex[id.xy]=float4(rgb,generated.a);
}
