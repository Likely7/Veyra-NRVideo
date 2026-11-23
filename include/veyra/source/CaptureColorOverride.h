#pragma once
#include "veyra/pipeline/FramePacket.h"

namespace veyra::source {
// Persisted connection value: low two bits = auto/PQ/HLG/709;
// next two bits = auto/limited/full. Legacy values 0..2 retain their meaning.
constexpr unsigned captureColorSpace(unsigned value) { return value & 3u; }
constexpr unsigned captureColorRange(unsigned value) { return value >> 2; }
constexpr unsigned captureColorOverride(unsigned space,unsigned range) { return space | (range << 2); }
constexpr bool validCaptureColorOverride(unsigned value) { return value <= 11; }
constexpr unsigned effectiveCaptureColorOverride(unsigned value,bool compressed) {
    return compressed?captureColorOverride(0,captureColorRange(value)):value;
}
inline void applyCaptureColorOverride(pipeline::ColorDescription& color,unsigned value) {
    using namespace pipeline;
    const auto space=captureColorSpace(value),range=captureColorRange(value);
    if(space) {
        color.transfer=space==1?TransferFunction::PQ:space==2?TransferFunction::HLG:TransferFunction::BT709;
        color.matrix=space==3?YuvMatrix::BT709:YuvMatrix::BT2020NCL;
        color.primaries=space==3?ColorPrimaries::BT709:ColorPrimaries::BT2020;
        color.preserveSdrCodeValues=space==3;
        color.transferAssumed=color.matrixAssumed=color.primariesAssumed=false;
    }
    if(range) { color.range=range==1?ColorRange::Limited:ColorRange::Full;color.rangeAssumed=false; }
}
}
