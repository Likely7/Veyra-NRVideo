// Smart SR filters in the coded domain the source's own scaler enlarged:
//   0: sRGB codes of linear BT.709 (SDR; Switch 2 TV mode fits at 45.85 dB),
//   1: PQ codes of BT.2020 (HDR10; Switch 2 TV mode fits at 51.1-51.7 dB,
//      linear light does not fit at all).
// Both read and write the graph's linear working values (HDR: scRGB, 1 = 80 nits).
#include "HdrColor.hlsli"

float3 SmartSrEncode(float3 linearValue, uint domain)
{
    if (domain == 1u) {
        return HdrEncodePq(linearValue);
    }
    const float3 c = max(linearValue, 0.0);
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

float3 SmartSrDecode(float3 coded, uint domain)
{
    coded = saturate(coded);
    if (domain == 1u) {
        return HdrDecodePq(coded);
    }
    return select(coded <= 0.04045, coded / 12.92, pow((coded + 0.055) / 1.055, 2.4));
}

// Luma of the coded values with the domain's own weights (BT.709 / BT.2020).
float SmartSrLuma(float4 linearValue, uint domain)
{
    const float3 coded = SmartSrEncode(linearValue.rgb, domain);
    return dot(coded, domain == 1u ? float3(0.2627, 0.6780, 0.0593) : float3(0.2126, 0.7152, 0.0722));
}
