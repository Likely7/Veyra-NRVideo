// N1: packed capture ingress. The driver's bytes are uploaded 1:1 into an
// R8G8B8A8_UNORM texture; this shader unpacks BGR24 / RGB555 / RGB565 /
// UYVY / YVYU with the same color contract as RgbToLinear / Yuy2ToLinear.
// packing: 1 BGR24, 2 RGB555, 3 RGB565, 4 UYVY, 5 YVYU.
#include "HdrColor.hlsli"
#include "HdrToSdr.hlsli"
#include "ColorGrade.hlsli"
cbuffer Params : register(b0) {
    uint width; uint height; uint transfer; uint limitedU;
    float limited; float matrix709; float primaries2020; uint packing;
    float hdrWorking; float sourcePeak; float targetPeak; float reserved;
    float4 colorRow0; float4 colorRow1; float4 colorRow2; float4 colorControls; float4 colorFlags;
};
Texture2D<float4> packedBytes : register(t0);
RWTexture2D<float4> linearRgb : register(u0);
float decode(float v) {
    if (transfer == 3) return pow(max(v, 0), 2.4);
    if (transfer == 2) return v < 0.081 ? v / 4.5 : pow((v + 0.099) / 1.099, 1.0 / 0.45);
    if (transfer == 0) return v;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
uint byteAt(uint y, uint index) {
    const float4 t = packedBytes[uint2(index >> 2, y)];
    return uint(round(saturate(t[index & 3]) * 255.0));
}
uint wordAt(uint y, uint index) {
    const uint4 bytes = uint4(round(saturate(packedBytes[uint2(index, y)]) * 255.0));
    return bytes.x | (bytes.y << 8) | (bytes.z << 16) | (bytes.w << 24);
}
[numthreads(16,16,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= width || p.y >= height) return;
    float3 rgb;
#ifdef VEYRA_Y210
    // Y0 U Y1 V, in four little-endian words. Ignore the low six padding bits.
    const uint pair = p.x / 2;
    const uint lo = wordAt(p.y, pair * 2);
    const uint hi = wordAt(p.y, pair * 2 + 1);
    const bool odd = (p.x & 1) != 0;
    const float luma = float(((odd ? hi : lo) >> 6) & 1023);
    float2 cbcr = float2(lo >> 22, hi >> 22);
    const uint chromaLocation = asuint(reserved);
    const bool centered = chromaLocation != 0 && (chromaLocation & 1) == 0;
    if (centered || odd) {
        const int neighbor = clamp(int(pair) + (centered && !odd ? -1 : 1), 0, int(width / 2) - 1);
        const float2 adjacent = float2(wordAt(p.y, neighbor * 2) >> 22, wordAt(p.y, neighbor * 2 + 1) >> 22);
        cbcr = lerp(cbcr, adjacent, centered ? 0.25 : 0.5);
    }
    const float y = saturate(limited > 0.5 ? (luma - 64.0) / 876.0 : luma / 1023.0);
    const float2 uv = (cbcr - 512.0) / (limited > 0.5 && limited < 1.5 ? 896.0 : 1023.0);
    rgb = matrix709 > 1.5 ? float3(y + 1.4746*uv.y, y - .164553*uv.x - .571353*uv.y, y + 1.8814*uv.x)
        : matrix709 > .5 ? float3(y + 1.5748*uv.y, y - .1873*uv.x - .4681*uv.y, y + 1.8556*uv.x)
        : float3(y + 1.402*uv.y, y - .3441*uv.x - .7141*uv.y, y + 1.772*uv.x);
    rgb = saturate(rgb);
#else
    if (packing == 6) {
        // Each of the three 4:2:2 pairs shares two adjacent 32-bit words.
        // Avoid reading the whole group for every pixel.
        const uint pair = (p.x % 6) / 2;
        const uint word = (p.x / 6) * 4 + pair;
        const uint lo = wordAt(p.y, word);
        const uint hi = wordAt(p.y, word + 1);
        const bool odd = (p.x & 1) != 0;
        const uint lumaWord = odd || pair == 2 ? hi : lo;
        const uint lumaShift = odd ? pair * 10 : ((pair + 1) % 3) * 10;
        const float luma = float((lumaWord >> lumaShift) & 1023);
        const uint crWord = pair == 0 ? lo : hi;
        const uint crShift = pair == 0 ? 20 : (pair - 1) * 10;
        float2 cbcr = float2((lo >> (pair * 10)) & 1023, (crWord >> crShift) & 1023);
        // The odd pixel is between two co-sited samples. Across the packing
        // boundary the first word of the next group carries both components.
        if (odd && p.x + 1 < width) {
            const uint next = wordAt(p.y, word + 2);
            const uint nextCb = pair == 2 ? next : (hi >> ((pair + 1) * 10));
            cbcr = 0.5 * (cbcr + float2(nextCb & 1023, (next >> (pair * 10)) & 1023));
        }
        const float2 chromaDelta = cbcr - 512.0;
        float y = luma / 1023.0;
        float2 uv = chromaDelta / 1023.0;
        if (limited > 0.5) { y = (luma - 64.0) / 876.0; uv = chromaDelta / 896.0; }
        rgb=matrix709>1.5?float3(y+1.4746*uv.y,y-.164553*uv.x-.571353*uv.y,y+1.8814*uv.x)
            :matrix709>.5?float3(y+1.5748*uv.y,y-.187324*uv.x-.468124*uv.y,y+1.8556*uv.x)
            :float3(y+1.402*uv.y,y-.344136*uv.x-.714136*uv.y,y+1.772*uv.x);
        rgb=saturate(rgb);
    } else if(packing==7){
        const uint v=wordAt(p.y,p.x);
        rgb=float3((v>>22)&1023,(v>>12)&1023,(v>>2)&1023)/1023.0;
        if(limitedU!=0)rgb=saturate((rgb-64.0/1023.0)*(1023.0/876.0));
    } else if (packing == 4 || packing == 5) {
        const float4 pair = packedBytes[uint2(p.x / 2, p.y)];
        const bool second = (p.x & 1) != 0;
        float y, cb, cr;
        if (packing == 4) { y = second ? pair.w : pair.y; cb = pair.x; cr = pair.z; }
        else { y = second ? pair.z : pair.x; cb = pair.w; cr = pair.y; }
        // Co-sited 4:2:2 chroma: the odd pixel averages its pair's chroma with
        // the next pair's, as in Yuy2ToLinear.hlsl (and OBS), instead of
        // repeating it, so coloured edges do not step in two-pixel blocks.
        if (second) {
            const float4 next = packedBytes[uint2(min(p.x / 2 + 1, width / 2 - 1), p.y)];
            cb = 0.5 * (cb + (packing == 4 ? next.x : next.w));
            cr = 0.5 * (cr + (packing == 4 ? next.z : next.y));
        }
        float2 uv = float2(cb, cr) - 128.0 / 255.0;
        if (limited > 0.5) { y = (y - 16.0 / 255.0) * (255.0 / 219.0); uv *= 255.0 / 224.0; }
        rgb = matrix709 > 0.5 ? float3(y + 1.5748 * uv.y, y - 0.187324 * uv.x - 0.468124 * uv.y, y + 1.8556 * uv.x)
                              : float3(y + 1.402 * uv.y, y - 0.344136 * uv.x - 0.714136 * uv.y, y + 1.772 * uv.x);
        if (matrix709 > 1.5) rgb = float3(y + 1.4746 * uv.y, y - 0.164553 * uv.x - 0.571353 * uv.y, y + 1.8814 * uv.x);
        rgb = saturate(rgb);
    } else if (packing == 1) {
        const uint base = p.x * 3;
        rgb = float3(byteAt(p.y, base + 2), byteAt(p.y, base + 1), byteAt(p.y, base)) / 255.0;
    } else {
        const uint base = p.x * 2;
        const uint v = byteAt(p.y, base) | (byteAt(p.y, base + 1) << 8);
        if (packing == 2) rgb = float3(((v >> 10) & 31) / 31.0, ((v >> 5) & 31) / 31.0, (v & 31) / 31.0);
        else rgb = float3(((v >> 11) & 31) / 31.0, ((v >> 5) & 63) / 63.0, (v & 31) / 31.0);
    }
    if (limitedU != 0 && packing <= 3) rgb = saturate((rgb - 16.0 / 255.0) * (255.0 / 219.0));
#endif
    float3 decoded;
    if(transfer>=4){
        float3 nits;
        if(transfer==4){
            const float3 q=pow(saturate(rgb),32.0/2523.0);
            nits=10000.0*pow(max(q-3424.0/4096.0,0)/max(2413.0/128.0-2392.0/128.0*q,1e-6),16384.0/2610.0);
        }else{
            const float3 scene=select(rgb<=.5,rgb*rgb/3.0,(exp((rgb-.55991073)/.17883277)+.28466892)/12.0);
            nits=1000.0*scene*pow(max(dot(scene,float3(.2627,.6780,.0593)),0),.2);
        }
        float3 linear709=primaries2020>.5?HdrTo709(nits):nits;
        if(colorFlags.x>.5){ColorGradeParams grade={colorRow0,colorRow1,colorRow2,colorControls,colorFlags};linear709=ColorGradeApplyWithFlags(linear709/203.0,grade)*203.0;}
        decoded=hdrWorking>.5?linear709/80.0:HdrToSdr(linear709,sourcePeak,targetPeak);
    }else{
        decoded=float3(decode(rgb.r),decode(rgb.g),decode(rgb.b));
        if(primaries2020>.5)decoded=HdrTo709(decoded);
        if(colorFlags.x>.5){ColorGradeParams grade={colorRow0,colorRow1,colorRow2,colorControls,colorFlags};decoded=ColorGradeApplyWithFlags(decoded,grade);}
    }
    linearRgb[p.xy] = float4(decoded, 1);
}
