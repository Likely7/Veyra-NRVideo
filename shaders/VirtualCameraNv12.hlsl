// Clean full-resolution compatibility output, independent of the window.
// SDR follows RgbToNv12. HDR uses the existing fixed 203-nit proxy shoulder
// and one sRGB encoding at this SDR output boundary, rather than clipping PQ.
#include "HdrColor.hlsli"
Texture2D<float4> rgb : register(t0);
RWTexture2D<float> yPlane : register(u0);
RWTexture2D<float2> uvPlane : register(u1);
cbuffer Constants : register(b0) {
    uint width; uint height; uint inputWidth; uint inputHeight;
    uint hdrMode; uint fine; uint contentWidth; uint contentHeight;
};
float3 srgb(float3 c){return select(c<=.0031308,12.92*c,1.055*pow(max(c,0),1.0/2.4)-.055);}
// Same bounded cubic weights as PresentBlit; linear is the normal display mode.
float4 weights(float t){float t2=t*t,t3=t2*t;return float4(-.5*t+t2-.5*t3,1-2.5*t2+1.5*t3,.5*t+2*t2-1.5*t3,-.5*t2+.5*t3);}
float3 sampleFrame(uint2 outPixel){
    outPixel=min(outPixel,uint2(contentWidth-1,contentHeight-1));
    const int2 last=int2(inputWidth,inputHeight)-1;
    if(inputWidth==contentWidth&&inputHeight==contentHeight)return rgb[min(outPixel,uint2(last))].rgb;
    float2 p=(float2(outPixel)+.5)*float2(inputWidth,inputHeight)/float2(contentWidth,contentHeight)-.5;
    int2 base=int2(floor(p));float2 t=frac(p);
    float3 a=rgb[clamp(base,int2(0,0),last)].rgb,b=rgb[clamp(base+int2(1,0),int2(0,0),last)].rgb;
    float3 c=rgb[clamp(base+int2(0,1),int2(0,0),last)].rgb,d=rgb[clamp(base+int2(1,1),int2(0,0),last)].rgb;
    if(!fine)return lerp(lerp(a,b,t.x),lerp(c,d,t.x),t.y);
    float4 wx=weights(t.x),wy=weights(t.y);float3 value=0;
    [unroll]for(int y=0;y<4;++y)[unroll]for(int x=0;x<4;++x)value+=rgb[clamp(base+int2(x-1,y-1),int2(0,0),last)].rgb*wx[x]*wy[y];
    return clamp(value,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
}
float3 inputColor(uint2 p){
    float3 c=sampleFrame(p);
    if(hdrMode){
        if(hdrMode==2)c=HdrDecodePq(c);
        c=srgb(HdrProxy(c));
    }
    return saturate(c);
}
float3 convert(float3 c){
    float y=dot(c,float3(.2126,.7152,.0722));
    return float3((16+219*y)/255.0,(128+224*(c.b-y)/1.8556)/255.0,(128+224*(c.r-y)/1.5748)/255.0);
}
// Cooperative 16x16 workgroup tile eliminates 4x redundant 16-tap cubic filters in 2x2 UV threads.
groupshared float3 tile[16][16];
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID, uint3 gtid:SV_GroupThreadID){
    // Guard odd widths/heights so the odd+1 neighbor slot in a 2x2 UV pair still holds inputColor(id.xy).
    const bool load = (id.x <= width && id.y <= height);
    const float3 col = load ? inputColor(id.xy) : float3(0,0,0);
    tile[gtid.y][gtid.x] = col;
    GroupMemoryBarrierWithGroupSync();
    if(id.x>=width||id.y>=height)return;
    yPlane[id.xy]=convert(col).x;
    if((gtid.x&1u)==0u&&(gtid.y&1u)==0u){
        float3 c=(col+tile[gtid.y][gtid.x+1]+tile[gtid.y+1][gtid.x]+tile[gtid.y+1][gtid.x+1])*.25;
        uvPlane[id.xy>>1]=convert(c).yz;
    }
}
