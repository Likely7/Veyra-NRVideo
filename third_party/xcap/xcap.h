// User-mode access to the XCAP capture driver. No dependency beyond Win32.
#pragma once
#include <windows.h>
#include <winioctl.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "xcap_ioctl.h"

namespace xcap {

struct Config {
    uint32_t width = 3840, height = 2160;
    uint32_t pixfmt = XCAP_PIXFMT_V210;
    uint32_t slots = 6;
    uint32_t sliceLines = 72;        // progress granularity; at most XCAP_MAX_BANDS (60) slices per frame
    bool hdmi = false;               // false: built-in test pattern
    bool audio = false;              // also stream audio packets
};

inline uint32_t strideOf(uint32_t pixfmt, uint32_t width) {
    return pixfmt == XCAP_PIXFMT_V210 ? (width + 5) / 6 * 16 : width * 4;
}

struct Clock { uint64_t ticks = 0; int64_t qpc = 0, errorQpc = 0; }; // one FPGA/host time pair

// Threads: slicesDone(), waitSlices(), frameIntact() and audioPackets() may be called from several threads
// at once, also while another thread calls stop() (they then report 0 / Stopped). open(), close() and
// start() belong to one thread at a time. stop() takes the views of the ring away: pointers from slot(),
// header(), pixels(), trailer(), audioPacket() and ctl() must not be used after it has been called, so
// threads that read frames have to be finished (joined) before stop(), close() or the destructor.
class Device {
public:
    Device() = default;
    ~Device() { close(); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    static std::vector<std::wstring> enumerate();
    bool open(const std::wstring& path);        // empty = first device
    void close();
    bool isOpen() const { return h_ != INVALID_HANDLE_VALUE; }
    DWORD lastError() const { return error_; }

    bool info(xcap_info& out);
    bool read(uint32_t reg, uint32_t& value);
    bool write(uint32_t reg, uint32_t value);
    bool clock(Clock& out);

    bool start(const Config& config);
    void stop();
    bool streaming() const { return started_; }

    // ---- progress ----
    // Number of slices finished since start(). Frame f covers slices [f*S, (f+1)*S).
    // The card counts descriptors in 24 bits; the count is widened here, so one of slicesDone(),
    // waitSlices() or frameIntact() has to run at least once per 2^23 descriptors (minutes of video at
    // the least; audioPackets(): once per 2^23 packets, hours) or progress is lost.
    uint64_t slicesDone();
    // Waits until `target` slices are finished. spinUs: busy-wait this long after each wake-up
    // before sleeping again (0 = event only).
    enum class Wait { Ok, Timeout, Stopped };
    Wait waitSlices(uint64_t target, uint32_t timeoutMs, uint32_t spinUs = 0);
    // False once the DMA may have started to overwrite the slot of this frame.
    bool frameIntact(uint64_t frame);

    const uint8_t* slot(uint64_t frame) const { return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(out_.slot[frame % cfg_.slots])); }
    const xcap_frame_header* header(uint64_t frame) const { return reinterpret_cast<const xcap_frame_header*>(slot(frame)); }
    const uint8_t* pixels(uint64_t frame) const { return slot(frame) + XCAP_HDR_BYTES; }
    const xcap_frame_trailer* trailer(uint64_t frame) const { return reinterpret_cast<const xcap_frame_trailer*>(slot(frame) + out_.frame_bytes - XCAP_TRL_BYTES); }

    // ---- audio ----
    // Packets finished since start(); packet n stays readable until n + XCAP_AUD_RING is written.
    uint64_t audioPackets();
    const xcap_audio_packet* audioPacket(uint64_t n) const { return reinterpret_cast<const xcap_audio_packet*>(static_cast<uintptr_t>(out_.audio_ring)) + n % XCAP_AUD_RING; }
    HANDLE audioEvent() const { return audioEvent_; }

    const Config& config() const { return cfg_; }
    const xcap_start_out& layout() const { return out_; }
    const xcap_ctl* ctl() const { return reinterpret_cast<const xcap_ctl*>(static_cast<uintptr_t>(out_.ctl)); }
    uint32_t slicesPerFrame() const { return out_.slices; }
    uint32_t stride() const { return strideOf(cfg_.pixfmt, cfg_.width); }
    uint32_t dataBytes() const { return stride() * cfg_.height; }
    HANDLE event() const { return event_; }

private:
    bool ioctl(DWORD code, const void* in, DWORD inLen, void* out, DWORD outLen);
    HANDLE h_ = INVALID_HANDLE_VALUE, event_ = nullptr, audioEvent_ = nullptr;
    DWORD error_ = 0;
    bool running();
    static uint64_t widen(std::atomic<uint64_t>& total, uint32_t now24);
    std::atomic<bool> started_{false};
    SRWLOCK views_ = SRWLOCK_INIT;          // shared: out_ and the mapped ring are in use; exclusive: start()/stop() change them
    Config cfg_;
    xcap_start_out out_{};
    std::atomic<uint64_t> descs_{0};        // descriptors / audio packets done; the low 24 bits are the card's counter
    std::atomic<uint64_t> aud_{0};
};

// CRC-32C per 32-bit lane over 16-byte words, the function the FPGA computes.
void crcLanes(const uint8_t* data, size_t bytes, uint32_t crc[4]);

} // namespace xcap
