// Three-step SR (2026-10-28): fits the RTX Video SR result into the smaller
// work extent. Each SR texel is first decoded (SDR, as ScaleBlit's decode) or
// HDR-restored (as HdrVideoSrRestore, base and proxy sampled at the texel's
// centre), then the linear values are averaged with NrDownsample's exact area
// weights. No full-size linear intermediate is written.
#include "HdrColor.hlsli"
Texture2D<float4> hdrBase:register(t0);
Texture2D<float4> proxyInput:register(t1);
Texture2D<float4> proxyOutput:register(t2);
RWTexture2D<float4> outputTex:register(u0);
cbuffer Dims:register(b0){uint sourceW;uint sourceH;uint outW;uint outH;float hdr;float3 unused;}
float3 Decode(float3 c){return select(c<=.04045,c/12.92,pow((c+.055)/1.055,2.4));}
float4 Texel(uint2 p){
    const float4 c=proxyOutput[p];
    if(hdr<.5)return float4(Decode(max(c.rgb,0.0)),c.a);
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
[numthreads(16,16,1)]void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=outW||id.y>=outH)return;
    float2 low=float2(id.xy)*float2(sourceW,sourceH)/float2(outW,outH);
    float2 high=float2(id.xy+1)*float2(sourceW,sourceH)/float2(outW,outH);
    float4 sum=0;float weights=0;
    for(uint y=(uint)floor(low.y);y<(uint)ceil(high.y);++y)for(uint x=(uint)floor(low.x);x<(uint)ceil(high.x);++x){
        float weight=max(0,min(high.x,x+1.0)-max(low.x,(float)x))*max(0,min(high.y,y+1.0)-max(low.y,(float)y));
        sum+=Texel(uint2(min(x,sourceW-1),min(y,sourceH-1)))*weight;weights+=weight;
    }
    outputTex[id.xy]=sum/max(weights,1e-6);
}
