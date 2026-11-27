#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>

namespace veyra::engine {
// Capture and content clocks can differ just enough for a repeated picture
// to occupy a third sample. Keep its measured lead on the content clock in
// the FG phase through the next beat. Based on the 2.2.1 capture timing
// contribution from @George (UGREEN CM630 + PS5 Pro, 2026-11-27).
class LiveContentLagReserve {
    static constexpr int64_t kSecond100ns = 10000000;
    static constexpr std::size_t kBuckets = 30;
    std::array<int64_t, kBuckets> peaks_{};
    int64_t lastTime_ = 0, elapsed_ = 0, peak_ = 0;
    std::size_t head_ = 0;
    bool held_ = false;

public:
    void reset() { *this = {}; }
    void hold(int64_t now) {
        lastTime_ = now;
        held_ = true;
    }
    void observe(int64_t now, int64_t lag, int64_t transport) {
        const auto delta = !held_ && lastTime_ > 0 ? std::max<int64_t>(0, now - lastTime_) : 0;
        lastTime_ = now;
        held_ = false;
        const auto buckets = (elapsed_ + delta) / kSecond100ns;
        elapsed_ = (elapsed_ + delta) % kSecond100ns;
        if (buckets >= int64_t(kBuckets)) {
            peaks_.fill(0);
            peak_ = 0;
        } else if (buckets > 0) {
            for (int64_t i = 0; i < buckets; ++i) {
                head_ = (head_ + 1) % kBuckets;
                peaks_[head_] = 0;
            }
            peak_ = *std::max_element(peaks_.begin(), peaks_.end());
        }
        // Sub-slot jitter already fits the regular phase. Cap the reserve at
        // one slot with a little room for transport jitter during a beat.
        if (transport > 0 && lag > transport / 4) {
            const auto sample = std::min(lag, transport + transport / 10);
            peaks_[head_] = std::max(peaks_[head_], sample);
            peak_ = std::max(peak_, sample);
        }
    }
    int64_t peak() const { return peak_; }
};
}
