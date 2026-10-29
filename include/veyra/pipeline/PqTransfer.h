#pragma once
// The ST 2084 transfer function, in the two directions this project needs.
//
// Both directions are used across layers: the source reads HDR10+ scene metadata
// (linear, normalised to 10000 nits) and must store it as PQ for the frame packet,
// while the presenter turns PQ back into nits to move the HDR curve's knee. They
// live here so the two cannot drift apart.
//
// Constants match H.273 / SMPTE ST 2084 and libplacebo's
// pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, x).
#include <algorithm>
#include <cmath>

namespace veyra::pipeline {

// 0..1 PQ -> nits (0..10000).
inline double pqToNits(double pq) {
    if (!(pq > 0.0)) return 0.0;
    constexpr double m1 = 0.1593017578125, m2 = 78.84375;
    constexpr double c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double p = std::pow(pq, 1.0 / m2);
    const double num = std::max(p - c1, 0.0);
    const double den = c2 - c3 * p;
    if (den <= 0.0) return 0.0;
    return 10000.0 * std::pow(num / den, 1.0 / m1);
}

// nits (0..10000) -> 0..1 PQ. Exact inverse of pqToNits.
inline double nitsToPq(double nits) {
    if (!(nits > 0.0)) return 0.0;
    constexpr double m1 = 0.1593017578125, m2 = 78.84375;
    constexpr double c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double l = std::min(nits / 10000.0, 1.0);
    const double lm = std::pow(l, m1);
    return std::pow((c1 + c2 * lm) / (1.0 + c3 * lm), m2);
}

} // namespace veyra::pipeline
