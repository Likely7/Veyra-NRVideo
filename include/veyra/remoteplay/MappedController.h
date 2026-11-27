// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/remoteplay/KeyboardMousePad.h"
#include "veyra/remoteplay/AimTest.h"
#include <array>
#include <functional>
#include <memory>
#include <mutex>

namespace veyra::remoteplay {

// Raw Input produces timestamped events. The protocol sender consumes their
// edges and samples continuous axes on the same clock as the outgoing packet.
class MappedController {
public:
    struct Timing {
        size_t events = 0;
        double p50 = 0., p95 = 0., maximum = 0.;
    };
    struct Diagnostics {
        Timing keys, mouse, sends;
        double fullStickFraction = 0.;
        double mouseReportsPerSecond = 0.;
        uint64_t revision = 0;
    };
    struct Preview {
        ControllerState pad;
        double speedCounts = 0.;
        Diagnostics diagnostics;
        bool driving = false;
    };

    explicit MappedController(KeyboardMousePad layout, std::function<bool()> focused)
        : mapper_(std::move(layout)), mouse_(mapper_.tuning()), focused_(std::move(focused)) {
        mapper_.reset();
        preview_.driving = mapper_.driving();
    }

    void press(int key, bool down, int64_t us) {
        std::lock_guard lock(mutex_);
        if (!active_) return;
        if (aimTest_) { aimTest_->key(key, down); return; }
        enqueue({key, down, us});
    }

    void wheel(int key, int steps, int64_t us) {
        std::lock_guard lock(mutex_);
        if (!active_) return;
        for (int i = 0; i < steps && wheelCount_ < wheelKeys_.size(); ++i) {
            wheelKeys_[(wheelStart_ + wheelCount_) % wheelKeys_.size()] = {key, true, us};
            ++wheelCount_;
        }
    }

    void move(int dx, int dy, int64_t us) {
        if (!dx && !dy) return;
        std::lock_guard lock(mutex_);
        if (!active_) return;
        if (mapper_.driving() && mapper_.tuning().mouseSteering) steeringCounts_ += dx;
        else mouse_.move(dx, dy, us);
        lastMouse_ = us;
        if (!unsampledMouse_) unsampledMouse_ = us;
        ++mouseReports_;
    }

    void setTuning(KeyboardMousePad::Tuning tuning) {
        std::lock_guard lock(mutex_);
        mapper_.setTuning(tuning);
        mouse_.setTuning(tuning);
        lastMouse_ = unsampledMouse_ = 0;
        steeringCounts_ = steering_ = 0.;
        driveAt_ = 0;
    }

    void setGameFps(double fps) {
        std::lock_guard lock(mutex_);
        mouse_.setGameFps(fps);
    }
    void setAimTest(std::shared_ptr<AimTest> test) {
        std::lock_guard lock(mutex_);
        aimTest_ = std::move(test);
        mouse_.resetMotion();
        mapper_.reset();
        eventCount_ = wheelCount_ = 0;
    }
    bool aimKey(int key, bool down) {
        std::lock_guard lock(mutex_);
        return aimTest_ && aimTest_->key(key, down);
    }

    void selectDriving(bool driving, int64_t us) {
        std::lock_guard lock(mutex_);
        if (active_) enqueue({driving ? kDrive : kWalk, true, us});
    }

    void stop() {
        std::lock_guard lock(mutex_);
        deactivate();
    }

    Preview preview() const {
        std::lock_guard lock(mutex_);
        return preview_;
    }
    bool hasFocus() const {
        std::lock_guard lock(mutex_);
        return active_ && (!focused_ || focused_());
    }

