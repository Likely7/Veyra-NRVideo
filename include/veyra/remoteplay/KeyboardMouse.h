#pragma once
#include "Types.h"
#include <stickmouse_mapper.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace veyra::remoteplay {
// One instance per control-capable connection. The callback only enqueues states
// on its own connection. Chiaki calls stay on the connection's sender thread.
class KeyboardMouse {
public:
    using Submit = std::function<void(ControllerState)>;
    using Warning = std::function<void(const char*)>;
    using Clock = std::chrono::steady_clock;
    explicit KeyboardMouse(Submit mapped, Submit physical = {}, Warning warning = {})
        : submit_(std::move(mapped)), submitPhysical_(physical ? std::move(physical) : submit_),
          warning_(std::move(warning)) {}
    ~KeyboardMouse() { retire(); }

    std::string start(std::string_view json, unsigned dpi, std::string_view initial);
    void stop();
    void retire();
    void physical(ControllerState state);
    bool active() const;
    void pausePhysical(bool pause);
    int situation() const;
    bool select(std::string_view name);
    void key(uint16_t scan, bool down, Clock::time_point captured = Clock::now());
    void motion(int dx, int dy);
    void button(uint8_t index, bool down);
    void wheel(int delta, bool horizontal);
    void triggerPair(const std::array<uint8_t,11>& left, const std::array<uint8_t,11>& right,
                     Clock::time_point captured = Clock::now());
    void haptics(const uint8_t* pcm, size_t bytes, Clock::time_point captured = Clock::now(),
                 bool gapBefore = false);
    void disconnected(Clock::time_point captured = Clock::now());

private:
    enum WarningBit : unsigned { InputOverflow=1, SubmissionFailed=2, FeedbackRejected=4 };
    void warnOnce(WarningBit, const char*) noexcept;
    void feedback(stickmouse_mapper_feedback packet, Clock::time_point captured);
    void stopLocked();
    static void receive(void*, const stickmouse_mapper_frame*, uint8_t) noexcept;
    void frame(const stickmouse_mapper_frame&, bool overflow);
    mutable std::mutex mutex_;
    Submit submit_, submitPhysical_;
    Warning warning_;
    std::atomic<unsigned> warnings_{0};
    bool physicalPaused_=false;
    stickmouse_mapper_auto_session* session_ = nullptr;
    Clock::time_point origin_;
    stickmouse_mapper_profile* profile_ = nullptr;
    std::atomic<int> situation_{0};
    std::atomic<uint64_t> frameTimeNs_{0};
    uint64_t previousFrameTimeNs_ = 0; // worker only until stop joins it
    int manualSituation_ = -1;
    uint64_t manualTimeNs_ = 0;
    ControllerState previous_; // worker only, reset after the worker joins
};
}
