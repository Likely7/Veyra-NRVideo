#ifndef VEYRA_NR_PROTECTION
#define VEYRA_NR_PROTECTION
// A region with left and right swapped encodes the ellipse inscribed in the box.
float ProtectionRegionWeight(float2 pixel,uint2 size,float feather,float4 region) {
    float4 r=region*float4(size,size);
    const bool ellipse=r.z<r.x;
    if(ellipse)r=float4(r.z,r.y,r.x,r.w);
    if(r.z<=r.x||r.w<=r.y)return 0;
    float edge;
    if(ellipse) {
        const float2 centre=(r.xy+r.zw)*0.5;
        const float2 axis=max((r.zw-r.xy)*0.5,1e-3);
        edge=(1.0-length((pixel-centre)/axis))*min(axis.x,axis.y);
    } else {
        edge=min(min(pixel.x-r.x,r.z-pixel.x),min(pixel.y-r.y,r.w-pixel.y));
    }
    return feather>0?smoothstep(0,feather,edge):(edge>=0?1:0);
}
float NrProtection(float2 pixel,uint2 size,float enabled,float feather,float4 regions[4]) {
    float protection=0;
    if(enabled>0) {
        for(uint i=0;i<4;++i)
            protection=max(protection,ProtectionRegionWeight(pixel,size,feather,regions[i]));
    }
    return protection;
}
#endif
