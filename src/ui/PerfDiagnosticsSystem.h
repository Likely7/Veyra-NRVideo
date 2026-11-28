// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// 性能诊断: the environment snapshot and the once-per-second system sampler.
// Everything here is read-only: no driver, registry or power setting changes.
#include <QJsonObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace veyra::ui::perfdiag {

// Replaces the user profile, the user name and other home-relative paths so a
// diagnostic package does not carry the account name.
QString redactText(QString text);

// OS, CPU, memory, adapters and drivers, displays (refresh, HDR), HAGS, game
// mode and windowed optimisations, power plan and battery, modules injected
// into this process and overlay/recording tools that are running.
QJsonObject systemSnapshot();

// Values the interface thread copies from the engine snapshot once a second.
struct EngineSample {
    int64_t host=0;               // traceHostNow() when the UI thread read it
    bool running=false,capture=false,remotePlay=false,applying=false,fgBudgetLimited=false;
    double submitFps=-1,low1Fps=-1,captureFps=0,lateMs=0,lateP95Ms=0;
    double schedulingWaitP95Ms=0,processCpuP95Ms=0,presentCpuP95Ms=0;
    double nominalSourceFps=0,displayRefreshHz=0;
    uint64_t frames=0,generated=0,captureReceived=0,captureDropped=0,captureRateSkipped=0,previewSkipped=0;
    unsigned displayVrr=0;int compositionMode=-1;int contentFps=0;
    uint32_t sourceWidth=0,sourceHeight=0;
};

// Writes telemetry.csv: one row per second with CPU (system and Veyra), Veyra
// memory, per-engine GPU utilisation from the Windows "GPU Engine" counters
// (Veyra vs. every other process, the busiest other process by name), GPU
// dedicated memory, NVIDIA clocks/power/temperature/throttle reasons through
// the driver's own nvml.dll when present, the latest engine sample and how
// late the interface thread's last sample was (interface stalls).
class TelemetrySampler {
public:
    TelemetrySampler();
    ~TelemetrySampler();
    bool start(const std::filesystem::path& csv);
    void push(const EngineSample& sample);
    void stop();
    uint64_t rows() const { return rows_.load(); }
    // Human-readable description of what could be sampled ("pdh nvml").
    std::string sources() const;
private:
    void run();
    struct State;
    State* state_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_=false;
    EngineSample latest_{};
    bool haveSample_=false;
    std::atomic<uint64_t> rows_{0};
};

}
