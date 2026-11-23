// Confidence grows on thin, screen-stationary marks that disagree with the moving background.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float> previousConfidence : register(t2);
Texture2D<float4> motionTex : register(t3);
Texture2D<float4> previousAppearance : register(t4);
Texture2D<uint> previousCounts : register(t5);
RWTexture2D<float> confidenceTex : register(u0);
RWTexture2D<uint> counts : register(u1);
RWTexture2D<float4> appearanceTex : register(u2);

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
bool localMotionEvidence(int2 p,float3 center,int step) {
    const int2 directions[4]={int2(step,0),int2(0,step),int2(step,step),int2(step,-step)};
    [unroll] for(uint i=0;i<4;++i) {
        const int2 lo=p-directions[i],hi=p+directions[i];
        if(!frameContains(lo)||!frameContains(hi))continue;
        const float3 a=currentAt(lo),b=currentAt(hi);
        if(colorError(a,center)>0.055&&colorError(b,center)>0.055) {
            const float changeA=colorError(a,previousAt(lo)),changeB=colorError(b,previousAt(hi));
            if(min(changeA,changeB)>0.040&&max(changeA,changeB)>0.074)return true;
        }
    }
    return false;
}
bool sharedRecolor(int2 p,int region,float3 color,float3 oldColor) {
    const int2 directions[4]={int2(1,0),int2(-1,0),int2(0,1),int2(0,-1)};
    int agrees=0;
    [unroll] for(uint i=0;i<4;++i) {
        const int2 q=p+directions[i],local=q-int2(roiOrigin);
        if(!roiContains(local)||hudRegion(q)!=region)continue;
        const float4 previous=previousAppearance.Load(int3(local,0));
        const float c=previousConfidence.Load(int3(local,0));
        if(c>=0.61&&c<0.998&&previous.a<0.010&&colorError(previous.rgb,oldColor)<0.055&&
           colorError(currentAt(q),color)<0.055)++agrees;
    }
    return agrees>=2;
}
float3 medianColor(float3 a,float3 b,float3 c) {
    return max(min(a,b),min(max(a,b),c));
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
    [loop] for(int distance=1;distance<=step;distance+=max(1,step/4)) {
        [unroll] for(int i=0;i<8;++i) {
            const int2 q=p+distance*directions[i],local=q-int2(roiOrigin);
            if(!roiContains(local))continue;
            const float known=previousConfidence.Load(int3(local,0));
            if(known>=0.61&&known<0.998&&previousAppearance.Load(int3(local,0)).a<0.010&&
               colorError(color,previousAt(q))<0.08&&hudRegion(q)==region)return true;
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
    const float4 centerBox=regionBox(0);
    const float2 centerDistance=abs(float2(p)+0.5-(centerBox.xy+centerBox.zw)*0.5);
    const float centerReach=max(27.0,float(frameSize.y)/36.0);
    const bool axial=min(centerDistance.x,centerDistance.y)<=max(2.0,float(frameSize.y)/300.0);
    const float dotReach=hudDotProbeRadius()+1;
    const bool insideDot=dot(centerDistance,centerDistance)<=dotReach*dotReach;
    float confidence=0;
    float4 appearance=0;
    bool active=false, remembered=false, pending=false;
    if(!resetHistory&&region>=0&&(region>0||max(centerDistance.x,centerDistance.y)<=centerReach)) {
        const float4 motion=motionTex.Load(int3(region,0,0));
        const int2 shift=int2(motion.xy);
        const bool haveMotion=motion.z>0&&frameContains(p+shift);
        const float3 a=previousAt(p),b=currentAt(p);
        const float still=colorError(b,a);
        const int step=clamp(int(round(float(frameSize.y)/180)),3,14);
        const float prior=previousConfidence.Load(int3(id.xy,0));
        const bool confirmed=prior>=0.61&&prior<0.998;
        const uint shape=counts[hudCounter(region,HudCentredMark)];
        const bool dotMark=region==0&&(shape&HudDotMark)!=0;
        const bool visible=region>0||shape!=0;
        const bool learn=region>0||(shape&HudFreshMark)!=0;
        const float4 known=previousAppearance.Load(int3(id.xy,0));
        const bool history=previousCounts.Load(int3(hudCounter(region,HudRemembered),0))>=2;
        const float moving=haveMotion?colorError(b,previousAt(p+shift)):0;
        confidence=prior<0.998?prior:0;

        if(confirmed) {
            const bool sameColor=colorError(known.rgb,b)<=0.064;
            const bool recolor=!sameColor&&sharedRecolor(p,region,b,known.rgb);
            const bool matchesStroke=thinContrast(p,b,step,false)||
                (region==0&&axial&&sameColor&&still<=0.073);
            if(visible&&(!dotMark||insideDot)&&(sameColor||recolor)&&matchesStroke) {
                // Median of the previous template and two pictures filters
                // an isolated flicker without delaying coherent recolours.
                appearance=float4(recolor?b:medianColor(known.rgb,a,b),0);
                confidence=min(0.992,prior+0.06);
                active=remembered=true;
            } else {
                const uint missing=(uint(round(known.a*255.0))&127u)+1;
                remembered=missing<8;
                confidence=remembered?prior:0;
                appearance=float4(known.rgb,min(missing,8u)/255.0);
            }
        } else {
            appearance=float4(b,0);
            const bool thin=learn&&(region>0||(dotMark?insideDot:axial))&&still<0.045&&thinContrast(p,b,step,false);
            if(!visible)confidence=0;
            if(thin&&haveMotion&&moving>0.075&&thinStroke(p,shift,b)) {
                const bool symmetric=frameContains(p-shift)&&colorError(a,currentAt(p-shift))>0.075;
                confidence+=symmetric?0.52:0.36;
            } else if(thin&&(localMotionEvidence(p,b,step)||tightRegion(region))) {
                confidence+=0.25;
            } else if(still>0.038) {
                confidence=0;
            }
            // An already tracked stroke can confirm nearby tips, provided
            // the tip itself stays fixed. A one-frame change cannot expand
            // the durable mask.
            if(thin&&confidence>=0.18&&confidence<0.61&&history&&motion.w==0&&
               trackedAppearance(p,b,region))confidence=max(confidence,0.44);
            confidence=min(confidence,0.992);
            active=remembered=confidence>=0.61;
            pending=!active&&confidence>=0.19&&still<0.052;
        }

        // Pair-specific changes have no authority to seed the next mask.
        const bool vanished=confirmed&&!active&&motion.z<0&&still>0.095;
        const bool railChange=region>0&&still>0.075&&
           (changesOnHudInset(p,int2(0,1),int2(1,0),region,a,b)||
            changesOnHudInset(p,int2(1,0),int2(0,1),region,a,b));
        // Moving arms (weapon spread) carry the nearby confirmed stroke in
        // the same pair. Require the changing pixel to match the stroke in
        // the picture that contains it, at its fixed screen position.
        const bool appeared=visible&&!confirmed&&history&&still>0.095&&
            trackedAppearance(p,b,region);
        const bool departed=visible&&confirmed&&!active&&still>0.095&&
            trackedAppearance(p,a,region,true);
        const bool dynamic=vanished||railChange||appeared||departed;
        if(active||dynamic)InterlockedAdd(counts[hudCounter(region,HudDensity)],1);
        if(active&&!dynamic)InterlockedAdd(counts[hudCounter(region,HudCore)],1);
        if(remembered)InterlockedAdd(counts[hudCounter(region,HudRemembered)],1);
        if(pending)InterlockedAdd(counts[hudCounter(region,HudEvidence)],1);
        // A dynamic rail can change colour and still carry its template.
        // Signal this pair in the high bit of the appearance age so it
        // does not erase learned confidence on the following frame.
        if(dynamic)appearance.a+=128.0/255.0;
    }
    confidenceTex[id.xy]=confidence;
    appearanceTex[id.xy]=appearance;
}
