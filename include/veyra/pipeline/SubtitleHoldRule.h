#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS Crosshair Hold: a crosshair that frame generation never sees.
//
// A crosshair stands still while the world behind it moves. Frame generation moves it with the world, so in
// every generated frame it is gone from its place and stands twice, half as strong, half the motion to either
// side; some methods also warp the picture round it. Putting its pixels back afterwards (Live HUD Lock, HUD
// protection) leaves the two ghosts, and a crosshair that is partly see-through never "stays the same" at all:
// its pixels change with what is behind it.
//
// So the crosshair is taken out of what frame generation is given, and laid back over what it makes:
//   1. FIND (every real frame, in a zone round the screen centre). A crosshair is made of thin bars: pixels that
//      are brighter (or darker) than something close by on both sides, whatever is behind them. A bar is counted
//      while the zone's picture moves and the picture near the bar with it; a bar of the world moves with it and
//      loses its count, a bar of an overlay keeps it. A bar whose every pixel has stood for kStand counted frames, and whose two ends stand still, is
//      FOUND and remembered; a bar lying where one was found may then move its ends (a reticle spreads and
//      narrows) and is FOLLOWED. A ring is found as the short bars it is made of, a dot as one short bar or,
//      when it is wider, by its outline (the inside of a small closed shape goes with it).
//   2. FILL. The found bars and a few pixels round them are filled in from the pixels beside them, in a copy of
//      the real frame that only frame generation gets. The frame that is shown is never changed.
//   3. COMPOSE (every generated frame). On those pixels the generated frame becomes
//          lerp(A, B, t) + m * (G - lerp(fA, fB, t))
//      A, B the two real frames, fA, fB their filled copies, G what frame generation made of the copies, and m how
//      much of the background shows through the overlay there, worked out from the two frames themselves
//      (0 for a solid overlay: the real pixels; about a half for a half see-through reticle).
//
// Everything here is the rule itself, without graphics: the shaders (shaders/PushOverlay*.hlsl) are the same
// statements, and tests/integration/PushGpuTests.cpp compares them with this file pixel by pixel. Change both
// together. The numbers were found on real frames of a real game (five clips, 23 November 2026) and are not to
// be changed without running that study again: a rule of this kind that is a little too eager draws artifacts
// round everything that moves (PushHudStill.h says how that went).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace veyra::pipeline::overlay {

// What the rule measures in, for a picture of a given height (the numbers are for 2160 lines).
struct Rule {
    int flank = 5;       // a bar's flank is looked for this many pixels to each side
    int stand = 4;       // counted frames in a row before a pixel has stood (the same at every size)
    int longest = 110;   // a bar longer than this along its own direction is not an overlay's
    int shortest = 4;    // nor one shorter than this
    int fresh = 24;      // a followed bar may have this many pixels outside what is remembered
    int memory = 60;     // real frames a place stays remembered after a bar was found there
    int grow = 3;        // the mask is the bars and this many pixels round them
    int reach = 24;      // how far the fill looks for a pixel outside the mask
    int hole = 9;        // a pixel with an accepted bar this near on all four sides is inside a small shape, and masked
};
inline constexpr float kContrast = 0.10f;       // a bar differs this much from its flanks (the picture as encoded, 0..1)
inline constexpr float kMoveRatio = 2.0f;       // a block moves when its change is this many times its own detail
inline constexpr float kMoveFloor = 0.004f * 0.004f;   // and more than this (mean square): below it there is nothing to judge
inline constexpr int kMoveShareDivisor = 10;    // the zone moves when a tenth of its blocks do
inline constexpr int kBlock = 16;
inline constexpr int kNearBlocks = 1;           // a frame counts for a pixel when a block this near to its own moves
inline constexpr float kWeightFloor = 1e-4f;    // added to the denominator of m
inline constexpr int kKinds = 4;                // bright with flanks left/right, bright up/down, dark left/right, dark up/down
inline constexpr int kMaxFlank = 8, kMaxGrow = 4, kMaxReach = 32, kMaxLongest = 160, kMaxHole = 12;

inline int scaled(double value, uint32_t height, int low, int high) {
    return std::clamp(int(std::floor(value * double(height) / 2160.0 + 0.5)), low, high);
}
inline Rule ruleFor(uint32_t height) {
    Rule r;
    r.flank = scaled(5, height, 2, kMaxFlank);
    r.longest = scaled(110, height, 16, kMaxLongest);
    r.shortest = scaled(4, height, 2, 8);
    r.fresh = scaled(24, height, 4, 48);
    r.grow = scaled(3, height, 1, kMaxGrow);
    r.reach = scaled(24, height, 4, kMaxReach);
    r.hole = scaled(9, height, 2, kMaxHole);
    return r;
}
// The zone: a square round the screen centre, a whole number of blocks, on even pixels. Since Subtitle Hold
// (after 4.4.5) a zone may be a rectangle: w x h; for the crosshair's square, w = h = size.
struct Zone { uint32_t x = 0, y = 0, size = 0, w = 0, h = 0; };
inline Zone zoneFor(uint32_t width, uint32_t height) {
    uint32_t size = uint32_t(std::floor(400.0 * double(height) / 2160.0 + 0.5));
    size = std::min({size, width, height}) / kBlock * kBlock;
    Zone z;
    if (size < 4 * kBlock) return z;     // too small a picture: nothing is done
    z.size = z.w = z.h = size; z.x = ((width - size) / 2) & ~1u; z.y = ((height - size) / 2) & ~1u;
    return z;
}
// PushFPS Subtitle Hold: the band where a game draws its subtitles, along the bottom of the picture: the middle
// 62.5 % of the width and the bottom 400 / 2160 of the height, from 4 % above the bottom edge (GTA 6's lines sit
// at about 90 % of the height). Whole blocks, on even pixels; `size` is the width, so code that only knows
// squares sees a zone it cannot take for one (`w != h`).
inline Zone subtitleZoneFor(uint32_t width, uint32_t height) {
    Zone z;
    uint32_t h = uint32_t(std::floor(400.0 * double(height) / 2160.0 + 0.5)) / kBlock * kBlock;
    uint32_t w = uint32_t(std::floor(double(width) * 0.625 + 0.5)) / kBlock * kBlock;
    if (h < 4 * kBlock || w < 4 * kBlock || h > height || w > width) return z;
    const uint32_t gap = uint32_t(std::floor(double(height) * 0.04 + 0.5));
    if (h + gap > height) return z;
    z.w = z.size = w; z.h = h;
    z.x = ((width - w) / 2) & ~1u;
    z.y = (height - gap - h) & ~1u;
    return z;
}
// How a frame's values become the 0..1 the thresholds are in (as Live HUD Lock): sRGB and PQ are used as
// they are, scRGB is linear with 1 = 80 nits.
}
