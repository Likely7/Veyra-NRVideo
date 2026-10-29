// YUV (NV12/P010) -> linear BT.709 working RGB (Playbook section 13.3).
// Reads plane 0 as R8/R16 UNORM and plane 1 as R8G8/R16G16 UNORM via two
// SRVs; the dispatch covers the full frame. Color metadata (range/matrix/
// transfer) arrives as root constants set per frame from FFmpeg stream data.
#include "HdrColor.hlsli"
#include "HdrToSdr.hlsli"
#include "ColorGrade.hlsli"

cbuffer YuvParams : register(b0)
{
    float4 colorParams0; // x=range (0 full, 1 limited, 2 limited luma/full chroma) y=matrix z=transfer w=bit depth class
    uint4 yuvDimensions; // x=width y=height z=bit0 nativeHDR, bit1 BT2020 primaries; w=chroma location
    float4 toneMapParams; // x=validated source peak nits, y=SDR target peak nits
                          // Custom: z=exposure in EV, w=shoulder (1.0 = the
                          // previous fixed knee). z/w were unused before.
    // Colour grade (plan v4): appended after the existing constants, packed by
    // veyra::pipeline::packColorGradeConstants (five float4s).
    float4 colorRow0; float4 colorRow1; float4 colorRow2;
    float4 colorControls; float4 colorFlags;
    // Dolby Vision profile 5 (IPT-PQ-C2) base layer: seven float4s appended
    // after the colour grade, packed by
    // veyra::pipeline::packDolbyVisionP5Constants. Rows are row-major with one
    // reshaping term in .w (mirror of include/veyra/pipeline/DolbyVisionP5.h):
    //   dv0.xyz ycc_to_rgb row0  dv0.w curve[0].x
    //   dv1.xyz ycc_to_rgb row1  dv1.w curve[0].y
    //   dv2.xyz ycc_to_rgb row2  dv2.w curve[0].z
    //   dv3.xyz lms_to_rgb row0  dv3.w curve[1].x
    //   dv4.xyz lms_to_rgb row1  dv4.w curve[1].y
    //   dv5.xyz lms_to_rgb row2  dv5.w curve[1].z
    //   dv6.xyz curve[2]         dv6.w 1 when the conversion is live
    float4 dv0; float4 dv1; float4 dv2;
    float4 dv3; float4 dv4; float4 dv5; float4 dv6;
};

Texture2D<float> lumaPlane : register(t0);   // R8_UNORM or R16_UNORM
Texture2D<float2> chromaPlane : register(t1); // R8G8_UNORM or R16G16_UNORM
Texture2D<float2> chromaPlane2 : register(t2); // second chroma view (unused for NV12; reserved)
// Custom (P7): enhancement layer planes, integer samples, 4:2:0 10-bit:
// luma at full size, chroma at half size per axis.
Texture2D<uint> elLumaPlane : register(t12);
Texture2D<uint2> elChromaPlane : register(t13);
// Custom (P7): reshaping weights for the components whose inverse mapping is an
// MMR polynomial (18 float4s, one row). Mirrors libplacebo's reshape shader.
Texture2D<float4> elCurvePlane : register(t14);
RWTexture2D<float4> linearRgb : register(u0);

float ExpandLimited(float c)
{
    // 16..235 -> 0..1 (8-bit ranges; P010 carries the same studio swing in
    // the top bits, so the normalized sample maps identically).
    return saturate((c - 16.0 / 255.0) * 255.0 / 219.0);
}

