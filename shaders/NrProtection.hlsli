#ifndef VEYRA_NR_PROTECTION
#define VEYRA_NR_PROTECTION
// regions: normalized (left, top, right, bottom). A region sent with left and
// right swapped (engine::protectionConstants) is the ellipse inscribed in the
// box; an ordinary rectangle is unchanged.
float NrProtection(float2 pixel, uint2 size, float enabled, float feather, float4 regions[4]) {
    float protection=0;
    if(enabled>0) {
        for(uint i=0;i<4;++i) {
            float4 r=regions[i]*float4(size,size);
            const bool ellipse=r.z<r.x;
            if(ellipse)r=float4(r.z,r.y,r.x,r.w);
            if(r.z<=r.x||r.w<=r.y)continue;
            float edge;
            if(ellipse) {
                // Distance inside the ellipse, approximated along the normalized
                // radius and scaled by the shorter semi-axis (exact on circles).
                const float2 centre=(r.xy+r.zw)*0.5;
                const float2 axis=max((r.zw-r.xy)*0.5,1e-3);
                edge=(1.0-length((pixel-centre)/axis))*min(axis.x,axis.y);
            } else {
                edge=min(min(pixel.x-r.x,r.z-pixel.x),min(pixel.y-r.y,r.w-pixel.y));
            }
            protection=max(protection,feather>0?smoothstep(0,feather,edge):(edge>=0?1:0));
        }
    }
    return protection;
}
#endif
