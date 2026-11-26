#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace veyra::source {
class CaptureCardSource;
struct SourceOpenDesc;
}

namespace veyra::engine {
class EngineWatchdog;

// Serializes graph control on its own COM thread. The playback owner can use
// the frame mailbox and source state between operations. It must take the
// result before reading the source again, and stop using it before close().
class CaptureDriverWorker {
public:
    using Operation = std::function<bool(source::CaptureCardSource&)>;

    CaptureDriverWorker();
    ~CaptureDriverWorker();
    CaptureDriverWorker(const CaptureDriverWorker&) = delete;
    CaptureDriverWorker& operator=(const CaptureDriverWorker&) = delete;

    source::CaptureCardSource& source();
    bool configure(source::SourceOpenDesc desc);
    bool submit(Operation operation);
    std::optional<bool> takeResult(std::chrono::milliseconds wait = {});
    uint64_t stalledMs() const;
    void close();
    bool waitClosed(std::chrono::milliseconds wait = {}) const;

private:
    struct State;
    std::shared_ptr<State> state_;
    std::shared_ptr<EngineWatchdog> watchdog_;
    std::thread thread_;
};
}
