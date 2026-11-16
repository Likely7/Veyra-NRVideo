// Frame-generation motion refinement (XeSS, motion source "optical flow").
// Optical flow returns one current->previous vector per grid cell, so a cell that straddles
// a moving character and the background moves both with one vector. Per flow pixel, pick
// the vector of the 3x3 neighbouring cells whose 3x3 luma patch matches the previous
// frame best: motion boundaries follow the picture instead of the grid. Unlike
// NvofDensify nothing is zeroed; the best cost is kept for FlowFill to find pixels that
// have no correspondence at all (background uncovered by a moving object).
// With NVOF's backward output (previous->current) the chosen vector is also checked for
// forward-backward agreement: the previous-frame point it lands on must flow back to this
// pixel. Uncovered background has no vector that does - its true vector lands on the object
// that covered it, the object's vector on background that moved on - even where a wrong
// vector matches the smooth background beside it photometrically. The choice itself stays
// photometric: on rotating limbs and repetitive texture the check is too noisy to rank by.
// FidelityFX optical flow has no backward output; FlowClaim makes the same check for it.
Texture2D<int2> rawFlow : register(t0);          // grid extent: NVOF S10.5 or FidelityFX integer pixels
Texture2D<float4> previousColor : register(t1);  // flow extent
Texture2D<float4> currentColor : register(t2);   // flow extent
Texture2D<int2> reverseFlow : register(t3);      // grid extent, S10.5, previous->current (NVOF)
Texture2D<uint> sceneChanges : register(t4);     // FidelityFX scene-change history at (1,0)
RWTexture2D<float4> refined : register(u0);      // xy vector, z best cost, w forward-backward disagreement

cbuffer FlowRefineConstants : register(b0)
{
    uint gridW;
    uint gridH;
    uint flowW;
    uint flowH;
    uint gridSize;
    float tieBias;   // added to every neighbour cell's cost: the own cell wins ties
    uint flags;      // 1 reverseFlow holds NVOF's backward output (else w = 0); 2 FidelityFX
                     // vectors; 4 FidelityFX still warming up
    uint unused0;
};

float luma(float3 c) { return dot(c, float3(.2126, .7152, .0722)); }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= flowW || id.y >= flowH) return;
    const int2 p = int2(id.xy);
    const bool fidelity = (flags & 2u) != 0;
    // The frames NvofDensify zeroes for FidelityFX (warm-up and its four-frame cut history)
    // carry no motion here either.
    if ((flags & 4u) != 0 || (fidelity && (sceneChanges[uint2(1, 0)] & 15u) != 0)) { refined[p] = 0; return; }
    const float scale = fidelity ? 1.0 : 1.0 / 32.0;
    const int2 limit = int2(flowW - 1, flowH - 1);
    float current[9];
    [unroll] for (int j = 0; j < 9; ++j)
        current[j] = luma(currentColor[clamp(p + int2(j % 3 - 1, j / 3 - 1), int2(0, 0), limit)].rgb);
    const int2 cell = p / int(gridSize);
    const int2 cellLimit = int2(gridW - 1, gridH - 1);
    float bestCost = 1e9;
    float2 best = 0;
    int bestIndex = 0;
    // The centre flow cell has no tie penalty and is the preferred initial
    // candidate. Evaluate it first to establish a useful lower bound for the
    // neighbouring candidates, while preserving the original k-order tie rule.
    [unroll] for (int order = 0; order < 9; ++order) {
        const int k = order == 0 ? 4 : (order <= 4 ? order - 1 : order);
        const int2 offset = int2(k % 3 - 1, k / 3 - 1);
        const float2 v = float2(rawFlow[clamp(cell + offset, int2(0, 0), cellLimit)]) * scale;
        const int2 q = p + int2(round(v));
        const float penalty = k != 4 ? tieBias : 0.0;
        // Every difference is non-negative. Check the centre first as a cheap
        // lower bound, then the first five samples; either bound can reject a
        // mismatched candidate before the remaining previous-frame reads.
        // Reuse the centre sample and keep the original accumulation order.
        const float centreCost = abs(current[4] - luma(previousColor[clamp(q, int2(0, 0), limit)].rgb));
        if (centreCost / 9.0 + penalty >= bestCost) continue;
        float cost = 0;
        [unroll] for (int j = 0; j < 5; ++j) {
            const float difference = j == 4
                ? centreCost
                : abs(current[j] - luma(previousColor[clamp(q + int2(j % 3 - 1, j / 3 - 1), int2(0, 0), limit)].rgb));
            cost += difference;
        }
        if (cost / 9.0 + penalty >= bestCost) continue;
        [unroll] for (int j = 5; j < 9; ++j)
            cost += abs(current[j] - luma(previousColor[clamp(q + int2(j % 3 - 1, j / 3 - 1), int2(0, 0), limit)].rgb));
        cost /= 9;
        cost += penalty;
        if (cost < bestCost || (cost == bestCost && k < bestIndex)) { bestCost = cost; best = v; bestIndex = k; }
    }
    float disagreement = 0;
    if ((flags & 1u) != 0) {
        const int2 back = clamp(int2(floor((float2(p) + .5 + best) / float(gridSize))), int2(0, 0), cellLimit);
        disagreement = length(best + float2(reverseFlow[back]) / 32.0);
    }
    refined[p] = float4(best, bestCost, disagreement);
}
