#include "HdrColor.hlsli"
Texture2D<float4> hdrBase:register(t0);
Texture2D<float4> proxyInput:register(t1);
Texture2D<float4> proxyOutput:register(t2);
RWTexture2D<float4> outputTex:register(u0);
// Root constants as the callers bind them: [4] a float, [5]/[6] the extent of
// hdrBase/proxyInput the SR read (smart SR: its render extent, the top-left of
// source-sized textures); 0 uses the whole texture.
// Smart SR always enlarges, so its base is enlarged in PQ codes, as the console
// enlarges its own HDR10 picture, and the proxy is the one of that base. Enlarged in
// linear light, bright pixels spread over the edge: dark strokes on a 211-nit HOME
// background kept 70-79 % of their area, bright ones on black grew to 109 %. Where the
// restore keeps the base (highlights) the result is the console's own picture.
cbuffer RestoreConstants:register(b0){uint4 extents;float mode;uint baseWidth,baseHeight,pad;};
float3 Decode(float3 c){return select(c<=.04045,c/12.92,pow((c+.055)/1.055,2.4));}
[numthreads(16,16,1)]void main(uint3 id:SV_DispatchThreadID){
    uint w,h,sw,sh;outputTex.GetDimensions(w,h);hdrBase.GetDimensions(sw,sh);
    if(baseWidth&&baseHeight){sw=min(sw,baseWidth);sh=min(sh,baseHeight);}
    if(id.x>=w||id.y>=h)return;
    float2 pos=(float2(id.xy)+.5)*float2(sw,sh)/float2(w,h)-.5;
    int2 q=int2(floor(pos)),last=int2(sw,sh)-1;float2 f=frac(pos);
    const bool coded=baseWidth&&baseHeight;
    float3 base=0,proxy=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int2 p=clamp(q+int2(x,y),int2(0,0),last);
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        base+=(coded?HdrEncodePq(hdrBase[p].rgb):hdrBase[p].rgb)*weight;
        proxy+=proxyInput[p].rgb*weight;
    }
    if(coded)base=HdrDecodePq(base);
    outputTex[id.xy]=float4(HdrRestore(base,coded?HdrProxy(base):Decode(proxy),Decode(proxyOutput[id.xy].rgb)),1);
}
