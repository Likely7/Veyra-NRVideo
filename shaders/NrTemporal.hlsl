// Adapted from SAOG0721/Magpie, GPL-3.0, commit
// 3841698348bfb246623d4acf791984c8b68a577b
// src/Magpie.Core/DLSSNRTemporalShader.h (motion route 2).
// Veyra: D3D12 Load-based bilinear sampling, source-flow extent conversion,
// bounded signed perceptual guide, reset via zero weight, post-protection residual.
//
// Anti-flicker tiers (Route, stage 6 of
// docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md). Route numbers follow the upstream
// shader's own meaning so the two stay comparable:
//   1 Static       accumulate only where the patch is not moving
//   2 Flow         the original 80 ms motion-reprojected EMA (unchanged path)
//   3 FlowPlus     60 ms attack / 180 ms release memory + direction-reversal
//                  rejection, plus an explicit support channel in history alpha
//   4 LowFrequency route 2 on a half-resolution residual; this frame's high
//                  frequency rides through untouched
// Route 2 is byte-for-byte the behaviour that existed before the tiers, so a
// preset that only ever knew "temporal on" keeps producing what it produced.
#include "NrProtection.hlsli"
#include "NrCorrection.hlsli"
Texture2D<float4> Base:register(t0);
Texture2D<float4> Raw:register(t1);
Texture2D<float4> History:register(t2);
Texture2D<float4> PreviousGuide:register(t3);
Texture2D<float2> Motion:register(t4);
Texture2D<float4> LowResidual:register(t5);
Texture2D<float4> LowGuide:register(t6);
RWTexture2D<float4> Output:register(u0);
RWTexture2D<float4> NextHistory:register(u1);
RWTexture2D<float4> NextGuide:register(u2);
cbuffer Settings:register(b0){
    uint2 Size;float HistoryWeight;float Total;float ProtectionEnabled;float Feather;float2 reserved;float4 Regions[4];
    uint Route;float2 LowSize;float RoutePad;
    float CorrectionEnabled;float HueProtection;float ChromaProtection;float HighlightProtection;
    float LocalCompression;float TemporalStability;float CorrectionAutomatic;float CorrectionHdr;
}
float GuideChannel(float c){
    // The matching tolerance must distinguish visible changes in shadows,
    // not treat a several-fold linear-light change as the same observation.
    // Retain the bounded signed HDR mapping, then use the sRGB transfer for
    // guide distance only. Residuals and the output remain signed linear.
    float mapped=abs(c)/(1+abs(c));
    return sign(c)*(mapped<=.0031308?12.92*mapped:1.055*pow(mapped,1.0/2.4)-.055);
}
float3 Guide(float3 c){return float3(GuideChannel(c.r),GuideChannel(c.g),GuideChannel(c.b));}
float RebaseResidual(float base,float raw,float correction){
    // A valid old residual can exceed a darker current base. As in
    // NrResidualComposite::shadowSafe, compress extra darkening smoothly
    // instead of manufacturing a negative channel or hard-clipping detail.
    // Only nonnegative inputs have this constraint: signed HDR/gamut data
    // must retain its sign. At a zero anchor there is no darker valid value.
    float anchorDelta=min(raw-base,0.0);
    if(base>=0&&raw>=0&&correction<anchorDelta){
        float anchor=min(base,raw);
        return anchor>0?anchor/(1.0+(anchorDelta-correction)/anchor):0;
    }
    return base+correction;
}
bool Inside(int2 p){return all(p>=0)&&all(p<int2(Size));}
float4 Bilinear(Texture2D<float4> tex,float2 p){
    int2 a=int2(floor(p));float2 f=frac(p);int2 b=min(a+1,int2(Size)-1);
    return lerp(lerp(tex[a],tex[int2(b.x,a.y)],f.x),lerp(tex[int2(a.x,b.y)],tex[b],f.x),f.y);
}
bool PreviousPosition(int2 p,out float2 previous){
    previous=p;if(!Inside(p))return false;
    uint w,h;Motion.GetDimensions(w,h);
    float2 at=(float2(p)+.5)*float2(w,h)/Size-.5;
    int2 a=int2(floor(at));float2 f=frac(at);int2 hi=int2(w,h)-1;
    float2 mv=lerp(lerp(Motion[clamp(a,0,hi)],Motion[clamp(a+int2(1,0),0,hi)],f.x),lerp(Motion[clamp(a+int2(0,1),0,hi)],Motion[clamp(a+1,0,hi)],f.x),f.y)*float2(Size)/float2(w,h);
    if(!all(isfinite(mv)))return false;previous+=mv;
    return all(previous>=0)&&all(previous<=float2(Size)-1);
}
// Route 4: rebuild a low-frequency observation at this pixel from the half-res
// pair, accepting only blocks whose guide still matches this pixel's colour.
// Returning false means "no usable low-frequency support", which must publish
// no history rather than a fabricated zero.
bool Reconstruct(int2 p,float3 color,out float3 residual){
    int2 a=clamp(int2(floor(float2(p)*.5-.25)),0,int2(LowSize)-1);
    int2 b=clamp(int2(floor(float2(p)*.5-.25))+1,0,int2(LowSize)-1);
    float2 ca=min(float2(a)*2+.5,float2(Size)-1);
    float2 cb=min(float2(b)*2+.5,float2(Size)-1);
    float2 f=saturate((float2(p)-ca)/max(cb-ca,float2(1,1)));
    residual=0;float mass=0;
    [unroll]for(int y=0;y<2;++y){
        [unroll]for(int x=0;x<2;++x){
            int2 n=int2(x?b.x:a.x,y?b.y:a.y);
            float4 r=LowResidual[n];float4 g=LowGuide[n];
            if(!all(isfinite(r))||!all(isfinite(g))||r.a<.999||g.a<.999)continue;
            float3 delta=abs(color-g.rgb);
            float accept=1-smoothstep(.02,.10,max(delta.r,max(delta.g,delta.b)));
            float w=(x?f.x:1-f.x)*(y?f.y:1-f.y)*accept;
            if(w<=0)continue;
            residual+=w*r.rgb;mass+=w;
        }
    }
    if(mass<.05)return false;
    residual/=mass;
    return true;
}
// Route 4's history read: validate each tap against the guide before blending,
// so a half-res block straddling an edge cannot leak across it.
bool GatherHistory(float2 previous,float3 color,out float4 result){
    int2 a=int2(floor(previous));float2 f=frac(previous);
    result=0;float mass=0;
    [unroll]for(int y=0;y<2;++y){
        [unroll]for(int x=0;x<2;++x){
            int2 n=a+int2(x,y);
            float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);
            if(w<=0||!Inside(n))continue;
            float4 g=PreviousGuide[n];float4 h=History[n];
            if(!all(isfinite(g))||!all(isfinite(h))||g.a<.999||h.a<.999)continue;
            float3 delta=abs(color-g.rgb);
            w*=1-smoothstep(.025,.10,max(delta.r,max(delta.g,delta.b)));
            if(w<=0)continue;
            result+=w*h;mass+=w;
        }
    }
    if(mass<.25)return false;
    result/=mass;
    return true;
}
groupshared float4 TileResidual[100];
groupshared float4 TilePosition[100];
groupshared float TileError[100];
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID,uint lane:SV_GroupIndex){
    // The 8x8 output tile shares a one-pixel halo. Each observation is computed
    // once instead of nine times; thresholds and accumulation order stay intact.
    if(HistoryWeight>0){
        for(uint tap=lane;tap<100;tap+=64){
            int2 n=int2(id.xy)-int2(local.xy)+int2(tap%10,tap/10)-1;
            float2 oldPosition;float4 residual=0,position=0;float error=0;
            if(PreviousPosition(n,oldPosition)){
                float4 inputN=Base[n],old=Bilinear(PreviousGuide,oldPosition);
                float3 residualN=Raw[n].rgb-inputN.rgb;
                bool valid=all(isfinite(inputN))&&all(isfinite(old))&&old.a>=.999&&all(isfinite(residualN));
                float3 delta=abs(Guide(inputN.rgb)-old.rgb);
                error=max(delta.r,max(delta.g,delta.b));
                residual=float4(residualN,valid?1:0);
                position=float4(oldPosition,1,0);
            }
            TileResidual[tap]=residual;TilePosition[tap]=position;TileError[tap]=error;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    int2 p=id.xy;if(any(id.xy>=Size))return;
    float4 base=Base[p],raw=Raw[p];bool finite=all(isfinite(base))&&all(isfinite(raw));
    NextGuide[p]=all(isfinite(base))?float4(Guide(base.rgb),1):0;
    if(!finite){NextHistory[p]=0;Output[p]=all(isfinite(base))?base:float4(0,0,0,1);return;}
    // Optional control must not keep an old correction after the current NR
    // produces an exact identity. The original temporal path stays unchanged.
    if(CorrectionEnabled>0.5&&all(raw.rgb==base.rgb)){NextHistory[p]=0;Output[p]=raw;return;}
    // Route 4 splits the observation: the accumulated signal is the
    // low-frequency component and `detail` carries this frame's exact
    // high-frequency complement through untouched.
    float3 detail=0;
    if(Route==4){
        float3 low;
        if(!Reconstruct(p,Guide(base.rgb),low)){NextHistory[p]=0;Output[p]=raw;return;}
        detail=(raw.rgb-base.rgb)-low;
        // `result` below now accumulates the low-frequency observation, so the
        // residual used for the guide/box comparison must be the same signal.
        // The tile cache above holds the full-band residual for route 2/3; for
        // route 4 the per-pixel comparison is replaced below.
    }
    // Protected/feathered pixels are excluded from history entirely: the caller
    // composites the original image there, so nothing this pass emits should be
    // remembered. This is a protection decision, not an observation.
    if(NrProtection(float2(p)+.5,Size,ProtectionEnabled,Feather,Regions)>0){NextHistory[p]=0;Output[p]=raw;return;}
    // A zero requested correction is a valid observation, not a protection mask.
    // It publishes the raw value, but must still record that as history: wiping
    // the chain here would drop the accumulated correction the moment a layer's
    // total reached zero, and the next frame would restart from nothing. The
    // temporal blend itself is gated on Total below.
    const bool observesCorrection=Total!=0;
    float3 current=raw.rgb-base.rgb;
    if(Route==4)current=current-detail; // accumulate the low-frequency part only
    float3 result=current;float2 previous;
    // Route 3 carries a support channel in history alpha: the residual is only
    // applied in proportion to how strongly the source is currently observed.
    float support=saturate(smoothstep(.005,.02,max(abs(Guide(raw.rgb)-Guide(base.rgb)).r,
                                                 max(abs(Guide(raw.rgb)-Guide(base.rgb)).g,
                                                     abs(Guide(raw.rgb)-Guide(base.rgb)).b))));
    uint center=(local.y+1)*10+local.x+1;
    if(observesCorrection&&HistoryWeight>0&&TilePosition[center].z>0){
        previous=TilePosition[center].xy;
        float error=0,maximumError=0;bool valid=true;float3 lo=current,hi=current;
        [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
            int2 n=p+int2(x,y);uint tap=center+y*10+x;
            float2 previousN=TilePosition[tap].xy;
            if(TilePosition[tap].z==0){valid=false;continue;}
            if(any(abs((previousN-n)-(previous-p))>2)){valid=false;continue;}
            float3 residualN=TileResidual[tap].rgb;
            if(TileResidual[tap].a==0){valid=false;continue;}
            // Route 4 accumulates a half-resolution signal, so the full-band tile
            // residual is not the quantity being filtered. Its spatial agreement
            // still gates motion validity, but the box must come from the
            // reconstructed low-frequency observation instead.
            if(Route==4){float3 lowN;if(!Reconstruct(n,Guide(Base[n].rgb),lowN)){valid=false;continue;}residualN=lowN;}
            float e=TileError[tap];error+=e/9;maximumError=max(maximumError,e);lo=min(lo,residualN);hi=max(hi,residualN);
        }
        float q=(1-smoothstep(.008,.04,error))*(1-smoothstep(.025,.10,maximumError));
        // Route 1 (static): accumulate only where the patch is essentially
        // still. No smooth falloff: any real motion publishes immediately, which
        // is what makes this the tier with the least ghosting risk.
        if(Route==1)q=maximumError<.008?1:0;
        if(valid&&q>0){
            float4 old=0;bool historyValid=true;
            if(Route==4)historyValid=GatherHistory(previous,Guide(base.rgb),old);
            else old=Bilinear(History,previous);
            if(historyValid&&all(isfinite(old))&&old.a>=.999){
                float3 margin=.02+q*abs(old.rgb);
                float3 safe=clamp(old.rgb,lo-margin,hi+margin);
                if(Route==3){
                    // 60 ms attack / 180 ms release, derived from the same
                    // capture interval as the 80 ms amplitude EMA. q applies once.
                    float oldSupport=saturate(old.a-1);
                    float memory=pow(abs(HistoryWeight),.08/(support>oldSupport?.06:.18))*q;
                    support=lerp(support,oldSupport,memory);
                    if(oldSupport<=.001)result=current;
                    else if(support>0&&dot(current,old.rgb)<0){
                        // An opposite-direction correction starts a new episode
                        // instead of being averaged into the old one.
                        support=support*(1-pow(abs(HistoryWeight),.08/.06)*q);
                        result=current;
                    }else{
                        float update=(1-HistoryWeight*q)*support;
                        result=lerp(safe,current,update);
                    }
                }else result=lerp(current,safe,HistoryWeight*q);
            }
        }
    }
    float3 resolved=float3(RebaseResidual(base.r,raw.r,result.r),
                           RebaseResidual(base.g,raw.g,result.g),
                           RebaseResidual(base.b,raw.b,result.b));
    // History must describe what was actually emitted, not the rejected
    // correction; otherwise the next frame reuses the same invalid shadow.
    // Route 3 stores 1+support in alpha: the support channel rides along with
    // the observation rather than in a separate texture.
    // Route 4 restores this frame's high frequency after the temporal filter.
    float3 composited=resolved+detail;
    if(CorrectionEnabled>0.5){
        composited=NrTemporalSafety(base.rgb,composited,CorrectionHdr>0.5,
            HueProtection+ChromaProtection+HighlightProtection+LocalCompression>0);
        resolved=composited-detail;
    }
    NextHistory[p]=float4(clamp(resolved-base.rgb,-65504,65504),Route==3?1+support:1);
    // Preserve signed wide-gamut/HDR working components (no SDR saturate).
    Output[p]=float4(clamp(composited,-65504,65504),raw.a);
}
