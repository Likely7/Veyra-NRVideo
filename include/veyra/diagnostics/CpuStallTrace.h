#pragma once
#include "veyra/Log.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>

namespace veyra::diagnostics {
// Wall time includes descheduling. No allocation or logging on normal frames.
class CpuStallTrace {
    using Clock = std::chrono::steady_clock;
public:
    // stage, when given, receives each mark (string literals) for a watchdog.
    explicit CpuStallTrace(const char* component, uint64_t frame = 0, double thresholdMs = 80.0,
                           std::atomic<const char*>* stage = nullptr)
        : component_(component), frame_(frame), thresholdMs_(thresholdMs), stage_(stage) {}
    void mark(const char* stage) {
        if (stage_) stage_->store(stage, std::memory_order_relaxed);
        const auto now = Clock::now();
        if (count_ < samples_.size()) samples_[count_++] = {stage, ms(now - last_), now};
        last_ = now;
    }
    // Deliberate waits must not look like work that failed to finish.
    void excludeIdle(Clock::duration elapsed) {
        start_+=elapsed;
        last_+=elapsed;
    }
    void recordPrivate(uint64_t session,const pipeline::FrameBatch& batch) const {
        if(!privateFrameTraceActive())return;
        for(size_t i=0;i<count_;++i){
            const auto& sample=samples_[i];
            const auto host=std::chrono::duration_cast<std::chrono::nanoseconds>(sample.end.time_since_epoch()).count()/100;
            FrameTraceEvent event{host,session,batch.identity,batch.batchId,0,batch.b100ns,TraceKind::Cpu,0,1,sample.ms};
            event.frameKind="real";event.stage=sample.name;
            recordPrivateFrame(event);
        }
    }
    ~CpuStallTrace() {
        const auto total = ms(Clock::now() - start_);
        if (total < thresholdMs_) return;
        mark("tail");
        std::string detail;
        for (size_t i = 0; i < count_; ++i)
            detail += std::format(" {}Ms={:.3f}", samples_[i].name, samples_[i].ms);
        log::info(component_, std::format("frame={} totalMs={:.3f}{}", frame_, total, detail));
    }
private:
    static double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
    struct Sample { const char* name; double ms; Clock::time_point end; };
    const char* component_;
    uint64_t frame_;
    double thresholdMs_;
    std::atomic<const char*>* stage_;
    Clock::time_point start_ = Clock::now(), last_ = start_;
    std::array<Sample, 32> samples_{};
    size_t count_ = 0;
};
}
