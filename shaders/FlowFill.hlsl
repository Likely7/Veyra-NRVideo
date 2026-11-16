// Disocclusion fill for frame-generation motion (after FlowRefine). A pixel whose best
// candidate still does not match the previous frame has no correspondence there: usually
// background that a moving object has just uncovered. Zero or a wrong vector makes the
// interpolator drag that background with the object (the "jelly" around characters).
// Take the vector of the most similar-coloured confident pixel nearby instead; uncovered
// background resembles the background beside it, which moves with the background.
// With backward flow a pixel whose best candidate fails the forward-backward check has no
// correspondence either. Only pixels that pass it clearly lend their vector: next to an
// object NVOF's backward vectors lean towards the object too, so a few uncovered pixels
// pass narrowly and must not spread the object's vector over the background beside them.
// FidelityFX flow brings the same disagreement from FlowClaim instead.
Texture2D<float4> refined : register(t0);        // xy vector, z cost, w disagreement (FlowRefine)
Texture2D<float4> currentColor : register(t1);   // flow extent
Texture2D<float> claimed : register(t2);         // FlowClaim disagreement (FidelityFX flow)
RWTexture2D<float2> output : register(u0);       // flow extent, current->previous pixels

cbuffer FlowFillConstants : register(b0)
{
    uint flowW;
    uint flowH;
    float threshold;      // best cost above this has no correspondence
    float distanceWeight; // colour distance added per pixel of search distance
    float consistencyThreshold; // disagreement above this (flow pixels) has none; 0 = unused
    float sourceThreshold;      // disagreement up to this lends its vector
    uint fromClaims;            // 1: disagreement from FlowClaim, else FlowRefine's w
    uint unused3;
};

static const int2 kDirections[8] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1), int2(1, 1), int2(-1, 1), int2(1, -1), int2(-1, -1) };
static const int kRadii[9] = { 2, 4, 8, 12, 16, 24, 32, 48, 64 };

float disagreement(int2 q, float4 r) { return fromClaims != 0 ? claimed[q] : r.w; }
bool confident(int2 q, float4 r) { return r.z <= threshold && (consistencyThreshold <= 0 || disagreement(q, r) <= sourceThreshold); }
bool filled(int2 q, float4 r) { return r.z > threshold || (consistencyThreshold > 0 && disagreement(q, r) > consistencyThreshold); }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= flowW || id.y >= flowH) return;
    const int2 p = int2(id.xy);
    const float4 own = refined[p];
    if (!filled(p, own)) { output[p] = own.xy; return; }
    const float3 colour = currentColor[p].rgb;
    float bestScore = 1e9;
    float2 vector = own.xy;
    [loop] for (int r = 0; r < 9; ++r) {
        const float distancePenalty = distanceWeight * kRadii[r];
        // Colour distance is non-negative and the distance penalty is
        // non-decreasing for the configured non-negative weight. Once the
        // best score reaches this lower bound, no direction at this radius
        // or any larger radius can improve it.
        if (distanceWeight >= 0.0 && bestScore <= distancePenalty) break;
        [unroll] for (int d = 0; d < 8; ++d) {
            const int2 q = p + kDirections[d] * kRadii[r];
            if (any(q < 0) || q.x >= int(flowW) || q.y >= int(flowH)) continue;
            const float4 candidate = refined[q];
            if (!confident(q, candidate)) continue;
            const float3 delta = currentColor[q].rgb - colour;
            const float score = dot(abs(delta), float3(1, 1, 1)) + distancePenalty;
            if (score < bestScore) { bestScore = score; vector = candidate.xy; }
        }
        if (distanceWeight >= 0.0 && bestScore <= distancePenalty) break;
    }
    output[p] = vector;
}
