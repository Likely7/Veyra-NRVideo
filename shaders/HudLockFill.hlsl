// SPDX-License-Identifier: GPL-3.0-or-later
// Close short gaps between confirmed cells on the same rail axis. The mask
// cannot grow past the last confirmed cell in either direction.
Texture2D<float> candidates : register(t0);
RWTexture2D<float> protection : register(u0);
cbuffer Constants : register(b0) {
    uint maskWidth, maskHeight, reset, pad;
};

int seekCell(int2 p, int2 step, float orientation, int count) {
    const int2 limits = int2(maskWidth, maskHeight);
    [loop] for (int i = 1; i <= count; ++i) {
        const int2 q = p + i * step;
        if (any(q < 0) || any(q >= limits)) break;
        const float value = candidates[q];
        if (value > 0.25) return abs(value - orientation) < 0.1 ? i : 0;
    }
    return 0;
}

bool shortGap(int2 p, int2 axis, float orientation) {
    const int first = seekCell(p, -axis, orientation, 4);
    return first != 0 && seekCell(p, axis, orientation, 5 - first) != 0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= maskWidth || id.y >= maskHeight) return;
    if (reset != 0) {
        protection[id.xy] = 0.0;
        return;
    }
    protection[id.xy] = candidates[id.xy] > 0.25 ||
                        shortGap(int2(id.xy), int2(1, 0), 1.0) ||
                        shortGap(int2(id.xy), int2(0, 1), 0.5) ? 1.0 : 0.0;
}
