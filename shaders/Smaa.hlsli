// SMAA for Veyra's anti-aliasing effect (#53).
// Ported from SAOG0721/Magpie d687997 src/Effects/SMAA/SMAA.hlsli (GPL-3.0),
// itself ported from https://github.com/iryoku/smaa. Magpie's changes: vertex
// shader work moved into the pixel shader; predication / temporal
// supersampling / cross-platform code removed; SMAA_LINEAR_SAMPLER and
// SMAA_POINT_SAMPLER; no discard in edge detection.
// Veyra changes: luma edge detection lives in AaSmaaEdges.hlsl and reads the
// perceptual luma texture (AntiAlias.hlsli); neighbourhood blending takes the
// pixel position and passes pixels without blending weights through with an
// exact Load; the long integration guide of the original header is omitted
// (see http://www.iryoku.com/smaa/). The blending weight code is unchanged.

/**
 * Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
 * Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
 * Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
 * Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
 * Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software. As clarification, there
 * is no requirement that the copyright notice and permission be included in
 * binary distributions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef VEYRA_SMAA_HLSLI
#define VEYRA_SMAA_HLSLI

//-----------------------------------------------------------------------------
// SMAA Presets

#if defined(SMAA_PRESET_LOW)
#define SMAA_THRESHOLD 0.15
#define SMAA_MAX_SEARCH_STEPS 4
#define SMAA_DISABLE_DIAG_DETECTION
#define SMAA_DISABLE_CORNER_DETECTION
#elif defined(SMAA_PRESET_MEDIUM)
#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 8
#define SMAA_DISABLE_DIAG_DETECTION
#define SMAA_DISABLE_CORNER_DETECTION
#elif defined(SMAA_PRESET_HIGH)
#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_CORNER_ROUNDING 25
#elif defined(SMAA_PRESET_ULTRA)
#define SMAA_THRESHOLD 0.05
#define SMAA_MAX_SEARCH_STEPS 32
#define SMAA_MAX_SEARCH_STEPS_DIAG 16
#define SMAA_CORNER_ROUNDING 25
#endif

//-----------------------------------------------------------------------------
// Configurable Defines

#ifndef SMAA_THRESHOLD
#define SMAA_THRESHOLD 0.1
#endif
#ifndef SMAA_MAX_SEARCH_STEPS
#define SMAA_MAX_SEARCH_STEPS 16
#endif
#ifndef SMAA_MAX_SEARCH_STEPS_DIAG
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#endif
#ifndef SMAA_CORNER_ROUNDING
#define SMAA_CORNER_ROUNDING 25
#endif
#ifndef SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
#endif

//-----------------------------------------------------------------------------
// Texture Access Defines

#ifndef SMAA_AREATEX_SELECT
#define SMAA_AREATEX_SELECT(sample) sample.rg
#endif
#ifndef SMAA_SEARCHTEX_SELECT
#define SMAA_SEARCHTEX_SELECT(sample) sample.r
#endif

//-----------------------------------------------------------------------------
// Non-Configurable Defines

#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / float2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE float2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE float2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)

//-----------------------------------------------------------------------------
// Porting Functions

#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(SMAA_POINT_SAMPLER, coord, 0)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]

//-----------------------------------------------------------------------------
// Misc functions

/**
 * Conditional move:
 */
void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    SMAA_FLATTEN if (cond.x) variable.x = value.x;
    SMAA_FLATTEN if (cond.y) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

//-----------------------------------------------------------------------------
// Diagonal Search Functions

#if !defined(SMAA_DISABLE_DIAG_DETECTION)

/**
 * Allows to decode two binary values from a bilinear-filtered access.
 */
