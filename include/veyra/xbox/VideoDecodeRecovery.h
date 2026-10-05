// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>

namespace veyra::xbox {
enum class VideoDecodeRecoveryAction { ResetHistory, ReopenHardware, UseSoftware, Stop };

// A bad decoder must not be retried forever with new IDRs. Keep transient
// loss on hardware, reopen that context once, then fall back once per session.
// A successful picture clears consecutive errors, not the bounded reopen budget.
class VideoDecodeRecovery {
public:
    VideoDecodeRecoveryAction failed(bool hardware) {
        if (++consecutiveFailures_ < 3) return VideoDecodeRecoveryAction::ResetHistory;
        consecutiveFailures_ = 0;
        if (hardware && !hardwareReopened_) {
            hardwareReopened_ = true;
            return VideoDecodeRecoveryAction::ReopenHardware;
        }
        if (hardware && !softwareUsed_) {
            softwareUsed_ = true;
            return VideoDecodeRecoveryAction::UseSoftware;
        }
        return VideoDecodeRecoveryAction::Stop;
    }
    void picture() { consecutiveFailures_ = 0; }
private:
    unsigned consecutiveFailures_ = 0;
    bool hardwareReopened_ = false, softwareUsed_ = false;
};

// Call while holding the source queue lock, before adding the new AU. Loss
// belongs to the first surviving AU, even when the IDR itself was discarded.
template<class Queue>
std::size_t trimVideoInputs(Queue& queue, std::size_t capacity) {
    if (!capacity) capacity = 1;
    std::size_t dropped = 0;
    while (queue.size() >= capacity) { queue.pop_front(); ++dropped; }
    if (dropped && !queue.empty()) queue.front().inputLoss = true;
    return dropped;
}
}
