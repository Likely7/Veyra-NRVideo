#pragma once
#include "veyra/source/AverMediaVendorProtocol.h"
#include "veyra/sink/BitstreamAudio.h"
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace veyra::source {
struct VendorPcmBlock {
    const float* samples=nullptr;
    size_t frames=0;
    unsigned channels=0;
    uint32_t rate=0,mask=0;
    bool discontinuity=false;
    std::string_view codec;
};
struct VendorCarrierState {
    bool bitstream=false;
    unsigned channels=0;
    uint32_t rate=0;
    uint64_t bursts=0,decodedFrames=0,pcmFrames=0,droppedBytes=0;
    size_t retainedBytes=0;
    std::string codec,error;
};
// Thread-affine, bounded, continuously detecting carrier. The explicit clock
// permits deterministic transition tests without waiting for hardware timers.
class AverMediaAudioCarrier {
public:
    using Callback=std::function<bool(const VendorPcmBlock&)>;
    static constexpr size_t kMaxBytes=131072;
    explicit AverMediaAudioCarrier(Callback callback);
    bool feed(std::span<const uint8_t>,avermedia::SampleInfo,double now);
    void reset();
    const VendorCarrierState& state()const{return state_;}
private:
    bool emitPcm(std::span<const uint8_t>,avermedia::SampleInfo);
    bool emitBurst(std::span<const uint8_t>,size_t payloadBytes,sink::BitstreamKind);
    Callback callback_;
    VendorCarrierState state_;
    std::vector<uint8_t> buffer_,packet_;
    std::vector<float> pcm_;
    std::unique_ptr<sink::BitstreamDecoder> decoder_;
    sink::BitstreamKind kind_=sink::BitstreamKind::None,candidate_=sink::BitstreamKind::None;
    unsigned confirmations_=0,undecodedBursts_=0;
    bool compressed_=false,discontinuity_=true;
    double lastIec_=0;
    uint64_t revision_=0;
    avermedia::SampleInfo input_{};
};
} // namespace veyra::source