float3 YuvToRgb(float y, float2 uv)
{
    // P010 is stored as 10 significant HIGH bits in 16-bit UNORM. Its
    // normalization is not 8-bit /255: decode legal code values explicitly.
    bool sixteen=colorParams0.w>1.5,ten=colorParams0.w>0.5;
    float scale=sixteen?65535.0:ten?65535.0/64.0:255.0;
    float black=sixteen?4096.0:ten?64.0:16.0,white=sixteen?60160.0:ten?940.0:235.0;
    float middle=sixteen?32768.0:ten?512.0:128.0,span=sixteen?57344.0:ten?896.0:224.0;
    float maximum=sixteen?65535.0:ten?1023.0:255.0;
    // colorParams0.x: 0 full range, 1 limited, 2 limited luma + full-range
    // chroma (Elgato cards other than the MK.2 send HDR P010 that way).
    bool limitedLuma=colorParams0.x>0.5,limitedChroma=limitedLuma&&colorParams0.x<1.5;
    float yy=limitedLuma?(y*scale-black)/(white-black):y*scale/maximum;
    yy=saturate(yy);
    float uu=(uv.x*scale-middle)/(limitedChroma?span:maximum);
    float vv=(uv.y*scale-middle)/(limitedChroma?span:maximum);

    if(colorParams0.y>1.5){
        // BT.2020 non-constant-luminance coefficients.
        return float3(yy+1.4746*vv,yy-0.164553*uu-0.571353*vv,yy+1.8814*uu);
    }

    if (colorParams0.y > 0.5) {
        // BT.709.
        float r = yy + 1.5748 * vv;
        float g = yy - 0.1873 * uu - 0.4681 * vv;
        float b = yy + 1.8556 * uu;
        return float3(r, g, b);
    }
    // BT.601.
    float r = yy + 1.402 * vv;
    float g = yy - 0.3441 * uu - 0.7141 * vv;
    float b = yy + 1.772 * uu;
    return float3(r, g, b);
}

