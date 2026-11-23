#pragma once
#include <cmath>

namespace veyra::engine {
struct VideoDenoiseSettings {
    float luma = 0.20f;
    float chroma = 0.60f;

    bool operator==(const VideoDenoiseSettings&) const = default;
    bool valid() const {
        return std::isfinite(luma) && luma >= 0 && luma <= 1 &&
               std::isfinite(chroma) && chroma >= 0 && chroma <= 1;
    }
};
}
