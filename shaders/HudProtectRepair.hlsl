// Estimate the background on HUD pixels. Keep the opaque body and
// the cleaned translucent fringe separate for the final composite.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float> confidenceTex : register(t2);
Texture2D<float4> previousMask : register(t3);
Texture2D<float4> motionTex : register(t4);
Texture2D<uint> counts : register(t5);
Texture2D<float4> previousClean : register(t6);
RWTexture2D<float4> cleanedTex : register(u0);
RWTexture2D<float4> maskTex : register(u1); // opacity, cleaned, dynamic pair, unused

bool coreAt(int2 p) {
    if(!roiContains(p))return false;
    const float confidence=confidenceTex.Load(int3(p,0));
    return confidence>=0.61&&confidence<0.998;
}
bool nearPair(int2 p,int region) {
    [loop] for(int y=-2;y<=2;++y) {
        [loop] for(int x=-2;x<=2;++x) {
            const int2 q=p+int2(x,y);
            if(roiContains(q)&&confidenceTex.Load(int3(q,0))>0.998&&
               hudRegion(q+int2(roiOrigin))==region)return true;
        }
    }
    return false;
}
bool nearestCore(int2 p,int radius,out int2 anchor) {
    anchor=p;
    int best=2*radius*radius+1;
    [loop] for(int y=-radius;y<=radius;++y) {
        [loop] for(int x=-radius;x<=radius;++x) {
            const int distance=x*x+y*y;
            if(distance>=best||!coreAt(p+int2(x,y)))continue;
            best=distance;
            anchor=p+int2(x,y);
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
float fringeOpacity(float3 delta,float3 reference) {
    const float power=dot(reference,reference);
    if(power<0.004)return 0;
    const float alpha=dot(delta,reference)/power;
    if(alpha<=0.02||colorError(delta,0)<0.014||
       dot(delta-alpha*reference,delta-alpha*reference)>max(0.00032,0.45*dot(delta,delta)))return 0;
    return alpha>=0.73?1:alpha;
}

[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const int2 local=int2(id.xy),pixel=local+int2(roiOrigin);
    const int region=hudRegion(pixel);
    maskTex[id.xy]=0;
    if(resetHistory||region<0)return;

    const float4 box=regionBox(region);
    const float2 extent=max(0,min(box.zw,float2(frameSize))-max(box.xy,0));
    const float fraction=region==0?0.12:0.55;
    const uint count=counts.Load(int3(region,0,0));
    if(count==0||float(count)>extent.x*extent.y*fraction)return;
    const float confidence=confidenceTex.Load(int3(id.xy,0));
    bool dynamic=confidence>0.998;
    if(!dynamic&&region>0&&colorError(comparisonColor(currentTex.Load(int3(pixel,0)).rgb),
        comparisonColor(previousTex.Load(int3(pixel,0)).rgb))>0.003)dynamic=nearPair(local,region);
    if(dynamic) {
        // Rail-delimited changes belong to this A/B pair, even on a cut.
        maskTex[id.xy]=float4(0,0,1,0);
        return;
    }
    const bool core=confidence>=0.61;
    const float4 motion=motionTex.Load(int3(region,0,0));
    if(motion.z<=0) {
        if(core)maskTex[id.xy]=float4(1,0,0,0);
        return;
    }

    const int feather=clamp(int(round(float(frameSize.y)/360)),3,5);
    int2 anchor;
    if(!core&&!nearestCore(local,feather,anchor))return;
    const int2 shift=int2(motion.xy);
    float4 background;
    if(!estimateBackground(pixel,shift,feather,background)) {
        if(core)maskTex[id.xy]=float4(1,0,0,0);
        return;
    }
    const float4 real=currentTex.Load(int3(pixel,0));
    float alpha=1;
    if(!core) {
        const int2 center=anchor+int2(roiOrigin);
        if(hudRegion(center)!=region)return;
        float4 anchorBackground;
        if(!temporalBackground(center,shift,anchorBackground))return;
        const float3 ref=comparisonColor(currentTex.Load(int3(center,0)).rgb)-comparisonColor(anchorBackground.rgb);
        alpha=fringeOpacity(comparisonColor(real.rgb)-comparisonColor(background.rgb),ref);
        if(alpha==0)return;
    }
    cleanedTex[pixel]=float4(background.rgb,real.a);
    maskTex[id.xy]=float4(alpha,1,0,0);
}
