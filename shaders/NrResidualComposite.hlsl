#include "NrProtection.hlsli"
#include "NrCorrection.hlsli"
Texture2D<float4> baseTex:register(t0);
Texture2D<float4> nrInput:register(t1);
Texture2D<float4> nrFinal:register(t2);
RWTexture2D<float4> outputTex:register(u0);
cbuffer Params:register(b0){
    float total;float darken;float brighten;float color;float luminance;float protectionEnabled;float featherPixels;float unused;float4 regions[4];
    float correctionEnabled;float hueProtection;float chromaProtection;float highlightProtection;
    float localCompression;float temporalStability;float correctionAutomatic;float correctionPad;
    float neutralProtection;float colorRetention;float luminanceRetention;float shadowProtection;
    float stackLimit;float stackDetail;float stackStrength;float residualStack;
}
static const float3 kResidualLuma=float3(0.2126,0.7152,0.0722);

float stackDetailWeight(int2 p,int2 last,float center){
    float4 neighbors=float4(
        dot(nrInput[int2(max(p.x-1,0),p.y)].rgb,kResidualLuma),
        dot(nrInput[int2(min(p.x+1,last.x),p.y)].rgb,kResidualLuma),
        dot(nrInput[int2(p.x,max(p.y-1,0))].rgb,kResidualLuma),
        dot(nrInput[int2(p.x,min(p.y+1,last.y))].rgb,kResidualLuma));
    float4 contrasts=abs(neighbors-center)/max(max(abs(neighbors),abs(center)),0.0625);
    return smoothstep(0.02,0.20,max(max(contrasts.x,contrasts.y),max(contrasts.z,contrasts.w)));
}
// Extrapolating a negative linear-light residual can cross zero even when
// both input images contain shadow detail. Continue below the unboosted
// endpoint with a positive, tangent-matched curve instead of hard clipping.
// Identity and 0..1 mixing remain exact. Signed HDR gamut components remain
// signed; this is a residual-strength safeguard, not an input/output gamma.
float shadowSafe(float base,float rawDelta,float requestedDelta){
    float anchorDelta=min(rawDelta,0.0);
    float anchor=base+anchorDelta;
    if(base>=0&&anchor>0&&requestedDelta<anchorDelta)
        return anchor/(1.0+(anchorDelta-requestedDelta)/anchor);
    return base+requestedDelta;
}
// Bilateral resampling of the change uses the preserved high-resolution base
// as the edge guide. It never interpolates the high-resolution base itself.
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID){
    uint w,h,nw,nh;baseTex.GetDimensions(w,h);nrInput.GetDimensions(nw,nh);
    if(id.x>=w||id.y>=h)return;
    float4 base=baseTex[id.xy];
    if(residualStack>0){
        if(stackStrength==0){outputTex[id.xy]=base;return;}
        float3 original=nrInput[id.xy].rgb;
        float3 delta=base.rgb-original;
        if(darken!=1||brighten!=1||color!=1){
            float dy=dot(delta,kResidualLuma);
            delta=(delta-dy)*color+dy*(dy<0?darken:brighten);
        }
        delta*=stackStrength;
        // 以本段入口画面的局部亮度对比度选择细节，默认不读取邻域。
        [branch] if(stackDetail>0){
            delta*=lerp(1,stackDetailWeight(int2(id.xy),int2(nw,nh)-1,dot(original,kResidualLuma)),stackDetail);
        }
        if(stackLimit>0){
            // 只软压这次新增的差分，三个通道共用倍率以保留差分方向。
            float3 magnitude=abs(delta);
            float peak=max(magnitude.r,max(magnitude.g,magnitude.b));
            float3 reference=abs(base.rgb);
            float knee=0.125*max(1,max(reference.r,max(reference.g,reference.b)))/stackLimit;
            if(peak>knee){
                float excess=peak-knee;
                delta*=(knee+excess/(1+excess/knee))/peak;
            }
        }
        outputTex[id.xy]=float4(base.rgb+delta,base.a);
        return;
    }
    float protection=NrProtection(float2(id.xy)+0.5,uint2(w,h),protectionEnabled,featherPixels,regions);
    if(total==0||protection>=1){outputTex[id.xy]=base;return;}
    float2 pos=(float2(id.xy)+0.5)*float2(nw,nh)/float2(w,h)-0.5;
    int2 origin=(int2)floor(pos);float2 f=frac(pos);float3 delta=0;float weights=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int2 q=clamp(origin+int2(x,y),int2(0,0),int2(nw-1,nh-1));
        float3 low=nrInput[q].rgb;
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        if(w!=nw||h!=nh)weight/=1+16*dot(abs(base.rgb-low),float3(0.2126,0.7152,0.0722));
        delta+=(nrFinal[q].rgb-low)*weight;weights+=weight;
    }
    delta/=max(weights,1e-6);
    float3 rawDelta=delta;
    if(darken!=1||brighten!=1)delta=min(delta,0)*darken+max(delta,0)*brighten;
    if(color!=1||luminance!=1){float dy=dot(delta,float3(0.2126,0.7152,0.0722));delta=dy*luminance+(delta-dy)*color;}
    delta*=total*(1-protection);
    float3 result=float3(shadowSafe(base.r,rawDelta.r,delta.r),
                         shadowSafe(base.g,rawDelta.g,delta.g),
                         shadowSafe(base.b,rawDelta.b,delta.b));
    if(correctionEnabled>0.5)
        result=NrApplyCorrection(base.rgb,result,unused>0.5,hueProtection,chromaProtection,highlightProtection,localCompression,
            neutralProtection,colorRetention,luminanceRetention,shadowProtection);
    outputTex[id.xy]=float4(unused>0.5?result:max(0,result),base.a);
}
