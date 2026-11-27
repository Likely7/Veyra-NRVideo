#pragma once

#include <cstdint>

namespace veyra::ui {

// At most one automatic startup request per process. A delayed capture query
// or stream launch carries its origin explicitly until it creates a session.
class StartupFullscreenRequest {
public:
    bool arm() noexcept {
        if (state_ != State::Available) return false;
        state_ = State::Pending;
        return true;
    }

    bool awaitingSession() const noexcept { return state_ == State::Pending && !session_; }
    bool hasSession() const noexcept { return session_ != 0; }

    void bind(std::uint64_t session) noexcept {
        if (awaitingSession()) session_ = session;
    }

    void cancel() noexcept {
        state_ = State::Finished;
        session_ = 0;
    }

    bool take(std::uint64_t session, bool terminal, bool playing, std::uint64_t presentedSession) noexcept {
        if (!session_ || session < session_) return false; // a poll can precede the open
        const bool ready = session == session_ && !terminal && playing && presentedSession == session_;
        if (ready || terminal || session != session_) cancel();
        return ready;
    }

private:
    enum class State { Available, Pending, Finished };
    State state_ = State::Available;
    std::uint64_t session_ = 0;
};

} // namespace veyra::ui
