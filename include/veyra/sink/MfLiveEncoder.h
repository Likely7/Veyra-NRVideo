#pragma once
// Media Foundation hardware MFT for the live recorder (AMD and Intel
// adapters, and NVIDIA when NVENC refuses a session): system-memory NV12 in,
// microsecond timestamps in and out, 8-bit 4:2:0 only. The vendor MFT
// selection, rate control and event handling are the export encoder's
// (MfVideoEncoder.cpp). One thread drives every call.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "veyra/sink/VideoEncoder.h"

namespace veyra::sink {

class MfLiveEncoder {
public:
    virtual ~MfLiveEncoder()=default;
    // config.fpsNum/fpsDen set rate control and the 2 s GOP; the writer
    // receives timestamps in microseconds.
    virtual bool openLive(uint32_t width,uint32_t height,const EncoderConfig& config,PacketWriter writer)=0;
    virtual std::vector<uint8_t> liveHeaders() const=0;
    // Y rows `yPitch` bytes apart, then interleaved UV rows `uvPitch` apart.
    virtual bool encodeNv12(const uint8_t* y,uint32_t yPitch,const uint8_t* uv,uint32_t uvPitch,int64_t ptsUs)=0;
    virtual bool finishLive()=0;
    virtual std::wstring liveDescription() const=0;
    virtual std::wstring liveError() const=0;
};

std::unique_ptr<MfLiveEncoder> createMediaFoundationLiveEncoder();

} // namespace veyra::sink
