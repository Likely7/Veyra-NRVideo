#ifndef VEYRA_HUD_PROTECT
#define VEYRA_HUD_PROTECT

cbuffer HudProtectConstants : register(b0) {
    uint2 frameSize, roiOrigin;
    uint2 roiSize;
    uint resetHistory, outputMode; // 0 sRGB, 1 scRGB, 2 PQ
    float4 hudRegions[5];          // left > right encodes an ellipse
    uint readDenseFlow;
};
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
