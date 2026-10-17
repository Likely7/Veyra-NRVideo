#pragma once
// Profile 7 reconstruction parameters in the exact layout the YUV upload pass
// reads (dv0..dv6). Kept free of FFmpeg types so frame packets can carry it.
#include <cstdint>

namespace veyra::pipeline {

inline constexpr int kDoviP7FloatCount = 28;

struct DoviP7Constants {
    float params[kDoviP7FloatCount] = {};
    bool ready = false;   // false: this RPU cannot be expressed, keep the base layer
};

} // namespace veyra::pipeline
