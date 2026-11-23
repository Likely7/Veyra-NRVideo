// Only the frame-stat panel's scissor is shaded. Each packed glyph uses
// 15 bits, one per cell of its 3x5 bitmap. The remaining 1 bit stays zero.
cbuffer FrameStats : register(b0) {
    uint2 origin;
    uint scale;
    float ink;
    uint4 glyphs[3];
};

float4 vsMain(uint vertex : SV_VertexID) : SV_Position {
    const float2 corners[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(corners[vertex], 0, 1);
}

float4 psMain(float4 position : SV_Position) : SV_Target {
    const uint2 cell = (uint2(position.xy) - origin) / scale;
    const uint textLine = cell.y >= 10 ? 1 : 0;
    const uint row = cell.y - (textLine != 0 ? 10 : 3);
    const uint textX = cell.x - 3;
    const uint index = textX / 4;
    const uint column = textX % 4;
    float value = 0;
    if (row < 5 && column < 3 && index < (textLine != 0 ? 13u : 10u)) {
        const uint slot = index + textLine * 10;
        const uint word = glyphs[slot / 8][(slot / 2) % 4];
        const uint bit = (word >> ((slot % 2) * 16 + row * 3 + 2 - column)) & 1;
        value = bit ? ink : 0;
    }
    return float4(value, value, value, 1);
}
