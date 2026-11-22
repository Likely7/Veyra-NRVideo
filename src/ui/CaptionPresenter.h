#pragma once
// When the translation of each on-screen line (live translation, on-screen text source)
// is shown and when it is taken down. Portable C++ (no Qt), so the timing is testable.
//
// Ported from VI Translator's presenter (https://github.com/vi-translator/vi-translator-desktop,
// src-tauri/src/main.rs, v1.2.3), MIT License, Copyright (c) 2026 Fouad Almalki.
// See THIRD_PARTY_NOTICES.md.
//  - a translation stays up as long as its original line was on screen, shifted by the
//    translation delay: it ends as long after the original as it started after it;
//  - a translation that comes after its line has already gone is still shown, for as long
//    as the line was up (without this, a translator slower than the dialogue showed nothing);
//  - the next line's translation waits until the current one has had its time; only the
//    newest waiting one is kept, so a slow translator never builds up a backlog;
//  - a line never outstays its original by more than maxLagMs.
// Veyra additions: every translation stays up at least minShowMs (time to read it), and a
// translation of a line that left the screen more than staleMs ago is dropped.
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <utility>

namespace veyra::ui {

template <class Payload>
class CaptionPresenter {
public:
    static constexpr int64_t maxLagMs = 3000;
    static constexpr int64_t staleMs = 8000;
    enum class Act { None, Show, Hide };

    // The translation of line `seq` (its original appeared at `start`) is ready at `now`.
    // False (dropped): a newer line is shown or waiting already, its line was cleared, or
    // it left the screen too long ago.
    bool ready(uint64_t seq, int64_t start, Payload payload, int64_t now, int64_t minShowMs = 1500) {
        if (seq <= clearedUpTo_) return false;
        if ((showing_ && seq <= showing_->seq) || (pending_ && seq <= pending_->seq)) return false;
        if (const auto end = ended_.find(seq); end != ended_.end() && now - end->second > staleMs) return false;
        pending_ = Line{seq, start, 0, std::max<int64_t>(0, minShowMs), std::move(payload)};
        return true;
    }
    // The original of line `seq` left the screen at `at` (gone, or replaced by the next line).
    void ended(uint64_t seq, int64_t at) {
        ended_[seq] = at;
        while (ended_.size() > 64) ended_.erase(ended_.begin());
    }
    // Take down what is shown now and drop every translation still on its way for lines
    // up to `upTo` (playback stopped, the source or the languages changed).
    bool clear(uint64_t upTo) {
        clearedUpTo_ = std::max(clearedUpTo_, upTo);
        pending_.reset();
        ended_.clear();
        return std::exchange(showing_, std::nullopt).has_value();
    }
    // What changes on screen now. After Show, shown() is the payload to display.
    Act tick(int64_t now) {
        if (showing_) {
            // The end of the shown line's original; not told yet but a newer line is waiting:
            // that one's original started after it had gone.
            std::optional<int64_t> end;
            if (const auto e = ended_.find(showing_->seq); e != ended_.end()) end = e->second;
            else if (pending_ && pending_->seq > showing_->seq) end = pending_->start;
            if (!end) return Act::None;
            const int64_t lag = std::clamp<int64_t>(showing_->shownAt - showing_->start, 0, maxLagMs);
            const int64_t lasted = std::max<int64_t>(0, *end - showing_->start);
            const int64_t until = std::max(std::max(*end, showing_->shownAt) + std::min(lag, lasted),
                                           showing_->shownAt + showing_->minShowMs);
            if (now < until) return Act::None;
        }
        if (pending_) {
            showing_ = std::move(*pending_);
            showing_->shownAt = now;
            pending_.reset();
            ended_.erase(ended_.begin(), ended_.lower_bound(showing_->seq));
            return Act::Show;
        }
        if (showing_) {
            showing_.reset();
            return Act::Hide;
        }
        return Act::None;
    }
    const Payload* shown() const { return showing_ ? &showing_->payload : nullptr; }
    bool waiting() const { return pending_.has_value(); }

private:
    struct Line {
        uint64_t seq = 0;
        int64_t start = 0, shownAt = 0, minShowMs = 0;
        Payload payload{};
    };
    std::optional<Line> showing_, pending_;
    std::map<uint64_t, int64_t> ended_;
    uint64_t clearedUpTo_ = 0;
};

} // namespace veyra::ui
