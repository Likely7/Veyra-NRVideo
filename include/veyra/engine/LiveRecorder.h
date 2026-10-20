#pragma once

// LiveRecorder: records the processed picture the engine presents.
//
// The recorder reuses the product video encoders (NVENC on NVIDIA, the
// driver's Media Foundation hardware MFT elsewhere) against the live
// enhancement graph, so the file holds the pixels the user actually sees
// after NR / SR / frame generation. Container writing runs on a private
// thread behind a bounded queue, so the playback loop never waits for the
// disk; when the queue is full the frame is dropped and counted instead of
// stalling playback.
//
// Scope for this first cut: video only (no audio track), MP4, one file per
// recording, and the presented frame. A settings change or a source switch
// rebuilds the graph, so the recorder closes the file at that point and says
// so instead of writing a file whose header no longer matches the frames.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace veyra::gfx { class D3D12DeviceContext; class CommandSlotRing; }
namespace veyra::pipeline { class EnhanceGraph; }

namespace veyra::engine {

struct RecordingSettings {
    std::wstring directory;      // output folder (created when missing)
    uint32_t bitrateMbps = 20;   // VBR target for the recorded file
    bool hevc = false;           // false: H.264 (SDR), true: HEVC Main10
    uint32_t fpsLimit = 0;       // 0: keep the presented rate
};

struct RecordingStatus {
    bool active = false;
    bool failed = false;
    std::wstring path;           // file written (or being written)
    std::wstring error;          // why it failed, when failed
    std::wstring encoder;        // encoder identity, e.g. "NVIDIA NVENC H.264 (D3D12)"
    uint64_t frames = 0;         // frames handed to the encoder
    uint64_t dropped = 0;        // frames skipped because the writer fell behind
    double seconds = 0;          // recorded length so far
};

class LiveRecorder {
public:
    LiveRecorder();
    ~LiveRecorder();
    LiveRecorder(const LiveRecorder&) = delete;
    LiveRecorder& operator=(const LiveRecorder&) = delete;

    // Engine thread only. Opens the encoder and the container; on failure the
    // reason is written to `error` and the recorder stays idle.
    bool begin(gfx::D3D12DeviceContext&, gfx::CommandSlotRing&, pipeline::EnhanceGraph&,
               const RecordingSettings&, uint32_t nominalFps, std::wstring& error);
    // Engine thread only. Encodes one presented frame; `generated` marks a
    // frame-generation output. Returns false when the recording failed and
    // was closed.
    bool encodeFrame(uint32_t frameSlot, bool generated);
    // Engine thread only. Finishes the file and releases the encoder.
    void end();
    // Any thread.
    bool active() const { return state_.load(std::memory_order_relaxed) == State::Recording; }
    RecordingStatus status() const;

private:
    enum class State { Idle, Recording, Failed };
    struct Impl;
    std::unique_ptr<Impl> p_;
    std::atomic<State> state_{State::Idle};
};

} // namespace veyra::engine