float2 SMAADecodeDiagBilinearAccess(float2 e) {
    // Bilinear access for fetching 'e' have a 0.25 offset, and we are
    // interested in the R and G edges:
    //
    // +---G---+-------+
    // |   x o R   x   |
    // +-------+-------+
    //
    // Then, if one of these edge is enabled:
    //   Red:   (0.75 * X + 0.25 * 1) => 0.25 or 1.0
    //   Green: (0.75 * 1 + 0.25 * X) => 0.75 or 1.0
    //
    // This function will unpack the values (mad + mul + round):
    // wolframalpha.com: round(x * abs(5 * x - 5 * 0.75)) plot 0 to 1
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

float4 SMAADecodeDiagBilinearAccess(float4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

/**
 * These functions allows to perform diagonal pattern searches.
 */
float2 SMAASearchDiag1(Texture2D<float2> edgesTex, float2 texcoord, float2 dir, out float2 e) {
    float4 coord = float4(texcoord, -1.0, 1.0);
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    e = float2(0.0, 0.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) &&
        coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = SMAASampleLevelZero(edgesTex, coord.xy).rg;
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAASearchDiag2(Texture2D<float2> edgesTex, float2 texcoord, float2 dir, out float2 e) {
    float4 coord = float4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * SMAA_RT_METRICS.x; // See @SearchDiag2Optimization
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    e = float2(0.0, 0.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) &&
        coord.w > 0.9) {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);

        // @SearchDiag2Optimization
        // Fetch both edges at once using bilinear filtering:
        e = SMAASampleLevelZero(edgesTex, coord.xy).rg;
        e = SMAADecodeDiagBilinearAccess(e);

        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

/**
 * Similar to SMAAArea, this calculates the area corresponding to a certain
 * diagonal distance and crossing edges 'e'.
 */
float2 SMAAAreaDiag(Texture2D<float4> areaTex, float2 dist, float2 e, float offset) {
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);

    // We do a scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Diagonal areas are on the second half of the texture:
    texcoord.x += 0.5;

    // Move to proper place, according to the subpixel offset:
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;

    // Do it!
    return SMAA_AREATEX_SELECT(SMAASampleLevelZero(areaTex, texcoord));
}

/**
 * This searches for diagonal patterns and returns the corresponding weights.
 */
float2 SMAACalculateDiagWeights(Texture2D<float2> edgesTex, Texture2D<float4> areaTex, float2 texcoord, float2 e, float4 subsampleIndices) {
    float2 weights = float2(0.0, 0.0);

    // Search for the line ends:
    float4 d;
    float2 end;
    if (e.r > 0.0) {
        d.xz = SMAASearchDiag1(SMAATexturePass2D(edgesTex), texcoord, float2(-1.0, 1.0), end);
        d.x += float(end.y > 0.9);
    } else
        d.xz = float2(0.0, 0.0);
    d.yw = SMAASearchDiag1(SMAATexturePass2D(edgesTex), texcoord, float2(1.0, -1.0), end);

    SMAA_BRANCH
        if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
            // Fetch the crossing edges:
            float4 coords = mad(float4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
            float4 c;
            c.xy = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(-1, 0)).rg;
            c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, int2(1, 0)).rg;
            c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

            // Merge crossing edges at each side into a single value:
            float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

            // Remove the crossing edge if we didn't found the end of the line:
            SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

            // Fetch the areas for this line:
            weights += SMAAAreaDiag(SMAATexturePass2D(areaTex), d.xy, cc, subsampleIndices.z);
        }

    // Search for the line ends:
    d.xz = SMAASearchDiag2(SMAATexturePass2D(edgesTex), texcoord, float2(-1.0, -1.0), end);
    if (SMAASampleLevelZeroOffset(edgesTex, texcoord, int2(1, 0)).r > 0.0) {
        d.yw = SMAASearchDiag2(SMAATexturePass2D(edgesTex), texcoord, float2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else
        d.yw = float2(0.0, 0.0);

    SMAA_BRANCH
        if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
            // Fetch the crossing edges:
            float4 coords = mad(float4(-d.x, -d.x, d.y, d.y), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
            float4 c;
            c.x = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(-1, 0)).g;
            c.y = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(0, -1)).r;
            c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, int2(1, 0)).gr;
            float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

            // Remove the crossing edge if we didn't found the end of the line:
            SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

            // Fetch the areas for this line:
            weights += SMAAAreaDiag(SMAATexturePass2D(areaTex), d.xy, cc, subsampleIndices.w).gr;
        }

    return weights;
}
#endif

