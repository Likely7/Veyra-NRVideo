#pragma once

namespace veyra::engine {
struct VideoHdrSettings {
    bool enabled=false;
    // Custom: also convert HDR sources (HDR10 / Dolby Vision) with RTX Video
    // HDR. Off keeps the whole previous behaviour: an HDR source stays on its
    // native HDR path and this stage is skipped for it. On routes the source
    // through the HDR->SDR tone map first (the working frame becomes SDR), so
    // the very same SDR->HDR stage can then raise it back.
    bool convertHdrSource=false;
    unsigned contrast=125, saturation=75, middleGray=44, peakNits=1000;
    // Custom: controls for that HDR->SDR tone map. Every default reproduces the
    // previously hard-coded numbers exactly, so an untouched install renders
    // identically (see HdrToSdr.hlsli):
    //   sourcePeakNits   0 = take the peak from the frame's own metadata
    //   sdrWhiteNits     SDR white the tone map targets (was fixed at 203)
    //   exposureEv100    exposure in 1/100 EV; 0 = none
    //   shoulderPercent  100 = the previous fixed BT.2390 knee
    unsigned sourcePeakNits=0;
    unsigned sdrWhiteNits=203;
    int exposureEv100=0;
    unsigned shoulderPercent=100;
    bool operator==(const VideoHdrSettings&) const = default;
    bool valid() const {
        return contrast<=200 && saturation<=200 && middleGray>=10 &&
            middleGray<=100 && peakNits>=400 && peakNits<=2000 &&
            (sourcePeakNits==0 || (sourcePeakNits>=400 && sourcePeakNits<=4000)) &&
            sdrWhiteNits>=80 && sdrWhiteNits<=400 &&
            exposureEv100>=-200 && exposureEv100<=200 &&
            shoulderPercent>=50 && shoulderPercent<=150;
    }
    // True only when HDR sources are meant to be tone-mapped down first. Used by
    // the graph to decide the working space and whether this stage runs at all.
    bool convertsHdrSource() const { return enabled && convertHdrSource; }
};
}
