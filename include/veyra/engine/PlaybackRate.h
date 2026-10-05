#pragma once
#include <cmath>

namespace veyra::engine {
inline constexpr double kMinPlaybackRate=.25;
inline constexpr double kMaxPlaybackRate=4.;
inline bool validPlaybackRate(double rate) {
    return std::isfinite(rate)&&rate>=kMinPlaybackRate&&rate<=kMaxPlaybackRate;
}
}
