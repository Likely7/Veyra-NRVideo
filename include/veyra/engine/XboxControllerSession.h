// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/source/XboxSessionSource.h"
#include "veyra/xbox/DisconnectPolicy.h"
#include "veyra/Log.h"
#include <format>

namespace veyra::engine {

// One capture-side Xbox session. Its owner can stop or replace it without
// blocking the capture engine or the caller that changes the source.
class XboxControllerSession {
public:
    struct Status {
        bool requested = false, reconnecting = false;
        source::XboxStats stream;
    };
    ~XboxControllerSession() {
        stop();
        worker_.request_stop();
        wake_.notify_all();
    }
    void start(source::XboxConnectDesc desc) {
        desc.controlOnly = true;
        desc.gamepad = true;
        auto request = std::make_shared<Request>();
        request->desc = std::move(desc);
        request->desc.stopRequested = &request->cancelled;
        {
            std::lock_guard lock(mutex_);
            cancelLocked();
            request_ = std::move(request);
            failure_ = {};
            if (!worker_.joinable()) worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
        }
        wake_.notify_all();
    }
    void stop() {
        {
            std::lock_guard lock(mutex_);
            cancelLocked();
            request_.reset();
            failure_ = {};
        }
        wake_.notify_all();
    }
    bool waitIdle(const std::atomic<bool>& stop) {
        const auto deadline = Clock::now() + std::chrono::seconds(15);
        std::unique_lock lock(mutex_);
        while (session_ && !stop && Clock::now() < deadline) wake_.wait_for(lock, std::chrono::milliseconds(100));
        return !session_ && !request_;
    }
    std::shared_ptr<source::XboxSessionSource> source(uint64_t generation = 0) const {
        std::lock_guard lock(mutex_);
        return request_ && generation_ && (!generation || generation == generation_) ? session_ : nullptr;
    }
    uint64_t inputTarget() const {
        std::lock_guard lock(mutex_);
        return request_ && generation_ ? generation_ : 0;
    }
    Status status() const {
        std::lock_guard lock(mutex_);
        if (!request_) return {};
        if (reconnecting_) return {true, true, failure_};
        if (session_) return {true, false, session_->stats()};
        return {true, false, failure_};
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Request {
        source::XboxConnectDesc desc;
        std::atomic<bool> cancelled{false};
    };

    void cancelLocked() {
        if (request_) request_->cancelled = true;
        if (session_) session_->cancel();
        generation_ = 0;
        reconnecting_ = false;
    }
    void run(std::stop_token stop) {
        std::shared_ptr<Request> previous;
        while (!stop.stop_requested()) {
            std::shared_ptr<Request> request;
            {
                std::unique_lock lock(mutex_);
                if (!wake_.wait(lock, stop, [this, &previous] { return request_ && request_ != previous; })) break;
                previous = request = request_;
            }
            xbox::ReconnectBudget budget;
            for (;;) {
                auto session = std::make_shared<source::XboxSessionSource>();
                {
                    std::lock_guard lock(mutex_);
                    if (request->cancelled || stop.stop_requested()) break;
                    session_ = session;
                    reconnecting_ = false;
                }
                const bool connected = session->connect(request->desc);
                source::XboxStats result;
                if (connected && !request->cancelled) {
                    {
                        std::lock_guard lock(mutex_);
                        if (request_ == request) generation_ = ++nextGeneration_;
                    }
                    uint64_t lastVideo = 0;
                    while (!stop.stop_requested() && !request->cancelled) {
                        result = session->stats();
                        if (result.state == source::XboxStats::State::Failed ||
                            result.state == source::XboxStats::State::Ended) break;
                        if (result.units > lastVideo) {
                            lastVideo = result.units;
                            const auto now = Clock::now().time_since_epoch();
                            budget.decoded(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() / 100);
                        }
                        std::unique_lock lock(mutex_);
                        wake_.wait_for(lock, stop, std::chrono::milliseconds(200), [&] { return request->cancelled.load(); });
                    }
                } else {
                    result = session->stats();
                }
                {
                    std::lock_guard lock(mutex_);
                    generation_ = 0;
                }
                session->close();
                budget.disconnected();
                const bool retry = result.reconnectable && budget.take();
                std::unique_lock lock(mutex_);
                session_.reset();
                wake_.notify_all();
                if (request != request_ || request->cancelled || stop.stop_requested()) break;
                failure_ = result;
                reconnecting_ = retry;
                if (!retry) break;
                const auto delay = std::chrono::seconds(1 << (budget.attempts() - 1));
                log::info("xbox-control", std::format("retry={} delaySec={} reason={}", budget.attempts(), delay.count(), result.disconnectReason));
                wake_.wait_for(lock, stop, delay, [&] { return request->cancelled.load(); });
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<Request> request_;
    std::shared_ptr<source::XboxSessionSource> session_;
    source::XboxStats failure_;
    uint64_t generation_ = 0, nextGeneration_ = 0;
    bool reconnecting_ = false;
    std::jthread worker_;
};

} // namespace veyra::engine
