#ifndef VEYRA_HUD_PROTECT
#define VEYRA_HUD_PROTECT

cbuffer HudProtectConstants : register(b0) {
    uint2 frameSize, roiOrigin;
    uint2 roiSize;
    uint resetHistory, outputMode; // 0 sRGB, 1 scRGB, 2 PQ
    float4 hudRegions[5];          // left > right encodes an ellipse
    uint readDenseFlow;
    uint reserved;
};
static const uint HudDensity = 0;
static const uint HudCore = 1;
static const uint HudRepaired = 2;
static const uint HudRemembered = 3;
static const uint HudEvidence = 4;
static const uint HudTrusted = 5;
static const uint HudCentredMark = 6;
static const uint HudCounterCount = 7;
static const uint HudMarkLifetime = 8u;
static const uint HudMarkAgeMask = 15u;
static const uint HudFreshMark = 16u;
static const uint HudDotMark = 32u;
uint2 hudCounter(uint region,uint field) {
    return uint2(region*HudCounterCount+field,0);
}

int hudDotProbeRadius() {
    // Sample beyond the antialiased rim, including six-to-eight-pixel dots
    // at 1440p. The thin-arm detector uses a smaller radius.
    return clamp(int(round(float(frameSize.y)/360.0)),3,12);
}
int2 boundPixel(int2 pixel) {
    return clamp(pixel, int2(0,0), int2(frameSize)-1);
}
bool frameContains(int2 pixel) {
    return all(pixel>=0) && all(pixel<int2(frameSize));
}
bool roiContains(int2 pixel) {
    return all(pixel>=0) && all(pixel<int2(roiSize));
}
float4 regionBox(uint index) {
    float4 r=hudRegions[index]*float4(frameSize,frameSize);
    return float4(min(r.x,r.z),r.y,max(r.x,r.z),r.w);
}
float allowedDensity(uint region) {
    const float4 b=regionBox(region);
    const float2 extent=max(0,min(b.zw,float2(frameSize))-max(b.xy,0));
    return extent.x*extent.y*(region==0?0.12:0.55);
}
bool tightRegion(uint region) {
    const float4 b=regionBox(region);
    return region>0 && max(b.z-b.x,b.w-b.y)<=max(28.0,120.0*float(frameSize.y)/2160.0);
}
int hudRegion(int2 pixel) {
    const float2 p=float2(pixel)+0.5;
    // A tight manual region takes precedence over the automatic centre.
    for(int i=4;i>=0;--i) {
        float4 b=regionBox(i);
        const float2 axis=(b.zw-b.xy)*0.5;
        if(any(axis<=0)||any(p<b.xy)||any(p>b.zw))continue;
        if(hudRegions[i].x>hudRegions[i].z) {
            const float2 q=(p-(b.xy+b.zw)*0.5)/axis;
            if(dot(q,q)>1)continue;
        }
        return i;
    }
    return -1;
}
float3 comparisonColor(float3 rgb) {
    if(outputMode==1) {
        rgb=max(rgb,0);
        return rgb/(1+rgb);
    }
    return rgb;
}
float colorError(float3 a,float3 b) {
    const float3 d=abs(a-b);
    return max(max(d.x,d.y),d.z);
}
#endif
