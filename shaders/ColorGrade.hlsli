// Colour grade core (plan v4). It runs inside the existing ingest dispatches, so
// there is no extra pass and no extra full-frame read/write: the source is
// decoded to linear and graded by the same thread that writes the working image.
//
// Tables, staged by EnhanceGraph in a second SRV table (register base 8):
//   t8  colorCurve : 1024x1 RGBA32F - log-domain input -> per-channel response
//   t9  colorHue   :  256x1 RGBA32F - hue -> (shift deg, saturation, luminance)
//  t10  colorLum   :  256x1 RGBA32F - tone -> (r,g,b) gains (grading + tint)
//  t11  colorLut   : NxNxN RGBA32F  - optional .cube LUT, tetrahedral
//
// The grade works in normalised scene-linear RGB (1.0 = SDR reference white;
// HDR sources are normalised by the BT.2408 203-nit reference before the call
// and scaled back afterwards), and tonal work happens in a Cineon-style log
// domain because that is where the curve table is indexed.
#pragma once

Texture2D<float4> colorCurve : register(t8);
Texture2D<float4> colorHue : register(t9);
Texture2D<float4> colorLum : register(t10);
Texture3D<float4> colorLut : register(t11);

// 20 root constants (five float4s), written by
// veyra::pipeline::packColorGradeConstants.
struct ColorGradeParams
{
    float4 row0;      // linear 3x3, row 0 (xyz used)
    float4 row1;
    float4 row2;
    float4 controls;  // x=exposure(EV) y=saturation z=vibrance w=lutStrength
    float4 flags;     // x=enabled (0/1) y=lutInputSpace
};

static const uint kColorCurveEntries = 1024;
static const uint kColorHueEntries = 256;
static const uint kColorLumEntries = 256;
static const float kColorMidGrey = 0.18;
static const float kColorLogStops = 10.0;

// "linear" is an interpolation modifier in HLSL, so value parameters are named
// lin throughout this include.
float3 ColorGradeEncodeLog(float3 lin)
{
    return log2(max(lin, 1e-5) / kColorMidGrey) / kColorLogStops + 0.5;
}
float3 ColorGradeDecodeLog(float3 encoded)
{
    return kColorMidGrey * exp2((encoded - 0.5) * kColorLogStops);
}
// Bilinear table reads: the compute passes carry no sampler in their root
// signature, and a table lookup is exactly what is needed here.
float4 ColorGradeCurve(float t)
{
    const float x = saturate(t) * float(kColorCurveEntries - 1);
    const uint i0 = uint(floor(x)), i1 = min(i0 + 1, kColorCurveEntries - 1);
    return lerp(colorCurve.Load(int3(int(i0), 0, 0)), colorCurve.Load(int3(int(i1), 0, 0)), x - float(i0));
}
float4 ColorGradeHue(float t)
{
    const float x = saturate(t) * float(kColorHueEntries - 1);
    const uint i0 = uint(floor(x)), i1 = min(i0 + 1, kColorHueEntries - 1);
    return lerp(colorHue.Load(int3(int(i0), 0, 0)), colorHue.Load(int3(int(i1), 0, 0)), x - float(i0));
}
float4 ColorGradeLum(float t)
{
    const float x = saturate(t) * float(kColorLumEntries - 1);
    const uint i0 = uint(floor(x)), i1 = min(i0 + 1, kColorLumEntries - 1);
    return lerp(colorLum.Load(int3(int(i0), 0, 0)), colorLum.Load(int3(int(i1), 0, 0)), x - float(i0));
}
float3 ColorGradeSrgbEncode(float3 lin)
{
    const float3 low = lin * 12.92;
    const float3 high = 1.055 * pow(max(lin, 1e-6), 1.0 / 2.4) - 0.055;
    return select(lin <= 0.0031308, low, high);
}
float3 ColorGradeSrgbDecode(float3 signal)
{
    const float3 low = signal / 12.92;
    const float3 high = pow(max((signal + 0.055) / 1.055, 0.0), 2.4);
    return select(signal <= 0.04045, low, high);
}
float3 ColorGradePqEncode(float3 lin)
{
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    const float3 p = pow(max(lin, 0.0), m1);
    return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}
