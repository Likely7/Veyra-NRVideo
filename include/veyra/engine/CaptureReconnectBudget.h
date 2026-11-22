#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>

namespace veyra::engine {

class CaptureReconnectBudget {
public:
    using Clock = std::chrono::steady_clock;
    enum class Limit { None, NoFrames, DeviceRemovals };
    struct Decision {
        std::chrono::seconds delay{};
        Limit limit = Limit::None;
    };

    Decision afterFailure(Clock::time_point now, bool deviceRemoved) {
        while (!removals_.empty() && now - removals_.front() >= std::chrono::minutes(10))
            removals_.pop_front();
        if (deviceRemoved && removals_.size() < kRemovalLimit) removals_.push_back(now);
        if (removals_.size() >= kRemovalLimit) return {{}, Limit::DeviceRemovals};
        if (attempts_ == kDelays.size()) return {{}, Limit::NoFrames};
        return {std::chrono::seconds(kDelays[attempts_])};
    }

    void attemptStarted() { ++attempts_; }
    void frameDelivered() { attempts_ = 0; }
    unsigned attempts() const { return attempts_; }
    size_t recentRemovals() const { return removals_.size(); }

private:
    static constexpr std::array kDelays{1, 2, 4, 8, 15};
    static constexpr size_t kRemovalLimit = 10;
    unsigned attempts_ = 0;
    std::deque<Clock::time_point> removals_;
};
}