float SrgbDecode(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// ST2084 EOTF: PQ code value -> absolute luminance, 1.0 = 10000 nits.
float3 PqToNits(float3 code)
{
    const float m1=2610.0/16384.0,m2=2523.0/32.0;
    float3 p=pow(saturate(code),1.0/m2);
    return 10000.0*pow(max(p-3424.0/4096.0,0.0)/max(2413.0/128.0-2392.0/128.0*p,1e-6),1.0/m1);
}

// ST2084 inverse EOTF: absolute luminance -> PQ code value in 0..1.
float3 NitsToPq(float3 nits)
{
    const float m1=2610.0/16384.0,m2=2523.0/32.0;
    const float3 y=pow(saturate(max(nits,0.0)/10000.0),m1);
    return pow((0.8359375+18.8515625*y)/(1.0+18.6875*y),m2);
}

// One RPU reshaping curve, projected on {1, sqrt(x), x} by the host.
float DvReshape(float3 curve,float x)
{
    x=saturate(x);
    return curve.x+curve.y*sqrt(x)+curve.z*x;
}

// Dolby Vision profile 5 base layer. A profile-5 base layer is IPT-PQ-C2, not
// BT.2020 YCbCr, so the ordinary YUV matrix cannot describe it: decoding it as
// HDR10 is what produces the green picture. Only the RPU states the colour, so
// this replays the profile-5 chain and returns the PQ-encoded BT.2020 RGB the
// rest of this shader already knows how to tone-map.
float3 DoviP5ToPqBt2020(float luma,float2 chroma)
{
    // IPT-PQ-C2 is full range by definition; the base-layer range tag is not
    // consulted here for that reason.
    const bool sixteen=colorParams0.w>1.5,ten=colorParams0.w>0.5;
    const float scale=sixteen?65535.0:ten?65535.0/64.0:255.0;
    const float maximum=sixteen?65535.0:ten?1023.0:255.0;
    const float3 code=saturate(float3(luma,chroma)*scale/maximum);
    const float3 curve0=float3(dv0.w,dv1.w,dv2.w);
    const float3 curve1=float3(dv3.w,dv4.w,dv5.w);
    const float3 curve2=dv6.xyz;
    // 1. Per-component RPU reshaping, then centre chroma on 0.5. The reshaped
    //    values may leave 0..1 - that is what the curve is for - and the matrix
    //    absorbs the excursion.
    const float3 ipt=float3(DvReshape(curve0,code.x),
                            DvReshape(curve1,code.y)-0.5,
                            DvReshape(curve2,code.z)-0.5);
    // 2. IPT carried as Y/Cb/Cr -> PQ-domain RGB, using the RPU's own matrix.
    const float3 pq=saturate(mul(float3x3(dv0.xyz,dv1.xyz,dv2.xyz),ipt));
    // 3. Linearise, apply the RPU crosstalk plus the fixed HPE LMS -> BT.2020
    //    stage (folded into one matrix on the host), then re-encode.
    return NitsToPq(mul(float3x3(dv3.xyz,dv4.xyz,dv5.xyz),PqToNits(pq)));
}

float2 ReconstructChroma(uint2 pixel)
{
    uint location=yuvDimensions.w;
    if(location==0)return chromaPlane[pixel/2];
    // Chroma sample origin in luma pixel-center coordinates. Respect MPEG
    // left, JPEG center and vertical top/bottom placement; no luma filtering.
    float2 origin=float2(0.0,0.5);
    if(location==2)origin=float2(0.5,0.5);
    else if(location==3)origin=float2(0.0,0.0);
    else if(location==4)origin=float2(0.5,0.0);
    else if(location==5)origin=float2(0.0,1.0);
    else if(location==6)origin=float2(0.5,1.0);
    float2 position=(float2(pixel)-origin)*0.5;
    int2 base=int2(floor(position));float2 fraction=frac(position);
    // Clamp to the visible chroma extent, not decoder allocation padding.
    int2 last=int2((yuvDimensions.xy+1)/2)-1;
    float2 a=chromaPlane[clamp(base,int2(0,0),last)];
    float2 b=chromaPlane[clamp(base+int2(1,0),int2(0,0),last)];
    float2 c=chromaPlane[clamp(base+int2(0,1),int2(0,0),last)];
    float2 d=chromaPlane[clamp(base+int2(1,1),int2(0,0),last)];
    return lerp(lerp(a,b,fraction.x),lerp(c,d,fraction.x),fraction.y);
}


// Custom (P7): ETSI GS CCM 001 5.4.3 reconstruction, mirroring
// veyra::engine::doviBlElComposeLuma / doviComposeLumaSample (unit tested).
// Every component of the layer carries its own LINEAR_DZ quantiser:
//   dv0 = channel 0 (Y)  nlq: (offset, slope, threshold, inMax)
//   dv1 = channel 1 (Cb) nlq
//   dv2 = channel 2 (Cr) nlq
//   dv3 = (coefLog2Denom, elBitDepth, outBitDepth, 0)
//   dv4 = (elWidth, elHeight, 0, 0)
//   dv6.w == 2 when live, 3 when the residual should be drawn instead.
float SampleElLuma(float2 pos, int2 elSize)
{
    const float2 texel = pos - 0.5;
    const int2 base = int2(floor(texel));
    const float2 f = texel - floor(texel);
    const int2 hi = elSize - 1;
    const int2 c0 = clamp(base, int2(0, 0), hi);
    const int2 c1 = clamp(base + int2(1, 0), int2(0, 0), hi);
    const int2 c2 = clamp(base + int2(0, 1), int2(0, 0), hi);
    const int2 c3 = clamp(base + int2(1, 1), int2(0, 0), hi);
    const float a = float(elLumaPlane.Load(int3(c0, 0)));
    const float b = float(elLumaPlane.Load(int3(c1, 0)));
    const float c = float(elLumaPlane.Load(int3(c2, 0)));
    const float d = float(elLumaPlane.Load(int3(c3, 0)));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// The layer is 4:2:0, so its chroma plane is half the extent per axis. `pos`
// arrives in luma pixels; resample bilinearly like luma, then round.
float2 SampleElChroma(float2 pos, int2 elSize)
{
    const int2 crSize = int2((elSize + 1) / 2);
    const float2 texel = pos * 0.5 - 0.5;
    const int2 base = int2(floor(texel));
    const float2 f = texel - floor(texel);
    const int2 hi = crSize - 1;
    const int2 c0 = clamp(base, int2(0, 0), hi);
    const int2 c1 = clamp(base + int2(1, 0), int2(0, 0), hi);
    const int2 c2 = clamp(base + int2(0, 1), int2(0, 0), hi);
    const int2 c3 = clamp(base + int2(1, 1), int2(0, 0), hi);
    const float2 a = float2(elChromaPlane.Load(int3(c0, 0)));
    const float2 b = float2(elChromaPlane.Load(int3(c1, 0)));
    const float2 c = float2(elChromaPlane.Load(int3(c2, 0)));
    const float2 d = float2(elChromaPlane.Load(int3(c3, 0)));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// LINEAR_DZ inverse quantisation of one component, in the 17-bit signed domain
// ETSI GS CCM 001 uses for the sum with the base layer.
int P7Residual(int e, float4 nlq, int elBitDepth, int coefLog2Denom)
{
    int rr = e - int(nlq.x + 0.5);
    if (rr == 0) return 0;                    // no residual on this sample
    const int sgn = rr < 0 ? -1 : 1;
    rr = rr * 2 - sgn;                        // |rr| - 0.5, in half-code units
    rr = rr << (10 - elBitDepth);
    int dq = rr * int(nlq.y + 0.5);
    dq += (int(nlq.z + 0.5) << (10 - elBitDepth + 1)) * sgn;
    const int limit = int(nlq.w + 0.5) << (10 - elBitDepth + 1);
    dq = clamp(dq, -limit, limit);
    return dq >> (coefLog2Denom - 5 - elBitDepth);
}

// One component of the base layer's inverse reshaping, evaluated the way
// libplacebo's reshape_mmr() does: the seven weights of an order are packed into
// two float4s, and the cross terms are the same r*g, r*b, g*b, r*g*b products.
float MmrEvaluate(int component, float3 sig, float constant, int order)
{
    const int base = component * 6;
    float4 sigX;
    sigX.xyz = sig.xxy * sig.yzz;   // (r*g, r*b, g*b)
    sigX.w = sigX.x * sig.z;        // r*g*b
    float s = constant;
    s += dot(elCurvePlane.Load(int3(base + 0, 0, 0)).xyz, sig);
    s += dot(elCurvePlane.Load(int3(base + 1, 0, 0)), sigX);
    if (order >= 2) {
        const float3 sig2 = sig * sig;
        const float4 sigX2 = sigX * sigX;
        s += dot(elCurvePlane.Load(int3(base + 2, 0, 0)).xyz, sig2);
        s += dot(elCurvePlane.Load(int3(base + 3, 0, 0)), sigX2);
        if (order >= 3) {
            s += dot(elCurvePlane.Load(int3(base + 4, 0, 0)).xyz, sig2 * sig);
            s += dot(elCurvePlane.Load(int3(base + 5, 0, 0)), sigX2 * sigX);
        }
    }
    return saturate(s);
}

// dv4 = (elWidth, elHeight, mmrConstant[0], mmrConstant[1])
// dv5 = (mmrConstant[2], mode[0], mode[1], mode[2])
// dv6 = (order[0], order[1], order[2], live flag)
float3 ReconstructP7(float3 sig, uint2 tid, out int3 residualOut)
{
    const int elBitDepth = int(dv3.y + 0.5);
    const int outBitDepth = int(dv3.z + 0.5);
    const int coefLog2Denom = int(dv3.x + 0.5);
    const int2 elSize = int2(dv4.xy + 0.5);
    const float2 pos = float2(tid) * float2(elSize) / float2(yuvDimensions.xy);
    const float2 elChroma = SampleElChroma(pos, elSize);
    const int3 e = int3(int(round(SampleElLuma(pos, elSize))),
                        int(round(elChroma.x)),
                        int(round(elChroma.y)));
    const int3 residual = int3(P7Residual(e.x, dv0, elBitDepth, coefLog2Denom),
                               P7Residual(e.y, dv1, elBitDepth, coefLog2Denom),
                               P7Residual(e.z, dv2, elBitDepth, coefLog2Denom));
    residualOut = residual;
    // Inverse reshaping of the base layer, then the residual on top. A component
    // whose mapping is the identity still goes through the same 16-bit domain so
    // the sum below has one unit.
    //
    // Lane map (see DolbyVisionP7.h): mode[0..2] is dv5.y/.z/.w, the MMR constant
    // is dv4.z / dv4.w / dv5.x, the order is dv6.x/.y/.z. Each component reads its
    // OWN triple: an earlier version indexed one component off, which silently
    // skipped Cb's mapping because it tested the luma mode flag.
    float3 base = sig;
    if (dv5.y > 0.5) base.x = MmrEvaluate(0, sig, dv4.z, int(dv6.x + 0.5));
    if (dv5.z > 0.5) base.y = MmrEvaluate(1, sig, dv4.w, int(dv6.y + 0.5));
    if (dv5.w > 0.5) base.z = MmrEvaluate(2, sig, dv5.x, int(dv6.z + 0.5));
    int3 hh = int3(round(base * 65535.0)) + residual + (1 << (15 - outBitDepth));
    hh = hh >> (16 - outBitDepth);
    const int maxOut = (1 << outBitDepth) - 1;
    return float3(clamp(hh, 0, maxOut)) / float(maxOut);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= yuvDimensions.x || dispatchThreadId.y >= yuvDimensions.y) {
        return;
    }
    const float y = lumaPlane[uint2(dispatchThreadId.x, dispatchThreadId.y)];
    const float2 uv = ReconstructChroma(dispatchThreadId.xy);
    // Custom (P7): dv6.w == 2 rebuilds the 12-bit signal from base + enhancement
    // before the colour conversion; dv6.w == 1 is the profile 5 conversion.
    // dv6.w == 2 reconstructs; dv6.w == 3 reconstructs and shows the gap instead.
    const bool p7Dump = abs(dv6.w - 3.0) < 0.5;
    const bool p7Live = p7Dump || abs(dv6.w - 2.0) < 0.5;
    int3 elResidual = int3(0, 0, 0);
    float3 yuvCode = float3(y, uv);
    if (p7Live) yuvCode = ReconstructP7(yuvCode, dispatchThreadId.xy, elResidual);
    float3 rgb = abs(dv6.w - 1.0) < 0.5 ? DoviP5ToPqBt2020(yuvCode.x, uv) : YuvToRgb(yuvCode.x, yuvCode.yz);
    // Custom (P7) diagnostic: showing the gap between the reconstructed signal and
    // the base layer proves the enhancement layer took part; a frame without one
    // renders pure black here.
    if (p7Dump) {
        // The residual itself: a frame without an enhancement layer is pure black,
        // one with it lights up where the enhancement layer acts.
        const float el = saturate(float(max(max(abs(elResidual.x), abs(elResidual.y)), abs(elResidual.z))) / 400.0);
        rgb = float3(el, el, el);
    }
    rgb = saturate(rgb);
    if(colorParams0.z>3.5){
        // ST2084 EOTF to absolute cd/m2, then BT.2020 -> BT.709 linear.
        float3 nits=PqToNits(rgb);
        if(colorParams0.z>4.5){
            // BT.2100 HLG reference display: 1000-nit peak, gamma 1.2,
            // ideal black. HLG scene light needs its OOTF before PQ/scRGB.
            float3 scene=select(rgb<=.5,rgb*rgb/3.0,(exp((rgb-.55991073)/.17883277)+.28466892)/12.0);
            nits=1000.0*scene*pow(max(dot(scene,float3(.2627,.6780,.0593)),0),.2);
        }
        float3 linear709=mul(float3x3(1.660491,-0.587641,-0.072850,-0.124550,1.132900,-0.008349,-0.018151,-0.100579,1.118730),nits);
        // Custom: per-scene HDR brightness management, HDR-output path only. The
        // graph measures the scene from the luma histogram it already keeps and
        // packs the map into the two spare toneMapParams lanes: z holds the gain in
        // 1/4096 units in its low half and the source peak in nits in its high
        // half, w holds the target peak in nits in its high half and 1 in the low
        // half while the map is live. Packed because this pass already uses 60 of
        // the 64 root constants D3D12 allows next to its three descriptor tables.
        if((yuvDimensions.z&1)!=0){
            const uint packedGain=asuint(toneMapParams.z),packedPeak=asuint(toneMapParams.w);
            if((packedPeak&0xFFFFu)==1u){
                const float sceneGain=float(packedGain&0xFFFFu)/4096.0;
                const float sceneTargetPeak=float(packedPeak>>16);
                const float sceneSourcePeak=max(float(packedGain>>16),sceneTargetPeak*1.001);
                const float3 gained=linear709*sceneGain;
                const float sceneY=dot(gained,float3(.212639,.715169,.072192));
                // Reuse the verified BT.2390 EETF. ToneLuminance returns the mapped
                // luminance normalised to the target peak, so scaling it back by the
                // target keeps absolute nits for the scRGB working space.
                const float mapped=ToneLuminance(max(sceneY,0.0),sceneSourcePeak,sceneTargetPeak)*sceneTargetPeak;
                linear709=gained*clamp(mapped/max(sceneY,0.05),0.0,4.0);
            }
        }
        // Grade in the normalised domain: HDR is referenced to the BT.2408
        // 203 cd/m2 SDR reference white; SDR paths below are already 0..1.
        if(colorFlags.x>0.5){ColorGradeParams grade={colorRow0,colorRow1,colorRow2,colorControls,colorFlags};linear709=ColorGradeApplyWithFlags(linear709/203.0,grade)*203.0;}
        if((yuvDimensions.z&1)!=0)rgb=linear709/80.0; // scRGB: 1.0 = 80 nits.
        else{
            rgb=HdrToSdr(linear709,toneMapParams.x,toneMapParams.y);
        }
    } else if (colorParams0.z > 2.5) {
        rgb = pow(rgb, 2.4); // BT.1886 EOTF, ideal black SDR display intent.
    } else if (colorParams0.z > 1.5) {
        rgb = float3(rgb.r < 0.081 ? rgb.r / 4.5 : pow((rgb.r + 0.099) / 1.099, 1.0/0.45),
                     rgb.g < 0.081 ? rgb.g / 4.5 : pow((rgb.g + 0.099) / 1.099, 1.0/0.45),
                     rgb.b < 0.081 ? rgb.b / 4.5 : pow((rgb.b + 0.099) / 1.099, 1.0/0.45));
    } else if (colorParams0.z > 0.5) {
        rgb = float3(SrgbDecode(rgb.r), SrgbDecode(rgb.g), SrgbDecode(rgb.b));
    }
    if(colorParams0.z<3.5&&(yuvDimensions.z&2)!=0)rgb=HdrTo709(rgb);
    // SDR paths carry normalised scene-linear RGB here (after any primaries
    // conversion), which is exactly the domain ColorGradeApply expects.
    if(colorParams0.z<3.5&&colorFlags.x>0.5){ColorGradeParams grade={colorRow0,colorRow1,colorRow2,colorControls,colorFlags};rgb=ColorGradeApplyWithFlags(rgb,grade);}
    linearRgb[dispatchThreadId.xy] = float4(rgb, 1.0);
}