//-----------------------------------------------------------------------------
// Horizontal/Vertical Search Functions

/**
 * This allows to determine how much length should we add in the last step
 * of the searches. It takes the bilinearly interpolated edge (see
 * @PSEUDO_GATHER4), and adds 0, 1 or 2, depending on which edges and
 * crossing edges are active.
 */
float SMAASearchLength(Texture2D<float> searchTex, float2 e, float offset) {
    // The texture is flipped vertically, with left and right cases taking half
    // of the space horizontally:
    float2 scale = SMAA_SEARCHTEX_SIZE * float2(0.5, -1.0);
    float2 bias = SMAA_SEARCHTEX_SIZE * float2(offset, 1.0);

    // Scale and bias to access texel centers:
    scale += float2(-1.0, 1.0);
    bias += float2(0.5, -0.5);

    // Convert from pixel coordinates to texcoords:
    // (We use SMAA_SEARCHTEX_PACKED_SIZE because the texture is cropped)
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    // Lookup the search texture:
    return SMAA_SEARCHTEX_SELECT(SMAASampleLevelZero(searchTex, mad(scale, e, bias)));
}

/**
 * Horizontal/vertical search functions for the 2nd pass.
 */
float SMAASearchXLeft(Texture2D<float2> edgesTex, Texture2D<float> searchTex, float2 texcoord, float end) {
    /**
     * @PSEUDO_GATHER4
     * This texcoord has been offset by (-0.25, -0.125) in the vertex shader to
     * sample between edge, thus fetching four edges in a row.
     * Sampling with different offsets in each direction allows to disambiguate
     * which edges are active from the four fetched ones.
     */
    float2 e = float2(0.0, 1.0);
    while (texcoord.x > end &&
        e.g > 0.8281 && // Is there some edge not activated?
        e.r == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(-float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }

    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(searchTex), e, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchXRight(Texture2D<float2> edgesTex, Texture2D<float> searchTex, float2 texcoord, float end) {
    float2 e = float2(0.0, 1.0);
    while (texcoord.x < end &&
        e.g > 0.8281 && // Is there some edge not activated?
        e.r == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(searchTex), e, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchYUp(Texture2D<float2> edgesTex, Texture2D<float> searchTex, float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    while (texcoord.y > end &&
        e.r > 0.8281 && // Is there some edge not activated?
        e.g == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(-float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(searchTex), e.gr, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.y, offset, texcoord.y);
}

float SMAASearchYDown(Texture2D<float2> edgesTex, Texture2D<float> searchTex, float2 texcoord, float end) {
    float2 e = float2(1.0, 0.0);
    while (texcoord.y < end &&
        e.r > 0.8281 && // Is there some edge not activated?
        e.g == 0.0) { // Or is there a crossing edge that breaks the line?
        e = SMAASampleLevelZero(edgesTex, texcoord).rg;
        texcoord = mad(float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(SMAATexturePass2D(searchTex), e.gr, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.y, offset, texcoord.y);
}

/**
 * Ok, we have the distance and both crossing edges. So, what are the areas
 * at each side of current edge?
 */
float2 SMAAArea(Texture2D<float4> areaTex, float2 dist, float e1, float e2, float offset) {
    // Rounding prevents precision errors of bilinear filtering:
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * float2(e1, e2)), dist);

    // We do a scale and bias for mapping to texel space:
    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    // Move to proper place, according to the subpixel offset:
    texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);

    // Do it!
    return SMAA_AREATEX_SELECT(SMAASampleLevelZero(areaTex, texcoord));
}

//-----------------------------------------------------------------------------
// Corner Detection Functions

void SMAADetectHorizontalCornerPattern(Texture2D<float2> edgesTex, inout float2 weights, float4 texcoord, float2 d) {
#if !defined(SMAA_DISABLE_CORNER_DETECTION)
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y; // Reduce blending for pixels in the center of a line.

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(0, 1)).r;
    factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1, 1)).r;
    factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(0, -2)).r;
    factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1, -2)).r;

    weights *= saturate(factor);
