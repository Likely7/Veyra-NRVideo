#pragma once

namespace veyra::engine {
struct VideoHdrSettings {
    bool enabled=false;
    unsigned contrast=125, saturation=75, middleGray=44, peakNits=1000;
    bool operator==(const VideoHdrSettings&) const = default;
    bool valid() const {
        return contrast<=200 && saturation<=200 && middleGray>=10 &&
            middleGray<=100 && peakNits>=400 && peakNits<=2000;
    }
};
}
