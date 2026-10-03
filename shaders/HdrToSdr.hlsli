// BT.2390 EETF, ideal black, PQ-domain Hermite shoulder. Target peak is
// explicitly SDR display white; output is relative linear BT.709 for the
// existing single sRGB encoding stage. Not used by native HDR or NR proxies.
float TonePq(float nits) {
    float p=pow(saturate(nits/10000.0),2610.0/16384.0);
    return pow((3424.0/4096.0+(2413.0/128.0)*p)/(1+(2392.0/128.0)*p),2523.0/32.0);
}
float ToneNits(float code) {
    float p=pow(saturate(code),32.0/2523.0);
    return 10000.0*pow(max(p-3424.0/4096.0,0)/max(2413.0/128.0-(2392.0/128.0)*p,1e-6),16384.0/2610.0);
}
float ToneLuminance(float nits,float sourcePeak,float targetPeak) {
    if(nits<=0)return 0;
    if(sourcePeak<=targetPeak)return min(nits/targetPeak,1.0);
    float black=TonePq(0),span=TonePq(sourcePeak)-black;
    float x=saturate((TonePq(nits)-black)/span);
    float top=(TonePq(targetPeak)-black)/span;
    float knee=1.5*top-.5;
    if(x>=knee){
        float t=(x-knee)/(1-knee),t2=t*t,t3=t2*t;
        x=(2*t3-3*t2+1)*knee+(t3-2*t2+t)*(1-knee)+(-2*t3+3*t2)*top;
    }
    return saturate(ToneNits(x*span+black)/targetPeak);
}
float3 ToneGamut(float3 rgb,float y) {
    // Retain signed out-of-gamut components until a common chroma scale can
    // be found. Move along the RGB chroma ray toward neutral, preserving Y;
    // independent channel clipping would change both luminance and hue.
    if(y<=0)return 0;if(y>=1)return 1;
    float3 chroma=rgb-y;
    float3 occupancy=select(chroma>=0,chroma/max(1-y,1e-7),-chroma/max(y,1e-7));
    float extent=max(occupancy.r,max(occupancy.g,occupancy.b));
    // Smooth shoulder begins at 90% of the target gamut boundary. All
    // interior colors below that point remain unchanged; no hard hue bends.
    if(extent>.9){float compressed=.9+.1*(1-exp(-(extent-.9)/.1));chroma*=compressed/extent;}
    return y+chroma;
}
float3 HdrToSdr(float3 linear709Nits,float sourcePeak,float targetPeak) {
    float y=dot(linear709Nits,float3(.212639,.715169,.072192));
    if(y<=0)return 0;
    float mapped=ToneLuminance(y,sourcePeak,targetPeak);
    return ToneGamut(linear709Nits*(mapped/y),mapped);
}