#endif
}

void SMAADetectVerticalCornerPattern(Texture2D<float2> edgesTex, inout float2 weights, float4 texcoord, float2 d) {
#if !defined(SMAA_DISABLE_CORNER_DETECTION)
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(1, 0)).g;
    factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1, 1)).g;
    factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(-2, 0)).g;
    factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(-2, 1)).g;

    weights *= saturate(factor);
#endif
}

//-----------------------------------------------------------------------------
// Blending Weight Calculation (Second Pass)

float4 SMAABlendingWeightCalculationPS(
    float2 texcoord,
    Texture2D<float2> edgesTex,
    Texture2D<float4> areaTex,
    Texture2D<float> searchTex,
    float4 subsampleIndices // Just pass zero for SMAA 1x, see @SUBSAMPLE_INDICES.
) {
    float2 pixcoord = texcoord * SMAA_RT_METRICS.zw;

    float4 offset[3];
    // We will use these offsets for the searches later on (see @PSEUDO_GATHER4):
    offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-0.25, -0.125, 1.25, -0.125), texcoord.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, float4(-0.125, -0.25, -0.125, 1.25), texcoord.xyxy);

    // And these for the searches, they indicate the ends of the loops:
    offset[2] = mad(SMAA_RT_METRICS.xxyy,
        float4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),
        float4(offset[0].xz, offset[1].yw));

    float4 weights = float4(0.0, 0.0, 0.0, 0.0);

    float2 e = SMAASample(edgesTex, texcoord).rg;

    SMAA_BRANCH
        if (e.g > 0.0) { // Edge at north
#if !defined(SMAA_DISABLE_DIAG_DETECTION)
            // Diagonals have both north and west edges, so searching for them in
            // one of the boundaries is enough.
            weights.rg = SMAACalculateDiagWeights(SMAATexturePass2D(edgesTex), SMAATexturePass2D(areaTex), texcoord, e, subsampleIndices);

            // We give priority to diagonals, so if we find a diagonal we skip
            // horizontal/vertical processing.
            SMAA_BRANCH
                if (weights.r == -weights.g) { // weights.r + weights.g == 0.0
#endif

                    float2 d;

                    // Find the distance to the left:
                    float3 coords;
                    coords.x = SMAASearchXLeft(SMAATexturePass2D(edgesTex), SMAATexturePass2D(searchTex), offset[0].xy, offset[2].x);
                    coords.y = offset[1].y; // offset[1].y = texcoord.y - 0.25 * SMAA_RT_METRICS.y (@CROSSING_OFFSET)
                    d.x = coords.x;

                    // Now fetch the left crossing edges, two at a time using bilinear
                    // filtering. Sampling at -0.25 (see @CROSSING_OFFSET) enables to
                    // discern what value each edge has:
                    float e1 = SMAASampleLevelZero(edgesTex, coords.xy).r;

                    // Find the distance to the right:
                    coords.z = SMAASearchXRight(SMAATexturePass2D(edgesTex), SMAATexturePass2D(searchTex), offset[0].zw, offset[2].y);
                    d.y = coords.z;

                    // We want the distances to be in pixel units (doing this here allow to
                    // better interleave arithmetic and memory accesses):
                    d = abs(round(mad(SMAA_RT_METRICS.zz, d, -pixcoord.xx)));

                    // SMAAArea below needs a sqrt, as the areas texture is compressed
                    // quadratically:
                    float2 sqrt_d = sqrt(d);

                    // Fetch the right crossing edges:
                    float e2 = SMAASampleLevelZeroOffset(edgesTex, coords.zy, int2(1, 0)).r;

                    // Ok, we know how this pattern looks like, now it is time for getting
                    // the actual area:
                    weights.rg = SMAAArea(SMAATexturePass2D(areaTex), sqrt_d, e1, e2, subsampleIndices.y);

                    // Fix corners:
                    coords.y = texcoord.y;
                    SMAADetectHorizontalCornerPattern(SMAATexturePass2D(edgesTex), weights.rg, coords.xyzy, d);

#if !defined(SMAA_DISABLE_DIAG_DETECTION)
                } else
                    e.r = 0.0; // Skip vertical processing.
#endif
        }

    SMAA_BRANCH
        if (e.r > 0.0) { // Edge at west
            float2 d;

            // Find the distance to the top:
            float3 coords;
            coords.y = SMAASearchYUp(SMAATexturePass2D(edgesTex), SMAATexturePass2D(searchTex), offset[1].xy, offset[2].z);
            coords.x = offset[0].x; // offset[1].x = texcoord.x - 0.25 * SMAA_RT_METRICS.x;
            d.x = coords.y;

            // Fetch the top crossing edges:
            float e1 = SMAASampleLevelZero(edgesTex, coords.xy).g;

            // Find the distance to the bottom:
            coords.z = SMAASearchYDown(SMAATexturePass2D(edgesTex), SMAATexturePass2D(searchTex), offset[1].zw, offset[2].w);
            d.y = coords.z;

            // We want the distances to be in pixel units:
            d = abs(round(mad(SMAA_RT_METRICS.ww, d, -pixcoord.yy)));

            // SMAAArea below needs a sqrt, as the areas texture is compressed
            // quadratically:
            float2 sqrt_d = sqrt(d);

            // Fetch the bottom crossing edges:
            float e2 = SMAASampleLevelZeroOffset(edgesTex, coords.xz, int2(0, 1)).g;

            // Get the area for this direction:
            weights.ba = SMAAArea(SMAATexturePass2D(areaTex), sqrt_d, e1, e2, subsampleIndices.x);

            // Fix corners:
            coords.x = texcoord.x;
            SMAADetectVerticalCornerPattern(SMAATexturePass2D(edgesTex), weights.ba, coords.xyxz, d);
        }

    return weights;
}

