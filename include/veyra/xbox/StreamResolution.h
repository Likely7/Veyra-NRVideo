// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace veyra::xbox {

enum class StreamResolution { Auto = 0, FullHd = 1080, QuadHd = 1440 };

struct StreamExtent { unsigned width, height; };

constexpr StreamExtent streamExtent(StreamResolution resolution) {
    return resolution == StreamResolution::QuadHd ? StreamExtent{2560, 1440} : StreamExtent{1920, 1080};
}

} // namespace veyra::xbox
