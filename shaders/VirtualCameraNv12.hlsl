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
// The camera rounds the surface extent up to even dimensions. One thread
// owns all four luma values and the chroma value of that NV12 block.
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
    const uint2 p=id.xy*2;
    if(p.x>=width||p.y>=height)return;
    const uint2 right=uint2(1,0),down=uint2(0,1);
    const float3 a=inputColor(p),b=inputColor(p+right);
    const float3 c=inputColor(p+down),d=inputColor(p+right+down);
    yPlane[p]=convert(a).x;
    yPlane[p+right]=convert(b).x;
    yPlane[p+down]=convert(c).x;
    yPlane[p+right+down]=convert(d).x;
    uvPlane[id.xy]=convert((a+b+c+d)*.25).yz;
}
