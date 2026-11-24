#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>

namespace veyra::engine {

class CaptureReconnectBudget {
public:
    using Clock = std::chrono::steady_clock;
    enum class Limit { None, NoFrames, DeviceRemovals, NoVideoAfterRelease };
    struct Decision {
        std::chrono::seconds delay{};
        Limit limit = Limit::None;
        bool releaseDevice = false;
    };

    Decision afterFailure(Clock::time_point now, bool deviceRemoved, bool noVideoAfterRun = false) {
        while (!removals_.empty() && now - removals_.front() >= std::chrono::minutes(10))
            removals_.pop_front();
        if (deviceRemoved && removals_.size() < kRemovalLimit) removals_.push_back(now);
        if (removals_.size() >= kRemovalLimit) return {{}, Limit::DeviceRemovals};
        if (releasedDevice_) return {{}, Limit::NoVideoAfterRelease};
        if (attempts_ == kDelays.size()) return {{}, Limit::NoFrames};
        noVideoAttempts_ = attempts_ && noVideoAfterRun ? noVideoAttempts_ + 1 : 0;
        if (noVideoAttempts_ == 2) {
            releasedDevice_ = true;
            return {std::chrono::seconds(2), Limit::None, true};
        }
        return {std::chrono::seconds(kDelays[attempts_])};
    }

    void attemptStarted() { ++attempts_; }
    void frameDelivered() {
        attempts_ = noVideoAttempts_ = 0;
        releasedDevice_ = false;
    }
    unsigned attempts() const { return attempts_; }
    size_t recentRemovals() const { return removals_.size(); }

private:
    static constexpr std::array kDelays{1, 2, 4, 8, 15};
    static constexpr size_t kRemovalLimit = 10;
    unsigned attempts_ = 0, noVideoAttempts_ = 0;
    bool releasedDevice_ = false;
    std::deque<Clock::time_point> removals_;
};
}
