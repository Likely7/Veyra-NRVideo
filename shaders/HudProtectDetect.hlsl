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
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const int2 p=int2(id.xy+roiOrigin);
    const int region=hudRegion(p);
    float confidence=0;
    if(!resetHistory&&region>=0) {
        const float4 motion=motionTex.Load(int3(region,0,0));
        confidence=previousConfidence.Load(int3(id.xy,0));
        if(motion.w>0)confidence=0;
        else if(motion.z>0 && frameContains(p+int2(motion.xy))) {
            const int2 shift=int2(motion.xy);
            const float3 b=currentAt(p);
            const float moving=colorError(b,previousAt(p+shift));
            const float still=colorError(b,previousAt(p));
            if(moving<0.039 && still>0.048)confidence-=0.72;
            else if(still<0.038 && moving>0.075 && thinStroke(p,shift,b))confidence+=0.44;
        } else confidence*=0.998;
    }
    confidence=saturate(confidence);
    confidenceTex[id.xy]=confidence;
    if(confidence>=0.61 && region>=0)InterlockedAdd(counts[uint2(region,0)],1);
}
