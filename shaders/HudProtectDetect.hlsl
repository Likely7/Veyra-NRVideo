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
        if(haveMotion||region>0) {
            const float3 a=previousAt(p),b=currentAt(p);
            const float still=colorError(b,a);
            if(haveMotion) {
                const float moving=colorError(b,previousAt(p+shift));
                if(moving<0.039&&still>0.048)confidence-=0.72;
                else if(still<0.038&&moving>0.075&&thinStroke(p,shift,b))confidence+=0.44;
            }
            confidence=clamp(confidence,0.0,0.992);
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
