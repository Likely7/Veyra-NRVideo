// Face mesh input: for every tracked face the 192 x 192 crop the landmark model expects
// (MediaPipe face_landmark: centred on the face, a square `scale` times the face box's
// long side, rotated so the eye line is level) as RGB 0..1 display-referred values,
// what the model saw in its 8-bit training pictures. Bilinear on the encoded values
// with replicated edges, as MediaPipe's ImageToTensor (and the reference script's
// OpenCV warpAffine with BORDER_REPLICATE) do. The crop's centre, side and axis go to
// the scratch buffer for the decode.
//   inW / inH: source size; inQ: HDR source; wOff: crop scale (float bits);
//   outOff: the input tensor; resOff: crop parameters (two float4 per face)
#include "BeautyMesh.hlsli"
#include "HdrColor.hlsli"

Texture2D<float4> Source : register(t2);

float3 meshDisplay(int2 p) {
    const float3 c = Source.Load(int3(clamp(p, int2(0, 0), int2(inW, inH) - 1), 0)).rgb;
    const float3 v = saturate(inQ != 0u ? HdrProxy(c) : c);
    return select(v <= 0.0031308, v * 12.92, 1.055 * pow(v, 1.0 / 2.4) - 0.055);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint face = id.z;
    if (face >= meshFaces() || id.x >= outW * outH) return;
    const uint u = id.x % outW, v = id.x / outW;
    const BeautyFace f = Faces[1 + face];
    const float2 size = float2(inW, inH);
    const float2 centre = f.box.xy * size;
    const float side = asfloat(wOff) * max(2.0 * f.box.z * size.x, 2.0 * f.box.w * size.y);
    const float2 axis = f.shape.xy;
    const float2 c = (float2(u, v) + 0.5 - 0.5 * float2(outW, outH)) * (side / float(outW));
    // Continuous picture position of this crop pixel's centre, then the texel grid (centres at +0.5).
    const float2 p = centre + float2(axis.x * c.x - axis.y * c.y, axis.y * c.x + axis.x * c.y) - 0.5;
    const int2 i0 = int2(floor(p));
    const float2 t = p - float2(i0);
    const float3 rgb = lerp(lerp(meshDisplay(i0), meshDisplay(i0 + int2(1, 0)), t.x),
                            lerp(meshDisplay(i0 + int2(0, 1)), meshDisplay(i0 + int2(1, 1)), t.x), t.y);
    Scratch[tensorAt(outOff, face, u, v, outW, outH, 1u)] = float4(rgb, 0.0);
    if (u == 0u && v == 0u) {
        Scratch[resOff + face * 2u] = float4(centre, side, 0.0);
        Scratch[resOff + face * 2u + 1u] = float4(axis, 0.0, 0.0);
    }
}
