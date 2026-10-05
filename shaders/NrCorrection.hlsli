// Adapted from SAOG0721/Magpie, GPL-3.0, commit
// 27c5df91177a29b33be612e98274169f3d2fca49:
// src/Magpie.Core/DLSSNRColorShader.h and DLSSNRDetailShader.h.
// Oklab matrices: Bjorn Ottosson, public domain (2021-01-25),
// https://bottosson.github.io/posts/oklab/ .
// Veyra: already-linear FP16 input (no SDR decode/encode), optional controls,
// chroma-delta budget, soft highlight shoulder, 16-step gamut ray search,
// signed scRGB/BT.2020 HDR boundary, and post-temporal safety without a second
// application of residual compression. No per-frame exposure adaptation.
#ifndef VEYRA_NR_CORRECTION
#define VEYRA_NR_CORRECTION
float3 NrLinearToLab(float3 c){
    float3 lms=float3(dot(c,float3(.4122214708,.5363325363,.0514459929)),
        dot(c,float3(.2119034982,.6806995451,.1073969566)),
        dot(c,float3(.0883024619,.2817188376,.6299787005)));
    lms=sign(lms)*pow(abs(lms),1.0/3.0);
    return float3(dot(lms,float3(.2104542553,.7936177850,-.0040720468)),
        dot(lms,float3(1.9779984951,-2.4285922050,.4505937099)),
        dot(lms,float3(.0259040371,.7827717662,-.8086757660)));
}
float3 NrLabToLinear(float3 c){
    float3 v=float3(c.x+.3963377774*c.y+.2158037573*c.z,
        c.x-.1055613458*c.y-.0638541728*c.z,
        c.x-.0894841775*c.y-1.2914855480*c.z);
    v=v*v*v;
    return float3(dot(v,float3(4.0767416621,-3.3077115913,.2309699292)),
        dot(v,float3(-1.2684380046,2.6097574011,-.3413193965)),
        dot(v,float3(-.0041960863,-.7034186147,1.7076147010)));
}
float3 NrTo2020(float3 c){
    return mul(float3x3(.627404,.329283,.043313,.069097,.919540,.011362,.016391,.088013,.895595),c);
}
float3 NrTo709(float3 c){
    return mul(float3x3(1.660491,-.587641,-.072850,-.124550,1.132900,-.008349,-.018151,-.100579,1.118730),c);
}
float NrMaximum(float3 c){return max(c.r,max(c.g,c.b));}
float NrUnit(bool hdr){return hdr?203.0/80.0:1.0;}
float NrCeiling(float3 base,bool hdr){
    // Preserve source HDR peaks; enhancement does not invent a higher peak.
    // The 203-nit floor allows useful midtone detail while 125 scRGB units
    // remain the explicit 10,000-nit PQ boundary. Signed BT.709 is retained.
    return hdr?min(125.0,max(203.0/80.0,NrMaximum(NrTo2020(base))))/(203.0/80.0):1.0;
}
bool NrInGamut(float3 rgb,float ceiling,bool hdr){
    float3 c=hdr?NrTo2020(rgb):rgb;
    float tolerance=1e-6*max(1,ceiling);
    return all(c>=-tolerance)&&all(c<=ceiling+tolerance);
}
float3 NrMapLab(float3 lab,float ceiling,bool hdr){
    lab.x=clamp(lab.x,0,pow(ceiling,1.0/3.0));
    float3 rgb=NrLabToLinear(lab);
    if(!NrInGamut(rgb,ceiling,hdr)){
        float lo=0,hi=1;
        [loop]for(int k=0;k<16;++k){
            float mid=(lo+hi)*.5;
            if(NrInGamut(NrLabToLinear(float3(lab.x,lab.yz*mid)),ceiling,hdr))lo=mid;else hi=mid;
        }
        rgb=NrLabToLinear(float3(lab.x,lab.yz*lo));
    }
    // Only roundoff remains here; clipping before the ray search would rotate hue.
    return hdr?NrTo709(clamp(NrTo2020(rgb),0,ceiling)):clamp(rgb,0,ceiling);
}
float NrSoftScale(float magnitude,float knee,float span){
    if(magnitude<=knee)return 1;
    float excess=magnitude-knee;
    return (knee+excess/(1+excess/span))/max(magnitude,1e-12);
}
float NrHighlightShoulder(float target,float original,float limit){
    float start=clamp(original,0,limit);
    float room=max(limit-start,0);
    float knee=start+room*.5;
    if(target<=knee)return target;
    float span=room*.5;
    return span>1e-7?knee+span*(1-exp(-(target-knee)/span)):start;
}
float3 NrApplyCorrection(float3 original,float3 candidate,bool hdr,
                         float hue,float chroma,float highlight,float compression,
                         float neutral,float colorKeep,float lumaKeep,float shadow){
    if(hue+chroma+highlight+compression+neutral+colorKeep+lumaKeep+shadow==0||all(original==candidate))return candidate;
    if(!all(isfinite(candidate)))return original;
    float unit=NrUnit(hdr),ceiling=NrCeiling(original,hdr);
    float3 o=NrLinearToLab(original/unit),d=NrLinearToLab(candidate/unit)-o;
    float c=length(o.yz);
    if(hue>0){
        // Original hue has no definition on the gray axis. Smoothly fade out
        // there; projection cannot overshoot through gray into the opposite hue.
        float weight=hue*smoothstep(0,.02,c);
        float2 n=o.yz/max(c,1e-12),target=o.yz+d.yz;
        d.yz=lerp(target,n*max(dot(target,n),0),weight)-o.yz;
    }
    if(chroma>0){
        float magnitude=length(d.yz);
        d.yz*=lerp(1,NrSoftScale(magnitude,.008+.12*c,.02+.15*c),chroma);
    }
    if(compression>0)d*=lerp(1,NrSoftScale(length(d),.04,.12),compression);
    if(highlight>0){
        float target=o.x+d.x;
        // Darkening can recover highlight detail: only outward lightness
        // gets the shoulder. Classification always uses the original base.
        if(d.x>0)d.x=lerp(target,NrHighlightShoulder(target,o.x,pow(ceiling,1.0/3.0)),highlight)-o.x;
    }
    // Lightness is independent of chromaticity. A manual value of one keeps
    // original lightness; shadow protection only restores excessive darkening
    // in source shadows, so positive shadow detail does not get suppressed.
    d.x*=1-lumaKeep;
    if(d.x<0)d.x*=1-shadow*(1-smoothstep(.12,.5,o.x));
    // Rebase source chroma to CURRENT lightness. Holding raw Lab a/b while
    // changing L changes saturation and can make styles 1/2 look color-shifted.
    // This preserves original RGB ratios at full retention, not a gray overlay.
    float2 prospectiveChroma=o.yz*((o.x+d.x)/max(o.x,1e-6));
    // Ignore tiny chromaticity noise from proxy/FP16 quantization. Source
    // color retention remains explicit; the neutral guard only repairs tint.
    float tintRisk=smoothstep(.001,.004,length(o.yz+d.yz-prospectiveChroma));
    float keep=1-(1-colorKeep)*(1-neutral*tintRisk*(1-smoothstep(.04,.14,c/max(o.x,.02))));
    // A source-color ray can hit the gamut ceiling before the gray axis does.
    // Respect that ray's lightness bound; otherwise the final gamut mapper
    // would desaturate a "fully retained" color near white.
    if(keep>0){
        float sourcePeak=NrMaximum(hdr?NrTo2020(original/unit):original/unit);
        float sourceLimit=o.x*pow(ceiling/max(sourcePeak,1e-9),1.0/3.0);
        d.x=lerp(o.x+d.x,min(o.x+d.x,sourceLimit),keep)-o.x;
    }
    float2 sourceChroma=o.yz*((o.x+d.x)/max(o.x,1e-6));
    d.yz=lerp(o.yz+d.yz,sourceChroma,keep)-o.yz;
    return NrMapLab(o+d,ceiling,hdr)*unit;
}
float3 NrTemporalSafety(float3 base,float3 candidate,bool hdr,bool spatial){
    if(!spatial||all(candidate==base))return candidate;
    if(!all(isfinite(candidate)))return base;
    float unit=NrUnit(hdr),ceiling=NrCeiling(base,hdr);
    if(NrInGamut(candidate/unit,ceiling,hdr))return candidate;
    return NrMapLab(NrLinearToLab(candidate/unit),ceiling,hdr)*unit;
}
#endif
