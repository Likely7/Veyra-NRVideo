#include "veyra/remoteplay/KeyboardMouse.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace veyra::remoteplay {
namespace {
stickmouse_mapper_text span(std::string_view s) { return {s.data(), static_cast<uint32_t>(s.size())}; }
std::string lastError() {
    const auto error = stickmouse_mapper_last_error();
    return {error.data, error.len};
}
using S = ControllerState;
constexpr uint32_t masks[]{S::Cross,S::Circle,S::Square,S::Triangle,S::L1,S::R1,0,0,
    S::L3,S::R3,S::Options,S::Share,S::PS,S::Touchpad,S::Up,S::Down,S::Left,S::Right};
int16_t axis(float x) {
    x = std::clamp(x, -1.f, 1.f);
    return static_cast<int16_t>(std::lround(x * (x < 0 ? 32768.f : 32767.f)));
}
}
std::string KeyboardMouse::start(std::string_view json, unsigned dpi, std::string_view initial) {
    std::lock_guard lock(mutex_);
    if (!submit_) return "Session ended";
    stopLocked();
    warnings_.store(0);
    if (stickmouse_mapper_profile_load(span(json), &profile_)) return lastError();
    if (dpi && stickmouse_mapper_profile_set_dpi(profile_, dpi)) {
        auto error = lastError();
        stopLocked();
        return error;
    }
    // A physical edge can still be queued on the connection owner. Flush it
    // before the translator starts its worker and publishes the first frame.
    submit_({});
    uint8_t initialId = 0;
    if (!initial.empty() && stickmouse_mapper_situation_id(profile_,span(initial),&initialId)) {
        auto error = lastError();
        stopLocked();
        return error;
    }
    situation_.store(initialId);
    frameTimeNs_.store(0);
    previousFrameTimeNs_ = 0;
    if (stickmouse_mapper_auto_start(profile_, span(initial), receive, this, &session_)) {
        auto error = lastError();
        stopLocked();
        return error;
    }
    // Pin the host's steady clock to this worker's origin once. Trigger and PCM
    // callbacks can then preserve receipt times across processing and contention.
    uint64_t elapsed = 0;
    const auto sampled = Clock::now();
    if (stickmouse_mapper_auto_now_ns(session_, &elapsed)) {
        auto error = lastError();
        stopLocked();
        return error;
    }
    origin_ = sampled - std::chrono::nanoseconds(elapsed);
    stickmouse_mapper_feedback sources{};
    sources.kind = STICKMOUSE_MAPPER_FEEDBACK_SOURCES;
    sources.sources = STICKMOUSE_MAPPER_SOURCE_TRIGGERS | STICKMOUSE_MAPPER_SOURCE_HAPTICS;
    feedback(sources,Clock::now());
    struct Config { KeyboardMouse* host; bool failed = false; } config{this};
    auto enable = [](void* user, stickmouse_mapper_text signal, stickmouse_mapper_text) {
        auto& config = *static_cast<Config*>(user);
        uint8_t supported = 0;
        auto& host = *config.host;
        if (stickmouse_mapper_supports_builtin_recognition(host.profile_, signal, &supported) ||
            (supported && stickmouse_mapper_auto_set_builtin_recognition(host.session_,signal,1,nullptr)))
            config.failed = true;
    };
    if (stickmouse_mapper_unavailable_switches(profile_, nullptr, 0, enable, &config) || config.failed) {
        stopLocked();
        return "Could not enable profile recognition";
    }
    return {};
}
bool KeyboardMouse::active() const { std::lock_guard lock(mutex_); return session_ != nullptr; }
void KeyboardMouse::pausePhysical(bool pause) {
    std::lock_guard lock(mutex_);
    if(pause && !physicalPaused_ && !session_ && submit_)submit_({});
    physicalPaused_=pause;
}
void KeyboardMouse::physical(ControllerState state) {
    std::lock_guard lock(mutex_);
    if (!session_ && !physicalPaused_ && submitPhysical_) submitPhysical_(std::move(state));
}
void KeyboardMouse::stopLocked() {
    if (session_) {
        stickmouse_mapper_auto_stop(session_);
        // The final neutral frame rolls back to the last published situation.
        // Keep a player's explicit choice if no gameplay tick processed it yet.
        if (manualSituation_ >= 0 && previousFrameTimeNs_ < manualTimeNs_)
            situation_.store(manualSituation_);
        manualSituation_ = -1;
        stickmouse_mapper_auto_free(session_);
        session_ = nullptr;
        previous_ = {};
        submit_({}); // cancel queued gameplay before physical control resumes
    }
    if (profile_) stickmouse_mapper_profile_free(profile_);
    profile_ = nullptr;
}
void KeyboardMouse::stop() { std::lock_guard lock(mutex_); stopLocked(); }
void KeyboardMouse::retire() { std::lock_guard lock(mutex_); stopLocked(); submit_ = {}; submitPhysical_ = {}; }
int KeyboardMouse::situation() const {
    std::lock_guard lock(mutex_);
    if (manualSituation_ >= 0 && frameTimeNs_.load() < manualTimeNs_)
        return manualSituation_;
    return situation_.load();
}
bool KeyboardMouse::select(std::string_view name) {
    std::lock_guard lock(mutex_);
    uint8_t id{};
    uint64_t now = 0;
    if (!session_ || stickmouse_mapper_situation_id(profile_,span(name),&id) ||
        stickmouse_mapper_auto_select_situation(session_,id,nullptr) ||
        stickmouse_mapper_auto_now_ns(session_,&now)) return false;
    manualSituation_ = id;
    manualTimeNs_ = now;
    return true;
}
void KeyboardMouse::key(uint16_t scan, bool down, Clock::time_point captured) {
    std::lock_guard lock(mutex_);
    if (!session_ || captured < origin_) return;
    const uint64_t time = std::chrono::duration_cast<std::chrono::nanoseconds>(captured-origin_).count();
    stickmouse_mapper_auto_key(session_,&time,0,scan,down);
}
void KeyboardMouse::button(uint8_t index, bool down) {
    std::lock_guard lock(mutex_);
    if (session_) stickmouse_mapper_auto_mouse_button(session_,nullptr,0,index,down);
}
void KeyboardMouse::motion(int dx, int dy) {
    std::lock_guard lock(mutex_);
    if (session_) stickmouse_mapper_auto_motion(session_,nullptr,0,dx,dy,1);
}
void KeyboardMouse::wheel(int delta, bool horizontal) {
    std::lock_guard lock(mutex_);
    if (session_) stickmouse_mapper_auto_wheel(session_,nullptr,0,horizontal,delta);
}
void KeyboardMouse::feedback(stickmouse_mapper_feedback packet, Clock::time_point captured) {
    if (!session_ || captured < origin_) return;
    packet.time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(captured-origin_).count();
    if (stickmouse_mapper_auto_submit_feedback(session_,&packet,1)) {
        warnOnce(FeedbackRejected, "PS5 keyboard/mouse feedback was rejected; clearing pending evidence");
        packet.kind = STICKMOUSE_MAPPER_FEEDBACK_LOST;
        packet.sources = STICKMOUSE_MAPPER_SOURCE_TRIGGERS | STICKMOUSE_MAPPER_SOURCE_HAPTICS;
        stickmouse_mapper_auto_submit_feedback(session_,&packet,1);
    }
}
void KeyboardMouse::triggerPair(const std::array<uint8_t,11>& left,const std::array<uint8_t,11>& right,Clock::time_point captured) {
    std::lock_guard lock(mutex_);
    stickmouse_mapper_feedback p{};
    p.kind = STICKMOUSE_MAPPER_FEEDBACK_TRIGGER_PAIR;
    p.l2.mode = left[0]; p.r2.mode = right[0];
    std::copy(left.begin()+1,left.end(),p.l2.params);
    std::copy(right.begin()+1,right.end(),p.r2.params);
    feedback(p,captured);
}
void KeyboardMouse::haptics(const uint8_t* pcm, size_t bytes, Clock::time_point captured, bool gapBefore) {
    std::lock_guard lock(mutex_);
    if (!session_) return;
    stickmouse_mapper_feedback p{};
    p.sources = STICKMOUSE_MAPPER_SOURCE_HAPTICS;
    const bool valid = pcm && bytes && bytes%4 == 0 && bytes<=1200;
    if (gapBefore || !valid) {
        p.kind = STICKMOUSE_MAPPER_FEEDBACK_LOST;
        feedback(p,captured);
        if (!valid) return;
    }
    p.kind = STICKMOUSE_MAPPER_FEEDBACK_HAPTIC_PEAK;
    for (size_t i=0; i<bytes; i+=2) {
        int16_t sample;
        std::memcpy(&sample, pcm+i, sizeof(sample));
        p.peak = std::max(p.peak, static_cast<uint16_t>(std::abs(int(sample))));
    }
    feedback(p,captured);
}
void KeyboardMouse::disconnected(Clock::time_point captured) {
    std::lock_guard lock(mutex_);
    stickmouse_mapper_feedback p{};
    p.kind = STICKMOUSE_MAPPER_FEEDBACK_DISCONNECTED;
    feedback(p,captured);
}
void KeyboardMouse::receive(void* ctx,const stickmouse_mapper_frame* f,uint8_t overflow) noexcept {
    auto& host = *static_cast<KeyboardMouse*>(ctx);
    try { host.frame(*f,overflow!=0); }
    catch (...) { host.warnOnce(SubmissionFailed, "PS5 keyboard/mouse controller submission failed"); }
}
void KeyboardMouse::warnOnce(WarningBit bit, const char* message) noexcept {
    if (warnings_.fetch_or(bit) & bit) return;
    try {
        if (warning_) { warning_(message); return; }
    } catch (...) {}
    std::fprintf(stderr,"%s\n",message);
}
void KeyboardMouse::frame(const stickmouse_mapper_frame& f,bool overflow) {
    situation_.store(f.situation);
    previousFrameTimeNs_ = frameTimeNs_.exchange(f.time_ns);
    if (overflow) {
        previous_ = {};
        submit_(previous_);
        warnOnce(InputOverflow, "PS5 keyboard/mouse input overflow; controls released");
        return;
    }
    previous_.inputActive = true;
    for (uint32_t i=0; i<f.edge_count; ++i) {
        const auto& edge = f.edges[i];
        if (edge.button >= std::size(masks)) continue;
        if (edge.button == STICKMOUSE_MAPPER_L2) previous_.l2 = edge.pressed ? 255 : 0;
        else if (edge.button == STICKMOUSE_MAPPER_R2) previous_.r2 = edge.pressed ? 255 : 0;
        else if (edge.pressed) previous_.buttons |= masks[edge.button];
        else previous_.buttons &= ~masks[edge.button];
        submit_(previous_);
    }
    previous_.buttons = 0;
    for (size_t i=0; i<std::size(masks); ++i)
        if (f.buttons & (1u<<i)) previous_.buttons |= masks[i];
    previous_.leftX=axis(f.left[0]); previous_.leftY=axis(-f.left[1]);
    previous_.rightX=axis(f.right[0]); previous_.rightY=axis(-f.right[1]);
    previous_.l2=uint8_t(std::lround(std::clamp(f.triggers[0],0.f,1.f)*255));
    previous_.r2=uint8_t(std::lround(std::clamp(f.triggers[1],0.f,1.f)*255));
    for (size_t i=0; i<2; ++i) previous_.touches[i]={int8_t(f.touch[i].active?f.touch[i].id:-1),f.touch[i].x,f.touch[i].y};
    submit_(previous_);
}
}
