// Copy clean pixels from the motion-aligned prior view or a local masked fill.
#include "HudProtect.hlsli"

Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
Texture2D<float> confidenceTex : register(t2);
Texture2D<float> previousMask : register(t3);
Texture2D<float4> motionTex : register(t4);
Texture2D<uint> counts : register(t5);
RWTexture2D<float4> cleanedTex : register(u0);
RWTexture2D<float> maskTex : register(u1);

bool marked(int2 p) {
    return roiContains(p)&&confidenceTex.Load(int3(p,0))>=0.61;
}
bool paddedMark(int2 p) {
    if(!roiContains(p))return false;
    const int radius=2;
    [loop] for(int y=-radius;y<=radius;++y) {
        [loop] for(int x=-radius;x<=radius;++x) {
            if(marked(p+int2(x,y)))return true;
        }
    }
    return false;
}
float4 spatialFill(int2 pixel) {
    const int2 dirs[8]={int2(1,0),int2(-1,0),int2(0,1),int2(0,-1),
                       int2(1,1),int2(-1,1),int2(1,-1),int2(-1,-1)};
    // Limit the search to the few pixels needed to bridge a thin stroke.
    [loop] for(int radius=2;radius<=16;radius*=2) {
        float4 sum=0;
        uint count=0;
        [unroll] for(uint i=0;i<8;++i) {
            const int2 p=pixel+radius*dirs[i];
            if(!frameContains(p)||paddedMark(p-int2(roiOrigin)))continue;
            sum+=currentTex.Load(int3(p,0));
            ++count;
        }
        if(count>0)return sum/count;
    }
    return currentTex.Load(int3(pixel,0));
}
[numthreads(16,16,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=roiSize))return;
    const int2 pixel=int2(id.xy+roiOrigin);
    const int region=hudRegion(pixel);
    maskTex[id.xy]=0;
    if(resetHistory || region<0)return;

    const float4 box=regionBox(region);
    const float2 extent=max(0,min(box.zw,float2(frameSize))-max(box.xy,0));
    // Reject a region with widespread stationary content. Keep tight
    // manually drawn boxes useful for small crosses and compact HUD icons.
    const float fraction=region==0?0.12:0.55;
    const uint count=counts.Load(int3(region,0,0));
    if(count==0||float(count)>extent.x*extent.y*fraction)return;
    if(!paddedMark(int2(id.xy)))return;

    const float4 motion=motionTex.Load(int3(region,0,0));
    const int2 q=pixel+int2(motion.xy);
    const int2 local=q-int2(roiOrigin);
    const bool occluded=roiContains(local)&&(previousMask.Load(int3(local,0))>0||paddedMark(local));
    cleanedTex[pixel]=(motion.z>0&&frameContains(q)&&!occluded)
        ?previousTex.Load(int3(q,0)):spatialFill(pixel);
    maskTex[id.xy]=1;
}