float3 ColorGradePqDecode(float3 signal)
{
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    const float3 p = pow(max(signal, 0.0), 1.0 / m2);
    return pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1);
}
float3 RgbToHsv(float3 c)
{
    const float maxc = max(c.r, max(c.g, c.b));
    const float minc = min(c.r, min(c.g, c.b));
    const float delta = maxc - minc;
    float hue = 0.0;
    if (delta > 1e-6)
    {
        if (maxc == c.r) hue = 60.0 * fmod((c.g - c.b) / delta + 6.0, 6.0);
        else if (maxc == c.g) hue = 60.0 * ((c.b - c.r) / delta + 2.0);
        else hue = 60.0 * ((c.r - c.g) / delta + 4.0);
    }
    return float3(hue, delta / max(maxc, 1e-5), maxc);
}
float3 HsvToRgb(float3 hsv)
{
    const float h = frac(hsv.x / 360.0) * 6.0;
    const float c = hsv.z * hsv.y;
    const float x = c * (1.0 - abs(fmod(h, 2.0) - 1.0));
    const float m = hsv.z - c;
    float3 rgb;
    if (h < 1.0) rgb = float3(c, x, 0);
    else if (h < 2.0) rgb = float3(x, c, 0);
    else if (h < 3.0) rgb = float3(0, c, x);
    else if (h < 4.0) rgb = float3(0, x, c);
    else if (h < 5.0) rgb = float3(x, 0, c);
    else rgb = float3(c, 0, x);
    return rgb + m;
}
float ColorGradeLuma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}
float3 ColorGradeLutSample(float3 uvw)
{
    uint3 dims;
    colorLut.GetDimensions(dims.x, dims.y, dims.z);
    const float3 coord = saturate(uvw) * float3(dims - 1u);
    // Clamp the base so the "+1" corners stay inside: at coord == dims-1 a
    // naive floor would fetch index dims and read out of bounds.
    const int3 base = int3(min(floor(coord), float3(dims - 2u)));
    const float3 f = coord - float3(base);
    const float3 c000 = colorLut.Load(int4(base + int3(0, 0, 0), 0)).rgb;
    const float3 c100 = colorLut.Load(int4(base + int3(1, 0, 0), 0)).rgb;
    const float3 c010 = colorLut.Load(int4(base + int3(0, 1, 0), 0)).rgb;
    const float3 c001 = colorLut.Load(int4(base + int3(0, 0, 1), 0)).rgb;
    const float3 c110 = colorLut.Load(int4(base + int3(1, 1, 0), 0)).rgb;
    const float3 c101 = colorLut.Load(int4(base + int3(1, 0, 1), 0)).rgb;
    const float3 c011 = colorLut.Load(int4(base + int3(0, 1, 1), 0)).rgb;
    const float3 c111 = colorLut.Load(int4(base + int3(1, 1, 1), 0)).rgb;
    // Six-case tetrahedral interpolation: four fetches per pixel, no trilinear
    // diagonal banding on steep LUTs.
    if (f.x >= f.y)
    {
        if (f.y >= f.z) return c000 + f.x * (c100 - c000) + f.y * (c110 - c100) + f.z * (c111 - c110);
        if (f.x >= f.z) return c000 + f.x * (c100 - c000) + f.z * (c101 - c100) + f.y * (c111 - c101);
        return c000 + f.z * (c001 - c000) + f.x * (c101 - c001) + f.y * (c111 - c101);
    }
    if (f.y >= f.z)
    {
        if (f.x >= f.z) return c000 + f.y * (c010 - c000) + f.x * (c110 - c010) + f.z * (c111 - c110);
        return c000 + f.y * (c010 - c000) + f.z * (c011 - c010) + f.x * (c111 - c011);
    }
    return c000 + f.z * (c001 - c000) + f.y * (c011 - c001) + f.x * (c111 - c011);
}
// Single entry point: every ingest shader calls this right before writing its
// linear RGB. Values outside the 10-stop table window pass through untouched so
// HDR highlights are never clipped by a tone table.
float3 ColorGradeApply(float3 lin, ColorGradeParams p)
{
    if (p.flags.x < 0.5) return lin;
    const float3x3 matrix = float3x3(p.row0.xyz, p.row1.xyz, p.row2.xyz);
    const float exposure = p.controls.x, saturation = p.controls.y, vibrance = p.controls.z, lutStrength = p.controls.w;
    const int lutInputSpace = int(p.flags.y + 0.5);
    float3 rgb = mul(matrix, lin) * exp2(exposure);

    // Tone response: per-channel table lookup in the log domain.
    const float3 encoded = ColorGradeEncodeLog(rgb);
    const float3 tableOut = float3(
        ColorGradeCurve(encoded.r).r,
        ColorGradeCurve(encoded.g).g,
        ColorGradeCurve(encoded.b).b);
    const float3 graded = ColorGradeDecodeLog(saturate(tableOut));
    rgb = select((encoded < 0.0) | (encoded > 1.0), rgb, graded);

    // Hue mixer. The bands must be matched against the *display-referred* colour
    // the user sees: skin sits at ~30 degrees (orange) in an encoded HSV, but
    // linear-light HSV pulls skin towards red, which made the red band grab
    // faces. Encode with a monotone gamma (no clipping, so HDR highlights are
    // safe), mix there, decode back to the linear working image.
    // scRGB contains legitimate negative components for wide-gamut HDR.
    // A signed gamma round trip retains them even with neutral grading.
    const float3 mixEncoded = sign(rgb) * pow(abs(rgb), 1.0 / 2.2);
    const float3 hsv = RgbToHsv(mixEncoded);
    const float4 hueResponse = ColorGradeHue(hsv.x / 360.0);
    if (p.flags.z > 0.5)
    {
        // Black & white mixer: the frame becomes monochrome and each colour band
        // lightens or darkens its own grey by up to +/-100% (Lightroom's B&W
        // mixer semantics). The per-band weight rides in hueResponse.a.
        const float luma = dot(mixEncoded, float3(0.2126, 0.7152, 0.0722));
        const float grey = max(0.0, luma * max(0.0, 1.0 + hueResponse.a * 0.01));
        rgb = pow(float3(grey, grey, grey), 2.2);
    }
    else
    {
        const float saturationLimit = max(1.0, hsv.y);
        const float3 mixed = HsvToRgb(float3(hsv.x + hueResponse.r, clamp(hsv.y * hueResponse.g, 0.0, saturationLimit), max(hsv.z * hueResponse.b, 0.0)));
        rgb = sign(mixed) * pow(abs(mixed), 2.2);
    }

    // Luminance-zone grading + calibration shadow tint.
    const float tone = saturate(ColorGradeEncodeLog(rgb).y);
    rgb *= ColorGradeLum(tone).rgb;

    // Saturation and vibrance: vibrance scales with how unsaturated the pixel
    // already is, so skin tones move less than primary colours.
    const float luma = ColorGradeLuma(rgb);
    const float maxc = max(rgb.r, max(rgb.g, rgb.b));
    const float minc = min(rgb.r, min(rgb.g, rgb.b));
    const float currentSat = saturate((maxc - minc) / max(maxc, 1e-5));
    const float scale = max(1.0 + saturation / 100.0 + vibrance / 100.0 * (1.0 - currentSat), 0.0);
    rgb = lerp(luma.xxx, rgb, scale);

    // Optional .cube LUT in its declared input space (importer lands in T5).
    if (lutStrength > 0.0)
    {
        const float3 safe = max(rgb, 0.0);
        float3 lutInput;
        if (lutInputSpace == 1) lutInput = ColorGradeSrgbEncode(min(safe, 1.0));
        else if (lutInputSpace == 2) lutInput = ColorGradePqEncode(safe);
        else lutInput = saturate(ColorGradeEncodeLog(safe));
        const float3 lutOutput = ColorGradeLutSample(lutInput);
        float3 decoded;
        if (lutInputSpace == 1) decoded = ColorGradeSrgbDecode(lutOutput);
        else if (lutInputSpace == 2) decoded = ColorGradePqDecode(lutOutput);
        else decoded = ColorGradeDecodeLog(lutOutput);
        rgb = lerp(rgb, decoded, saturate(lutStrength));
    }
    return rgb;
}

// Keep the fused ingress path bit-for-bit aligned with ColorGradePass for a
// pure exposure grade. The independent pass uses the same fast path because
// neutral log/HSV/LUT work can otherwise move HDR scRGB values by a visible
// amount before the FP16 store.
float3 ColorGradeApplyWithFlags(float3 lin, ColorGradeParams p)
{
    if (p.flags.w > 0.5) return lin * exp2(p.controls.x);
    return ColorGradeApply(lin, p);
}