//-----------------------------------------------------------------------------
// Neighborhood Blending (Third Pass)

float4 SMAANeighborhoodBlending(
    uint2 pixel,
    Texture2D<float4> colorTex,
    Texture2D<float4> blendTex
) {
    const float2 texcoord = (float2(pixel) + 0.5) * SMAA_RT_METRICS.xy;
    float4 offset = mad(SMAA_RT_METRICS.xyxy, float4(1.0, 0.0, 0.0, 1.0), texcoord.xyxy);

    // Fetch the blending weights for current pixel:
    float4 a;
    a.x = SMAASample(blendTex, offset.xy).a; // Right
    a.y = SMAASample(blendTex, offset.zw).g; // Top
    a.wz = SMAASample(blendTex, texcoord).xz; // Bottom / Left

    // Is there any blending weight with a value greater than 0.0?
    SMAA_BRANCH
        if (dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
            return colorTex.Load(int3(pixel, 0)); // exact pass-through (Veyra)
        } else {
            bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

            // Calculate the blending offsets:
            float4 blendingOffset = float4(0.0, a.y, 0.0, a.w);
            float2 blendingWeight = a.yw;
            SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));
            SMAAMovc(bool2(h, h), blendingWeight, a.xz);
            blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

            // Calculate the texture coordinates:
            float4 blendingCoord = mad(blendingOffset, float4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy), texcoord.xyxy);

            // We exploit bilinear filtering to mix current pixel with the chosen
            // neighbor (linear working colour: the blend is in linear light):
            float4 color = blendingWeight.x * SMAASampleLevelZero(colorTex, blendingCoord.xy);
            color += blendingWeight.y * SMAASampleLevelZero(colorTex, blendingCoord.zw);

            return color;
        }
}
#endif
