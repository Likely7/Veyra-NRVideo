// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <objbase.h> // Declare COM GUIDs before the KS media GUID macros.
#include "veyra/sink/RecordingAudioTap.h"

#include <array>
#include <atomic>
#include <vector>

struct SwrContext;

namespace veyra::ui::asr {
// One producer at a time is enforced by RecordingAudioTap's forwarder. Only
// the controller consumes slots. Neither side waits for the other.
class AudioTap final : public sink::RecordingAudioSink {
public:
    static constexpr size_t blockFrames = 480;
    struct Block {
        std::array<float, blockFrames * 8> pcm{};
        sink::AudioFormat format;
        const void* renderer = nullptr;
        int64_t playoutUs = 0;
        size_t frames = 0;
    };
    void renderedPcm(const float*, size_t, const sink::AudioFormat&, int64_t, const void*) override;
    bool pop(Block&);
    unsigned takeDrops() { return drops_.exchange(0, std::memory_order_relaxed); }

private:
    static constexpr unsigned slotCount = 128;
    std::array<Block, slotCount> slots_;
    std::atomic<unsigned> read_{0};
    std::atomic<unsigned> write_{0};
    std::atomic<unsigned> drops_{0};
    const void* renderer_ = nullptr;
    int64_t rendererEndUs_ = 0;
};

class Resampler {
public:
    Resampler();
    ~Resampler();
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;
    bool convert(const AudioTap::Block& block, std::vector<float>& output);
    void reset();

private:
    SwrContext* context_ = nullptr;
    sink::AudioFormat format_;
};
}
