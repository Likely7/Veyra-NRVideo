#pragma once

// Dolby / DTS bitstream support for the capture path.
//
// A capture card that is fed Dolby Atmos / Dolby Audio / DTS by the console
// exposes the audio pin as a *compressed* media type instead of PCM. Veyra used
// to enumerate those types and ignore them, so the user only ever got linear
// PCM. This module decodes such a stream back to interleaved float PCM so it can
// enter the existing 5.1 pipeline, and it also carries the (read-only)
// classification used to report device capability.
//
// Decoding uses the FFmpeg already shipped with the player (libavcodec +
// libswresample). Nothing here changes the PCM path: it is only selected when
// the device offers no usable PCM layout or when passthrough is requested.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace veyra::sink {

// Compressed audio families the capture endpoint can expose. Data1 of the
// KSDATAFORMAT/WAVE subtype GUID is what identifies them on the wire.
enum class BitstreamKind { None, Ac3, Eac3, TrueHd, Dts, DtsHd };

// Maps a media-type subtype id to a family. Returns None for anything unknown
// (never guesses).
BitstreamKind classifyBitstreamSubtype(uint32_t subtypeData1);
const char* bitstreamKindName(BitstreamKind kind);
// Negotiation preference: higher wins when several compressed formats are
// offered (TrueHD Atmos > DD+ Atmos > DD+ > DTS-HD > DTS > AC-3).
int bitstreamPreferenceOrder(BitstreamKind kind);
// True when the subtype carries IEC 61937 framed bursts (S/PDIF style) that must
// be unwrapped before the codec parser sees them.
bool bitstreamIsIec61937(uint32_t subtypeData1);

// IEC 61937 burst header: sync 0xF8724E1F, then a 16-bit data-type word and a
// 16-bit payload length in bits. Mapping a data-type to a codec family is how a
// compressed stream is recognised when the endpoint labels it as plain PCM:
// the AVerMedia capture cards forward the Dolby stream on a 2-channel carrier
// whose media type does not change (see AverMediaAudioSwitch.h).
BitstreamKind classifyIec61937DataType(uint16_t dataType);

// Read-only carrier probe.
//
// Some capture cards deliver an IEC 61937 compressed stream *inside* a media
// type that still claims linear PCM. Guessing from the device's declared
// format is not safe - the same card also downmixes Dolby to real 2.0 PCM when
// passthrough is not armed - so the only reliable discriminator is the bytes
// themselves. This probe scans for valid bursts, requires at least two of the
// same data-type, and then stops; it never changes what is played.
//
// Bounded by design: scanning stops after kMaxScanBytes or on a verdict, and
// only an 8-byte tail is retained between calls.
class Iec61937Probe {
public:
    static constexpr size_t kMaxScanBytes = 1u << 20;

    // Feed carrier bytes in arrival order. Thread-affine: call from the one
    // thread that delivers samples. Logs its verdict exactly once.
    void feed(const uint8_t* data, size_t bytes);

    bool concluded() const;
    bool detected() const;
    uint16_t dataType() const;
    size_t scannedBytes() const;
    unsigned burstCount() const;

private:
    std::vector<uint8_t> tail_;
    size_t scanned_ = 0;
    uint16_t type_ = 0;
    unsigned bursts_ = 0;
    int verdict_ = 0;  // 0 = undecided, 1 = IEC 61937, -1 = not IEC 61937
};

class BitstreamDecoder {
public:
    BitstreamDecoder();
    ~BitstreamDecoder();
    BitstreamDecoder(const BitstreamDecoder&) = delete;
    BitstreamDecoder& operator=(const BitstreamDecoder&) = delete;

    // Opens the decoder for `kind`. `iec61937` unwraps S/PDIF bursts first.
    bool open(BitstreamKind kind, bool iec61937 = false);
    // Feeds raw bytes from the capture endpoint; decoded interleaved float
    // samples are appended to `out`. Returns false only on a hard failure, which
    // is also stored in lastError(). The AC-3 parser holds a complete frame
    // until the next frame header arrives, so this path is one frame behind.
    bool push(const uint8_t* data, size_t bytes, std::vector<float>& out);
    // Complete codec packet, already delimited by the caller (e.g. an IEC
    // payload, which may aggregate frames). It does not wait for the next
    // packet's header. Do not mix this with push() without reset().
    bool pushFramed(const uint8_t* data, size_t bytes, std::vector<float>& out);
    // Drops codec state (seek / discontinuity): the next bytes start a new
    // stream, which is what happens when the console switches audio format.
    void reset();

    bool opened() const;
    unsigned channels() const;
    unsigned sampleRate() const;
    uint32_t channelMask() const;
    uint64_t formatRevision() const;
    uint64_t decodedFrames() const;
    uint64_t decodedBytesIn() const;
    const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::sink