    ControllerState sample(int64_t us, const ControllerState& physical = {}) {
        std::lock_guard lock(mutex_);
        if (!active_) return {};
        if (focused_ && !focused_()) { deactivate(); return {}; }
        if (aimTest_) return aimTest_->sample(us);
        const auto& tune = mapper_.tuning();
        if (!windowStart_) windowStart_ = us;
        const auto elapsedUs = previousSend_ ? us - previousSend_ : 0;
        if (previousSend_) sendTimes_.add(elapsedUs);
        previousSend_ = us;
        if (unsampledMouse_) {
            mouseTimes_.add(us - unsampledMouse_);
            unsampledMouse_ = 0;
        }
        if (us - windowStart_ >= 10000000) {
            const auto revision = preview_.diagnostics.revision + 1;
            preview_.diagnostics = {keyTimes_.timing(), mouseTimes_.timing(), sendTimes_.timing(),
                totalSamples_ ? double(fullSamples_) / totalSamples_ : 0.,
                mouseReports_ * (1000000. / (us - windowStart_)), revision};
            keyTimes_ = mouseTimes_ = sendTimes_ = {};
            windowStart_ = us;
            fullSamples_ = totalSamples_ = mouseReports_ = 0;
        }

        bool layoutChanged = false;
        while (eventCount_) {
            const auto event = events_[eventStart_];
            eventStart_ = (eventStart_ + 1) % events_.size();
            --eventCount_;
            const bool driving = mapper_.driving();
            bool stateChanged;
            if (event.key == kDrive || event.key == kWalk) {
                mapper_.selectDriving(event.key == kDrive);
                stateChanged = driving != mapper_.driving();
            } else {
                stateChanged = mapper_.digital(event.key, event.down, event.us);
            }
            if (!stateChanged) continue;
            keyTimes_.add(us - event.us);
            layoutChanged = mapper_.driving() != driving;
            break; // A press and release in one tick still get two packets.
        }
        if (layoutChanged) driveAt_ = 0;
        if (!layoutChanged && driveAt_ && us >= driveAt_) {
            mapper_.selectDriving(!mapper_.driving());
            driveAt_ = 0;
            layoutChanged = true;
        }
        if (layoutChanged || releaseEdge_) {
            releaseEdge_ = false;
            if (wheelKey_) mapper_.digital(wheelKey_, false, us);
            wheelKey_ = 0;
            wheelCount_ = 0;
            wheelAt_ = 0;
            mouse_.resetMotion();
            lastMouse_ = 0;
            steeringCounts_ = steering_ = 0.;
            preview_.pad = {};
            preview_.driving = mapper_.driving();
            return {};
        }
        if (wheelKey_ && us >= wheelAt_) {
            mapper_.digital(wheelKey_, false, us);
            wheelKey_ = 0;
            wheelAt_ = us + 32000;
        }
        if (!wheelKey_ && wheelCount_ && us >= wheelAt_) {
            const auto event = wheelKeys_[wheelStart_];
            wheelStart_ = (wheelStart_ + 1) % wheelKeys_.size();
            --wheelCount_;
            wheelKey_ = event.key;
            mapper_.digital(wheelKey_, true, us);
            keyTimes_.add(us - event.us);
            wheelAt_ = us + 55000;
        }

        bool useMouse = false;
        auto mapped = mapper_.state(&useMouse, us);
        const bool vehicleButton = (mapped.buttons & ControllerState::Triangle) != 0;
        if (tune.autoDriving && vehicleButton && !vehicleButtonHeld_) driveAt_ = us + tune.autoDriveMs * 1000LL;
        vehicleButtonHeld_ = vehicleButton;
        const bool mouseSteering = mapper_.driving() && tune.mouseSteering;
        mouse_.controlContext(mapper_.menuActive(), mapper_.driving());
        if (mouseSteering) {
            const double recenter = double(elapsedUs) / (tune.steeringReturnMs * 1000.);
            steering_ = std::copysign(std::max(0., std::abs(steering_) - recenter), steering_);
            steering_ = std::clamp(steering_ + steeringCounts_ * 2.54 / tune.dpi * tune.mouseSteerGain, -1., 1.);
            steeringCounts_ = 0.;
            if (std::abs(steering_ * 32767.) > std::abs(mapped.leftX))
                mapped.leftX = int16_t(std::lround(steering_ * 32767.));
        } else if (useMouse) {
            mouse_.apply(mapped, us);
        }
        if (useMouse && std::hypot(double(mapped.rightX), double(mapped.rightY)) >= .98 * 32767.) ++fullSamples_;
        ++totalSamples_;
        preview_.pad = mapped;
        preview_.speedCounts = mouse_.speed();
        preview_.driving = mapper_.driving();
        const bool recentMouse = useMouse && !mouseSteering && lastMouse_ && us - lastMouse_ < 100000;
        return tune.mixPhysical ? combine(mapped, physical, recentMouse) : mapped;
    }

private:
    struct Event { int key = 0; bool down = false; int64_t us = 0; };
    static constexpr int kWalk = -1, kDrive = -2;
    struct Times {
        std::array<size_t, 202> buckets{};
        size_t count = 0;
        int64_t maximum = 0;
        void add(int64_t us) {
            us = std::max<int64_t>(0, us);
            ++buckets[std::min<size_t>(us / 500, buckets.size() - 1)];
            maximum = std::max(maximum, us);
            ++count;
        }
        Timing timing() const {
            Timing result{count, 0., 0., maximum / 1000.};
            if (!count) return result;
            size_t sum = 0;
            for (size_t i = 0; i < buckets.size(); ++i) {
                sum += buckets[i];
                if (sum >= (count + 1) / 2) { result.p50 = std::min(result.maximum, (i + 1) * .5); break; }
            }
            sum = 0;
            for (size_t i = 0; i < buckets.size(); ++i) {
                sum += buckets[i];
                if (sum >= (count * 95 + 99) / 100) { result.p95 = std::min(result.maximum, (i + 1) * .5); break; }
            }
            return result;
        }
    };

