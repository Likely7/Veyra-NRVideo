#pragma once
// HDMI audio from the XCAP card: PCM, or a compressed stream (Dolby Digital / Plus / MAT,
// DTS) carried in IEC 61937 bursts. Compressed audio is forwarded bit for bit to a receiver,
// the only way Dolby Atmos keeps its objects; without such an endpoint it is decoded.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace veyra::sink { enum class BitstreamKind; class BitstreamDecoder; }
namespace veyra::source::xcapture {

enum class Codec { Pcm, Ac3, Dts, DtsHd, Eac3, Mat };
const wchar_t* codecName(Codec);
sink::BitstreamKind bitstreamKind(Codec);

// Turns the card's subframe words into PCM frames or IEC 61937 carrier bytes. No I/O.
class AudioParser {
public:
    struct Output {
        std::function<void(const int32_t* interleaved24, unsigned channels, size_t frames)> pcm;
        // carrier: 16-bit little-endian words, channels 2 or 8, exactly as received
        std::function<void(Codec, unsigned channels, const uint16_t* words, size_t count)> carrier;
        std::function<void(Codec, unsigned channels)> changed;
    };
    explicit AudioParser(Output out) : out_(std::move(out)) {}
    void push(const uint32_t* words, size_t count);
    void reset();
    // The input has a gap (lost packets): start again at the next frame. The codec and the channel count stay,
    // so a running passthrough is not torn down for one missing packet.
    void resync();
    Codec codec() const { return codec_; }
    unsigned channels() const { return channels_; }
private:
    void frame(unsigned channels);
    void flush();
    Output out_;
    int32_t cur_[8]{};
    unsigned seen_ = 0;
    unsigned channels_ = 0;
    Codec codec_ = Codec::Pcm;
    unsigned sinceSync_ = 0;     // frames since the last IEC 61937 preamble
    bool waitPc_ = false;        // the next frame starts with Pc
    std::vector<int32_t> pcm_;
    std::vector<uint16_t> raw_;
};

// Which compressed carriers the active render endpoints accept in exclusive mode.
struct PassthroughSupport { bool ac3 = false, eac3 = false, mat = false, dts = false, dtsHd = false; std::wstring endpoint; };
PassthroughSupport passthroughSupport();
// Monitor name of the TV behind this PC (VEYRA_XCAP_TV, else the one with Dolby audio, else the primary display).
std::wstring tvName();

// Takes the bursts out of a carrier and decodes them: PCM without objects, for endpoints that accept no
// bitstream, and the level meter of the passthrough.
class CarrierDecoder {
public:
    CarrierDecoder();
    ~CarrierDecoder();
    bool open(Codec codec, unsigned carrierChannels);
    void push(const uint16_t* words, size_t count, std::vector<float>& out);   // appends interleaved samples
    unsigned channels() const;
    uint64_t bursts() const { return bursts_; }
private:
    void burst(std::vector<float>& out);
    void trueHd(const uint8_t* data, size_t size, std::vector<float>& out);
    std::unique_ptr<sink::BitstreamDecoder> decoder_;
    Codec codec_ = Codec::Pcm;
    std::vector<uint8_t> payload_, stream_;
    size_t want_ = 0;
    uint16_t last_ = 0, type_ = 0;
    unsigned state_ = 0;
    bool major_ = false;
    uint64_t bursts_ = 0;
};

// Delay line of a compressed carrier. The console's audio clock and the output's differ by tens of ppm
// and a bitstream cannot be resampled, so now and then one WHOLE burst is repeated or left out: the
// receiver keeps seeing valid IEC 61937 framing. Quiet moments are preferred. No I/O, not thread safe.
// A burst is only repeated or left out once two consecutive bursts had the same length; sync words that come
// before that length is over are payload (or a gap in the input), not a burst boundary.
class CarrierQueue {
public:
    void reset(unsigned channels, unsigned rate, size_t periodWords);
    void setTarget(size_t words) { target_ = words; }
    void quiet(bool q) { quiet_ = q; }              // the audio around now is inaudible
    void push(const uint16_t* words, size_t count);
    void pull(uint16_t* out, size_t count);         // silence until primed and after an underrun
    size_t size() const { return queue_.size(); }
    size_t burst() const { return burst_; }
    uint64_t dropped = 0, repeated = 0, underruns = 0, trims = 0;
private:
    static constexpr size_t kLongest = 65536;       // words, longest burst period that is handled whole
    void correct();
    void settle();
    std::vector<uint16_t> current_, previous_;      // the burst coming in, and the complete one before it
    bool synced_ = false, repeat_ = false;
    std::deque<uint16_t> queue_;
    std::deque<uint64_t> starts_;                   // absolute positions of the preambles still queued
    uint64_t head_ = 0, tail_ = 0;                  // absolute position of the first queued / next word
    uint64_t windowEnd_ = 0, pauseUntil_ = 0;
    size_t burst_ = 0, lastLength_ = 0, target_ = 0, period_ = 0, low_ = ~size_t(0), rate_ = 0;
    unsigned channels_ = 2;
    uint16_t last_ = 0;
    bool primed_ = false, quiet_ = false;
};

// Exclusive WASAPI stream that repeats a carrier unchanged. COM must stay initialised (multithreaded) on some
// thread of the process for as long as the object is open: the owner's thread does that, not open().
class CarrierSink {
public:
    CarrierSink();
    ~CarrierSink();
    bool open(Codec codec, unsigned channels, std::wstring& error);
    void push(const uint16_t* words, size_t count);
    void close();
    bool opened() const;
    // The endpoint went away under the open stream (TV or receiver switched off, re-enumerated): close and reopen.
    bool failed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// User choices, shared with the capture source.
void setAudioIngress(unsigned mode);   // 0 automatic, 1 PCM only, 2 bitstream
unsigned audioIngress();
void setAudioDelay(int ms);            // fixed delay; compressed passthrough only, PCM has its own sync
void setAutoAudioDelay(bool on);       // follow the measured picture delay instead
// Picture shown: from its last line readable to Present. Scan-in and scan-out are added here.
void videoLatency(double ms);
int audioDelay();
// Formats that may be advertised to the console for `support` and the user's choice.
PassthroughSupport offeredFormats(const PassthroughSupport& support);
}
