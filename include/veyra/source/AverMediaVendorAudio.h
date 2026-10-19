#pragma once

// AVerMedia multichannel capture audio, driven in this process.
//
// The card only forwards the console's compressed (Dolby) stream when
// AVerMedia's own audio graph is running; a plain DirectShow/WASAPI capture of
// the card's audio function keeps handing out linear PCM. The OBS component
// solves this by driving `AVerMedia::AudioDevice` (avt_dshow_core.dll) inside
// the OBS process; this class does the same for Veyra and turns the resulting
// IEC 61937 carrier into interleaved float PCM for the existing capture audio
// session.
//
// The AudioDealer ABI is checked against the fixed public headers referenced
// in THIRD_PARTY_NOTICES.md. Everything here
// is read-only with respect to the vendor binaries: they are loaded by absolute
// path from the user's own installation, never copied, patched or signed.
//
// Behaviour:
//   * The carrier arrives as 16-bit stereo at 48 kHz. When IEC 61937 bursts are
//     present the payload is unwrapped, byte-swapped per 16-bit word and sent to
//     the FFmpeg AC-3 decoder already used by the bitstream capture path.
//   * PCM uses its actual callback metadata. Carrier detection continues while
//     PCM plays; compressed-to-PCM changes reconfigure the existing session.
//   * A bounded watchdog rebuilds the graph when callbacks stop or decoding
//     fails, then exposes terminal failure for the owner's DirectShow fallback.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "veyra/source/AverMediaAudioCarrier.h"

namespace veyra::source {

struct AverMediaVendorAudioState {
    bool started = false;
    bool running = false;
    bool bitstream = false;
    bool starting = false;
    bool failed = false;
    std::wstring device;
    std::string carrier;      // "AC-3" / "linear PCM 2ch" once decided
    unsigned channels = 0;
    uint32_t sampleRate = 0;
    uint64_t callbacks = 0;
    uint64_t carrierBytes = 0;
    uint64_t bursts = 0;
    uint64_t decodedFrames = 0;
    uint64_t pcmFrames = 0;
    uint64_t restarts = 0;
    uint64_t losses = 0;
    std::wstring error;
};

class AverMediaVendorAudio {
public:
    // Implementation handle. Public only so the module's own translation unit
    // can forward-declare it for its callback object.
    struct Impl;

    // Called on the vendor streaming thread with interleaved float PCM.
    // Block frames count sample frames, not floats. False requests recovery.
    using PcmCallback = AverMediaAudioCarrier::Callback;

    explicit AverMediaVendorAudio(unsigned stallMs=6000,unsigned maxRestarts=3);
    ~AverMediaVendorAudio();
    AverMediaVendorAudio(const AverMediaVendorAudio&) = delete;
    AverMediaVendorAudio& operator=(const AverMediaVendorAudio&) = delete;

    // Resolves the installed component, starts the vendor graph on a dedicated
    // STA thread. Waits at most 150ms for an initial result; a true return may
    // mean pending startup. state().starting/failed distinguishes that from
    // actual samples. Missing components and immediate failures return false.
    // `preferName` is the capture card's own name ("Live Gamer Ultra 2.1") and
    // `preferPath` its DirectShow device path. The vendor device list is matched
    // on the USB instance first (the audio function of the same card shares it),
    // then on the name, so a second AVerMedia card in the same machine cannot
    // donate its audio to this capture.
    bool start(PcmCallback onPcm, const std::wstring& preferName = std::wstring(),
               const std::wstring& preferPath = std::wstring());

    // Stops the graph and joins the worker. Safe to call more than once.
    void stop();

    bool running() const;
    AverMediaVendorAudioState state() const;

private:
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::source
