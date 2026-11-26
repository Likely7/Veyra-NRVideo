#include "CaptureDriverWorker.h"

#include "veyra/engine/EngineWatchdog.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/Log.h"

#include <d3d12.h>
#include <objbase.h>
#include <wrl/client.h>
#include <atomic>
#include <condition_variable>
#include <format>
#include <mutex>
#include <utility>

namespace veyra::engine {

struct CaptureDriverWorker::State {
    std::mutex mutex;
    std::condition_variable wake;
    Operation pending;
    std::optional<bool> result;
    bool working = false, closed = false;
    std::atomic<bool> cancelled{false};
    std::atomic<uint64_t> stalled{0};
    // SourceOpenDesc keeps raw pointers for reconnect. Pin them until the
    // graph, callbacks and any compressed decoder have all been released.
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    std::unique_ptr<source::CaptureCardSource> capture = std::make_unique<source::CaptureCardSource>();

    void run() {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized))
            log::error("capture-driver", std::format("COM initialization hr=0x{:08X}", uint32_t(initialized)));

        for (;;) {
            Operation operation;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return cancelled || pending; });
                if (cancelled) break;
                operation = std::exchange(pending, {});
            }

            bool ok = false;
            try {
                if (SUCCEEDED(initialized) && !cancelled) ok = operation(*capture);
            } catch (const std::exception& error) {
                log::error("capture-driver", std::format("operation failed: {}", error.what()));
            }

            {
                std::lock_guard lock(mutex);
                working = false;
                if (!cancelled) result = ok;
            }
            wake.notify_all();
        }

        // This may block too. Only this thread owns closing the graph.
        capture.reset();
        queue.Reset();
        device.Reset();
        if (SUCCEEDED(initialized)) CoUninitialize();
        log::info("capture-driver","graph and device references released");
        {
            std::lock_guard lock(mutex);
            closed = true;
        }
        wake.notify_all();
    }
};

CaptureDriverWorker::CaptureDriverWorker()
    : state_(std::make_shared<State>()),
      watchdog_(std::make_shared<EngineWatchdog>(
          [weak = std::weak_ptr(state_)](uint64_t stalledMs, bool captureDriver) {
              if (captureDriver) if (const auto state = weak.lock()) state->stalled = stalledMs;
          })) {
    watchdog_->beginSession();
    state_->capture->setDriverStageObserver(
        [weak = std::weak_ptr(watchdog_), state = state_.get()](source::CaptureDriverStage stage) {
            state->stalled = 0;
            if (const auto watchdog = weak.lock()) watchdog->setCaptureDriverStage(stage);
        });
    thread_ = std::thread([state = state_] { state->run(); });
}

CaptureDriverWorker::~CaptureDriverWorker() {
    close();
    // An uncooperative driver must not hold application shutdown. The worker
    // owns all state it can still reach and never calls back into the player.
    if (waitClosed(std::chrono::milliseconds(200))) thread_.join();
    else thread_.detach();
}

source::CaptureCardSource& CaptureDriverWorker::source() {
    return *state_->capture;
}

bool CaptureDriverWorker::configure(source::SourceOpenDesc desc) {
    Microsoft::WRL::ComPtr<ID3D12Device> device(static_cast<ID3D12Device*>(desc.d3d12Device));
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue(static_cast<ID3D12CommandQueue*>(desc.d3d12Queue));
    return submit([state = state_.get(), desc = std::move(desc), device = std::move(device),
                   queue = std::move(queue)](source::CaptureCardSource& capture) mutable {
        state->device = std::move(device);
        state->queue = std::move(queue);
        desc.cancelled = &state->cancelled;
        return capture.configure(desc);
    });
}

bool CaptureDriverWorker::submit(Operation operation) {
    {
        std::lock_guard lock(state_->mutex);
        if (state_->cancelled || state_->working || state_->result) return false;
        state_->working = true;
        state_->pending = std::move(operation);
    }
    state_->wake.notify_all();
    return true;
}

std::optional<bool> CaptureDriverWorker::takeResult(std::chrono::milliseconds wait) {
    std::unique_lock lock(state_->mutex);
    if (wait.count() > 0)
        state_->wake.wait_for(lock, wait, [&] { return state_->result.has_value() || state_->cancelled; });
    const auto result = state_->result;
    state_->result.reset();
    return result;
}

uint64_t CaptureDriverWorker::stalledMs() const {
    return state_->stalled.load();
}

void CaptureDriverWorker::close() {
    {
        std::lock_guard lock(state_->mutex);
        state_->cancelled = true;
        state_->pending = {};
        state_->result.reset();
    }
    state_->wake.notify_all();
}

bool CaptureDriverWorker::waitClosed(std::chrono::milliseconds wait) const {
    std::unique_lock lock(state_->mutex);
    if (wait.count() > 0)
        state_->wake.wait_for(lock, wait, [&] { return state_->closed; });
    return state_->closed;
}
}
