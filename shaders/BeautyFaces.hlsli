// Tracked faces shared by every beauty node of one graph. BeautyTrack.hlsl owns the
// buffer; the node passes read it. Entry 0 is the header, entries 1..8 the faces.
#ifndef VEYRA_BEAUTY_FACES_HLSLI
#define VEYRA_BEAUTY_FACES_HLSLI
struct BeautyFace {
    float4 box;     // centre x, centre y, half width, half height: fractions of the source frame
    float4 shape;   // cos roll, sin roll (eye line), recent peak detector score, presence 0..1
    float4 motion;  // filtered speed x, y (fractions per ms), ms since the last detection match, half-width speed
    uint4 meta;     // header: count, next id, frame counter, raw candidates;
                    // face: id, hits, filtered centre x, y at the last match (float bits)
};
static const uint kBeautyMaxFaces = 8u;

// Face outlines from the face mesh (FaceMeshGpu, BeautyMeshDecode.hlsl): one block of
// kShapeEntries float2 per face slot, in source-frame fractions, read by the node passes.
//   0: (1 when the face has a usable mesh, the mesh's face score); 2-3: contour box
//   min / max; 4-5: box of the eyes, brows and lips; kShapeContour..: the closed outlines.
// A face without a usable mesh keeps the oval from its box.
static const uint kShapeEntries = 128u;
static const uint kShapeContour = 8u, kShapeContourCount = 36u;
static const uint kShapeLips = 44u, kShapeLipsCount = 20u;
static const uint kShapeLeftEye = 64u, kShapeRightEye = 80u, kShapeEyeCount = 16u;
static const uint kShapeLeftBrow = 96u, kShapeRightBrow = 106u, kShapeBrowCount = 10u;
static const uint kShapeOutlinePoints = 108u;
#endif
