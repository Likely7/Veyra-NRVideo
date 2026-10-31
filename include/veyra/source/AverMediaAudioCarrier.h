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
    // An IEC 61937 sync was seen and the carrier has not returned to PCM.
    bool compressed()const{return compressed_;}
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
// Any capture card whose audio pin is 16-bit stereo PCM. A PS5 set to Bitstream
// (Dolby/DTS) behind a card whose USB audio has only two channels arrives as
// IEC 61937 inside "PCM": it is decoded through the carrier above instead of
// being played as noise, and the session returns to the card's PCM format once
// the carrier does (4 s after the last burst). Plain PCM stays on the caller's
// unchanged path; a sync-like pattern inside real PCM that brings no second
// burst within kConfirmSeconds is released to the speakers again.
class PcmCarrierRouter {
public:
    static constexpr double kConfirmSeconds=0.3; // E-AC-3 repeats every 128 ms
    enum class Route { Pcm, Withheld };
    struct Result { Route route=Route::Pcm; bool restorePcm=false; };
    PcmCarrierRouter(uint32_t rate,AverMediaAudioCarrier::Callback decoded);
    PcmCarrierRouter(const PcmCarrierRouter&)=delete;
    PcmCarrierRouter& operator=(const PcmCarrierRouter&)=delete;
    // Pcm: push these original bytes. Withheld: push nothing; decoded blocks
    // went to the callback. restorePcm: put the session back on the PCM format
    // before pushing. Thread-affine like the carrier.
    Result feed(std::span<const uint8_t>,double now);
    bool decoding()const{return decoding_;}
    uint64_t falseStarts()const{return falseStarts_;}
    const VendorCarrierState& state()const{return carrier_.state();}
private:
    AverMediaAudioCarrier carrier_;
    avermedia::SampleInfo info_{};
    double compressedSince_=-1;
    uint64_t burstsAtEntry_=0,falseStarts_=0;
    bool decoding_=false;
};
} // namespace veyra::source
