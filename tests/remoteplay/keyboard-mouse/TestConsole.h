#pragma once
#include "veyra/remoteplay/KeyboardMouse.h"
#include "veyra/source/RemotePlaySessionSource.h"
#include <condition_variable>
#include <stdexcept>
#include <thread>
#include <vector>

namespace veyra::remoteplay::test {
struct TestConsole {
    bool connect(source::RemotePlaySessionSource& source, bool captureCard, bool viewOnly);
    std::shared_ptr<KeyboardMouse> feedback;
    std::thread::id owner;
    bool discardMedia = false;
    std::vector<ControllerState> sent;
    std::mutex mutex;
    std::condition_variable changed;
    bool stall = false, stalled = false;

    size_t mark() {
        std::lock_guard lock(mutex);
        return sent.size();
    }

    template<class Predicate>
    ControllerState expect(size_t& next, Predicate match, const char* message,
                           std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
        std::unique_lock lock(mutex);
        ControllerState result;
        if (!changed.wait_for(lock, timeout, [&] {
            while (next < sent.size()) {
                result = sent[next++];
                if (match(result)) return true;
            }
            return false;
        })) throw std::runtime_error(message);
        return result;
    }

    void stallSender() {
        std::unique_lock lock(mutex);
        stall = true;
        if (!changed.wait_for(lock, std::chrono::seconds(1), [&] {return stalled;}))
            throw std::runtime_error("sender did not reach Chiaki");
    }

    void resumeSender() {
        std::lock_guard lock(mutex);
        stall = false;
        changed.notify_all();
    }
};
extern TestConsole console;
}
