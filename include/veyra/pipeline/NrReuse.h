#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <cstdint>

namespace veyra::pipeline {
// NR inference reuse, frame selection (docs/NR_INFERENCE_REUSE_2026-11-07.md).
//
// Veyra's own design for the realtime preview: when the GPU cannot keep up, a
// frame may skip NR inference and carry the change made by the frame before
// it along the optical flow. A reused frame is ALWAYS followed by an inferred
// one, so a carried change is never older than one source frame and a wrong
// vector can only show for one frame.
//
// Levels: 0 infers every frame; 1, 2 and 3 reuse one frame in every 4, 3 and
// 2. Manual modes pin a level; Auto lets the engine's controller choose it.
// Export, paused frames and images never construct a scheduler.
class NrReuseScheduler {
public:
    static constexpr unsigned kMaxLevel = 3;
    // Inferred frames between two reused ones; zero never reuses.
    static constexpr unsigned inferredPerReuse(unsigned level) {
        return level == 1 ? 3u : level == 2 ? 2u : level == 3 ? 1u : 0u;
    }
    // One reused frame in every `framesPerReuse`, for logs and the UI.
    static constexpr unsigned framesPerReuse(unsigned level) {
        return level ? inferredPerReuse(level) + 1 : 0u;
    }
    static constexpr unsigned levelFor(engine::NrReuseMode mode) {
        return mode == engine::NrReuseMode::Half ? 3u : mode == engine::NrReuseMode::Third ? 2u :
               mode == engine::NrReuseMode::Quarter ? 1u : 0u;
    }

    void setLevel(unsigned level) { level_ = level > kMaxLevel ? kMaxLevel : level; }
    unsigned level() const { return level_; }
    // Once per frame, before NR. `available`: a stored inference from the
    // previous frame and this frame's motion both exist, and nothing reset.
    // Returns true when this frame reuses instead of inferring.
    bool next(bool available) {
        if (level_ == 0 || !available || lastReused_ || inferred_ < inferredPerReuse(level_)) {
            lastReused_ = false;
            ++inferred_;
            return false;
        }
        lastReused_ = true;
        inferred_ = 0;
        return true;
    }
    bool lastReused() const { return lastReused_; }
    // After an inferred frame: whether the next frame may reuse it, so only
    // that inferred frame needs to keep its chain (1/3 and 1/4 skip the rest).
    bool reuseMayFollow() const { return level_ && !lastReused_ && inferred_ >= inferredPerReuse(level_); }
    // A reset, rebuild or settings change: the next frame infers and the
    // pattern restarts from there.
    void reset() { inferred_ = 0; lastReused_ = false; }

private:
    unsigned level_ = 0;
    unsigned inferred_ = 0;
    bool lastReused_ = false;
};
}
