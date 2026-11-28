// Three-step SR (2026-10-28): fits the RTX Video SR result into the smaller
// work extent. Each SR texel is first decoded (SDR, as ScaleBlit's decode) or
// HDR-restored (as HdrVideoSrRestore, base and proxy sampled at the texel's
// centre), then the linear values are filtered with Lanczos-2 AA without a
// full-size linear intermediate.
#include "HdrColor.hlsli"
Texture2D<float4> hdrBase:register(t0);
Texture2D<float4> proxyInput:register(t1);
Texture2D<float4> linearTex:register(t2);
RWTexture2D<float4> outputTex:register(u0);
cbuffer Dims : register(b0) {
    uint sourceW, sourceH, outW, outH;
    float hdr;
    uint tileW, tileH, srgbView;
}
float3 Decode(float3 c){return select(c<=.04045,c/12.92,pow((c+.055)/1.055,2.4));}
float4 ReadLinear(uint2 p){
    const float4 c=linearTex[p];
    if(hdr<.5)return srgbView ? c : float4(Decode(max(c.rgb,0.0)),c.a);
    uint bw,bh;hdrBase.GetDimensions(bw,bh);
    float2 pos=(float2(p)+.5)*float2(bw,bh)/float2(sourceW,sourceH)-.5;
    int2 q=int2(floor(pos)),last=int2(bw,bh)-1;float2 f=frac(pos);
    float3 base=0,proxy=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int2 s=clamp(q+int2(x,y),int2(0,0),last);
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        base+=hdrBase[s].rgb*weight;
        proxy+=proxyInput[s].rgb*weight;
    }
    return float4(HdrRestore(base,Decode(proxy),Decode(c.rgb)),1);
}
#include "Lanczos2Reduce.hlsli"