    static ControllerState combine(ControllerState mapped, const ControllerState& pad, bool recentMouse) {
        if (!pad.inputActive) return mapped;
        mapped.buttons |= pad.buttons;
        mapped.l2 = std::max(mapped.l2, pad.l2);
        mapped.r2 = std::max(mapped.r2, pad.r2);
        const auto magnitude = [](int16_t x, int16_t y) { return double(x) * x + double(y) * y; };
        if (magnitude(pad.leftX, pad.leftY) > magnitude(mapped.leftX, mapped.leftY)) {
            mapped.leftX = pad.leftX;
            mapped.leftY = pad.leftY;
        }
        if (!recentMouse && magnitude(pad.rightX, pad.rightY) > magnitude(mapped.rightX, mapped.rightY)) {
            mapped.rightX = pad.rightX;
            mapped.rightY = pad.rightY;
        }
        if (mapped.touches[0].id < 0) mapped.touches = pad.touches;
        if (!mapped.motionValid && pad.motionValid) {
            mapped.gyro = pad.gyro;
            mapped.accel = pad.accel;
            mapped.motionTimestampUs = pad.motionTimestampUs;
            mapped.motionValid = true;
            mapped.motionSamples = pad.motionSamples;
            mapped.motionSampleCount = pad.motionSampleCount;
        }
        return mapped;
    }

    void enqueue(Event event) {
        if (eventCount_ == events_.size()) {
            // Queue overload releases buttons before accepting more edges.
            mapper_.reset();
            eventCount_ = 0;
            releaseEdge_ = true;
        }
        events_[(eventStart_ + eventCount_) % events_.size()] = event;
        ++eventCount_;
    }

    void deactivate() {
        active_ = false;
        if (aimTest_) aimTest_->cancel();
        eventCount_ = wheelCount_ = 0;
        mapper_.reset();
        mouse_.resetMotion();
        preview_.pad = {};
    }

    mutable std::mutex mutex_;
    KeyboardMousePad mapper_;
    MouseStick mouse_;
    std::function<bool()> focused_;
    std::shared_ptr<AimTest> aimTest_;
    std::array<Event, 64> events_{};
    std::array<Event, 16> wheelKeys_{};
    size_t eventStart_ = 0, eventCount_ = 0, wheelStart_ = 0, wheelCount_ = 0;
    bool active_ = true, releaseEdge_ = false;
    int wheelKey_ = 0;
    int64_t wheelAt_ = 0, lastMouse_ = 0, unsampledMouse_ = 0;
    int64_t windowStart_ = 0, previousSend_ = 0;
    int64_t driveAt_ = 0;
    double steering_ = 0., steeringCounts_ = 0.;
    bool vehicleButtonHeld_ = false;
    size_t fullSamples_ = 0, totalSamples_ = 0, mouseReports_ = 0;
    Times keyTimes_, mouseTimes_, sendTimes_;
    Preview preview_;
};

} // namespace veyra::remoteplay
