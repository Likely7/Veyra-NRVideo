// SPDX-License-Identifier: GPL-3.0-only
#include "AsrAudioTap.h"
#include "AsrModels.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace veyra::ui::asr {
void AudioTap::renderedPcm(const float* pcm, size_t frames, const sink::AudioFormat& format,
                           int64_t playoutUs, const void* renderer) {
    if (!pcm || !renderer || !format.valid() || !frames) return;
    // Prefer the renderer already feeding this session. A replacement can
    // take over after a short gap, for example when the endpoint is reopened.
    if (renderer_ && renderer_ != renderer && playoutUs < rendererEndUs_ + 200000) return;
    renderer_ = renderer;
    rendererEndUs_ = playoutUs + static_cast<int64_t>(frames) * 1000000 / inputRate;
    unsigned next = write_.load(std::memory_order_relaxed);
    for (size_t offset = 0; offset < frames;) {
        if (next - read_.load(std::memory_order_acquire) >= slotCount) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto& block = slots_[next % slotCount];
        block.playoutUs = playoutUs + static_cast<int64_t>(offset) * 1000000 / inputRate;
        block.frames = std::min(blockFrames, frames - offset);
        block.format = format;
        block.renderer = renderer;
        std::memcpy(block.pcm.data(), pcm + offset * format.channels, block.frames * format.channels * sizeof(float));
        offset += block.frames;
        write_.store(++next, std::memory_order_release);
    }
}

bool AudioTap::pop(Block& block) {
    const unsigned next = read_.load(std::memory_order_relaxed);
    if (next == write_.load(std::memory_order_acquire)) return false;
    block = slots_[next % slotCount];
    read_.store(next + 1, std::memory_order_release);
    return true;
}

Resampler::Resampler() = default;
Resampler::~Resampler() { reset(); }

void Resampler::reset() {
    swr_free(&context_);
}

bool Resampler::convert(const AudioTap::Block& block, std::vector<float>& output) {
    if (!context_ || block.format != format_) {
        reset();
        format_ = block.format;
        AVChannelLayout input{};
        if (av_channel_layout_from_mask(&input, block.format.mask) < 0) return false;
        AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        const int allocated = swr_alloc_set_opts2(&context_, &mono, AV_SAMPLE_FMT_FLT, sampleRate,
            &input, AV_SAMPLE_FMT_FLT, inputRate, 0, nullptr);
        av_channel_layout_uninit(&input);
        if (allocated < 0) return false;
        std::array<double, 8> weights{};
        if (format_.channels == 1) weights[0] = 1.0;
        else if (format_.channels == 2) weights[0] = weights[1] = 0.5;
        else {
            const bool hasCenter = (format_.mask & SPEAKER_FRONT_CENTER) != 0;
            unsigned channel = 0;
            for (unsigned bit = 0; bit < 18; ++bit) {
                const auto speaker = uint32_t{1} << bit;
                if (!(format_.mask & speaker)) continue;
                if (speaker == SPEAKER_FRONT_CENTER) weights[channel] = 0.625;
                if (speaker == SPEAKER_FRONT_LEFT || speaker == SPEAKER_FRONT_RIGHT)
                    weights[channel] = hasCenter ? 0.1875 : 0.5;
                ++channel;
            }
        }
        if (swr_set_matrix(context_, weights.data(), int(format_.channels)) < 0 || swr_init(context_) < 0) {
            reset();
            return false;
        }
    }
    output.resize(size_t(swr_get_out_samples(context_, int(block.frames))));
    auto* dest = reinterpret_cast<uint8_t*>(output.data());
    const auto* src = reinterpret_cast<const uint8_t*>(block.pcm.data());
    const int converted = swr_convert(context_, &dest, int(output.size()), &src, int(block.frames));
    if (converted < 0) return false;
    output.resize(size_t(converted));
    return true;
}
}
