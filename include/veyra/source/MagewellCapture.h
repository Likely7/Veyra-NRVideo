#pragma once
// Magewell Pro Capture low-latency video (MWCapture SDK, "low latency mode + partial notify").
//
// Magewell's PCIe Pro Capture cards can start the DMA of a frame after the first 64-256 lines
// have reached the card instead of after the whole frame, so the frame is complete in host
// memory almost as soon as its last line arrives (Magewell: 1080p60 capture latency ~21.7 ms ->
// ~17 ms, 4K60 ~37.6 ms -> ~22.6 ms). DirectShow cannot ask for that; the vendor SDK can.
// USB Capture models and first-generation cards do not support it.
//
// LibMWCapture.dll is loaded at run time (runtime\magewell next to the program first, then the
// system), so builds and machines without it simply keep the ordinary DirectShow capture.
// Calling sequence follows the SDK's Examples\Applications\LowLatency.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace veyra::source::magewell {

// The DirectShow device path of a Magewell PCIe card (PCI vendor 0x1CD7). USB Capture models
// enumerate as USB devices and are deliberately excluded: they have no low-latency mode.
bool isProCaptureDevicePath(std::wstring_view directShowPath);

// True when this build knows the SDK and LibMWCapture.dll could be loaded. `detail` says
// which file was used or why not.
bool runtimeAvailable(std::wstring* detail = nullptr);

// The user's choice (capture dialog). Read when a capture device is connected.
void setLowLatencyPreference(bool enabled);
bool lowLatencyPreference();

// One line for the capture dialog: active with its measured latency, or why it is not active.
std::wstring statusText();

// Pixel formats the SDK can write in the layout the capture path already expects.
enum class Format : uint8_t { Nv12, Yuy2, P010, Bgra };

struct VideoRequest {
    std::wstring directShowPath;     // the device the user picked
    Format format = Format::Nv12;
    unsigned width = 0, height = 0;
    unsigned stride = 0;             // bytes per luma/packed row, as the capture layout expects
    size_t frameBytes = 0;           // whole frame in that layout
    bool bottomUp = false;           // RGB DIB orientation of the layout
    int partialLines = 64;           // DMA starts after this many lines (64/128/256)
};

struct VideoStats {
    uint64_t frames = 0, timeouts = 0, signalChanges = 0;
    double frameStartToHostMs = 0;   // average: frame started arriving -> whole frame in host memory
};

class LowLatencyVideo {
public:
    // data/bytes: one complete frame; deviceSeconds: when the frame started arriving (card clock);
    // discontinuity: the signal changed or frames were skipped since the previous call.
    using Deliver = std::function<void(const uint8_t* data, size_t bytes, double deviceSeconds, bool discontinuity)>;

    LowLatencyVideo();
    ~LowLatencyVideo();
    LowLatencyVideo(const LowLatencyVideo&) = delete;
    LowLatencyVideo& operator=(const LowLatencyVideo&) = delete;

    // Opens the matching SDK channel and starts the capture thread. False (with `error`) means
    // the caller keeps using DirectShow for video.
    bool start(const VideoRequest& request, Deliver deliver, std::wstring& error);
    void stop();
    bool running() const;
    VideoStats stats() const;

private:
    bool startImpl(const VideoRequest& request, Deliver deliver, std::wstring& error);
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::source::magewell
