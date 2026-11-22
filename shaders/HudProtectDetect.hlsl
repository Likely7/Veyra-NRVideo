// Confidence grows on thin, screen-stationary marks that disagree with the moving background.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float> previousConfidence : register(t2);
Texture2D<float4> motionTex : register(t3);
RWTexture2D<float> confidenceTex : register(u0);
RWTexture2D<uint> counts : register(u1);

float3 currentAt(int2 p){ return comparisonColor(currentTex.Load(int3(p,0)).rgb); }
float3 previousAt(int2 p){ return comparisonColor(previousTex.Load(int3(p,0)).rgb); }

bool backgroundNeighbor(int2 p,int2 shift,float3 center) {
    if(!frameContains(p)||!frameContains(p+shift))return false;
    const float3 b=currentAt(p);
    return colorError(b,center)>0.043 && colorError(b,previousAt(p+shift))<0.062;
}
bool thinStroke(int2 p,int2 shift,float3 center) {
    const int k=clamp(int(round(float(frameSize.y)/180)),3,14);
    const int2 directions[4]={int2(k,0),int2(0,k),int2(k,k),int2(k,-k)};
    [unroll] for(uint i=0;i<4;++i) {
        if(backgroundNeighbor(p+directions[i],shift,center)&&backgroundNeighbor(p-directions[i],shift,center))return true;
    }
    return false;
}
// A just-exposed stroke can inherit screen-space evidence from a nearby
// established stroke of the same appearance, even if the background stops.
bool thinContrast(int2 p,float3 color,int step,bool previous) {
    const int2 directions[4]={int2(step,0),int2(0,step),int2(step,step),int2(step,-step)};
    [unroll] for(int i=0;i<4;++i) {
        const int2 a=p+directions[i],b=p-directions[i];
        if(!frameContains(a)||!frameContains(b))continue;
        const float3 left=previous?previousAt(a):currentAt(a);
        const float3 right=previous?previousAt(b):currentAt(b);
        if(colorError(color,left)>0.066&&colorError(color,right)>0.066)return true;
    }
    return false;
}
bool trackedAppearance(int2 p,float3 color,int region,bool previous=false) {
    const int step=clamp(int(round(float(frameSize.y)/180)),3,14);
    if(!thinContrast(p,color,step,previous))return false;
    const int2 directions[8]={int2(1,0),int2(-1,0),int2(0,1),int2(0,-1),
                              int2(1,1),int2(-1,1),int2(1,-1),int2(-1,-1)};
    [loop] for(int distance=1;distance<=3*step;distance+=max(1,step/4)) {
        [unroll] for(int i=0;i<8;++i) {
            const int2 q=p+distance*directions[i],local=q-int2(roiOrigin);
            if(!roiContains(local))continue;
            const float known=previousConfidence.Load(int3(local,0));
            if(known>=0.61&&known<0.998&&colorError(color,previousAt(q))<0.11&&hudRegion(q)==region)return true;
        }
    }
    return false;
}
// Paired, stationary rails delimit a panel with a common inset colour. A
// changing glyph or bar is protected when it covers or uncovers that inset.
// This is restricted to explicit regions and uses the same per-pair mask as
// the crosshair. Adapted from the dynamic HUD experiment (70241356).
struct HudRail {
    float3 edge;
    float3 inset;
    bool found;
};

HudRail findHudRail(int2 pixel,int2 outward,int2 along,int region,int probe,int radius) {
    HudRail missing={float3(0,0,0),float3(0,0,0),false};
    [loop] for(int distance=probe*2+2;distance<=radius;++distance) {
        const int2 p=pixel+outward*distance;
        const int2 a=p-outward*(probe+1),b=p+outward*probe;
        if(!frameContains(p-along*probe)||!frameContains(p+along*probe)||
           !frameContains(a)||!frameContains(b))break;
        const float3 rail=currentAt(p);
        const float3 inset=currentAt(a);
        if(colorError(rail,previousAt(p))>0.035||colorError(inset,previousAt(a))>0.035||
           colorError(rail,inset)<0.10||colorError(rail,currentAt(b))<0.10)continue;
        if(colorError(rail,currentAt(p-along*probe))>0.045||
           colorError(rail,currentAt(p+along*probe))>0.045||
           hudRegion(p)!=region)continue;
        HudRail match={rail,inset,true};
        return match;
    }
    return missing;
}
bool changesOnHudInset(int2 pixel,int2 normal,int2 along,int region,float3 a,float3 b) {
    const int probe=clamp(int(round(float(frameSize.y)/360)),2,7);
    const int radius=probe*7+2;
    const float4 bounds=regionBox(region);
    // Tight panels only. Wide boxes keep the original thin-stroke detector.
    if(dot(abs(float2(normal)),bounds.zw-bounds.xy)>2*radius+8)return false;
    const HudRail lo=findHudRail(pixel,-normal,along,region,probe,radius);
    if(!lo.found)return false;
    const HudRail hi=findHudRail(pixel,normal,along,region,probe,radius);
    if(!hi.found||colorError(lo.edge,hi.edge)>0.05||colorError(lo.inset,hi.inset)>0.045)return false;
    const float3 inset=(lo.inset+hi.inset)*0.5;
    return min(colorError(a,inset),colorError(b,inset))<=0.038;
}

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const int2 p=int2(id.xy+roiOrigin);
    const int region=hudRegion(p);
    float confidence=0;
    if(!resetHistory&&region>=0) {
        const float4 motion=motionTex.Load(int3(region,0,0));
        const int2 shift=int2(motion.xy);
        const bool haveMotion=motion.w==0&&motion.z>0&&frameContains(p+shift);
        if(motion.w==0) {
            confidence=previousConfidence.Load(int3(id.xy,0));
            // R8 value 255 is specific to A/B and cannot seed stillness.
            if(confidence>0.998)confidence=0;
            if(!haveMotion)confidence*=0.998;
        }
        if(motion.w==0||region>0) {
            const float3 a=previousAt(p),b=currentAt(p);
            const float still=colorError(b,a);
            if(motion.w==0) {
                const float moving=haveMotion?colorError(b,previousAt(p+shift)):0;
                if(haveMotion&&moving<0.039&&still>0.048)confidence-=0.72;
                else if(haveMotion&&still<0.038&&moving>0.075&&thinStroke(p,shift,b)) {
                    const bool symmetric=frameContains(p-shift)&&colorError(a,currentAt(p-shift))>0.075;
                    confidence+=symmetric?0.66:0.44;
                } else if(!haveMotion&&still>0.065)confidence=0;
                const bool appearanceCandidate=haveMotion?moving>0.095:motion.z<0||still>0.05;
                if(confidence<0.61&&appearanceCandidate&&trackedAppearance(p,b,region))
                    confidence=0.78;
            }
            confidence=clamp(confidence,0.0,0.992);
            // On a verified still scene an established stroke can disappear
            // between A and B. Keep that change with the chosen real frame.
            if(motion.z<0&&still>0.095&&confidence<0.61&&trackedAppearance(p,a,region,true))
                confidence=1;
            // Two stable rails provide their own screen-space evidence, even
            // if the scene behind the panel has jumped or cannot be matched.
            if(region>0&&still>0.075&&
               (changesOnHudInset(p,int2(0,1),int2(1,0),region,a,b)||
                changesOnHudInset(p,int2(1,0),int2(0,1),region,a,b)))confidence=1;
        }
    }
    confidenceTex[id.xy]=confidence;
    if(confidence>=0.61 && region>=0)InterlockedAdd(counts[uint2(region,0)],1);
}
