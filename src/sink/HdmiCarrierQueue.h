#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace veyra::sink::detail {

// Access is serialized by the output's mutex. The in-flight burst belongs
// to the render cursor and is never discarded by a producer-side overflow.
class HdmiCarrierQueue {
public:
    size_t push(std::vector<uint8_t> burst, size_t capacity) {
        size_t dropped = 0;
        while (!bursts_.empty() && bytes_ + burst.size() > capacity) {
            bytes_ -= bursts_.front().size();
            bursts_.pop_front();
            ++dropped;
        }
        bytes_ += burst.size();
        bursts_.push_back(std::move(burst));
        return dropped;
    }
    void clear() {
        bursts_.clear();
        bytes_ = 0;
    }
    size_t bytes() const { return bytes_; }

    struct Cursor {
        std::vector<uint8_t> burst;
        size_t offset = 0;
        bool priming = true;
    };

    size_t fill(Cursor& cursor, std::span<uint8_t> output, size_t prebufferBytes) {
        while (!output.empty()) {
            if (cursor.offset == cursor.burst.size()) {
                if (bursts_.empty()) cursor.priming = true;
                if (cursor.priming && bytes_ < prebufferBytes) break;
                if (bursts_.empty()) break;
                cursor.priming = false;
                cursor.burst = std::move(bursts_.front());
                bursts_.pop_front();
                bytes_ -= cursor.burst.size();
                cursor.offset = 0;
            }
            const size_t count = std::min(output.size(), cursor.burst.size() - cursor.offset);
            std::copy_n(cursor.burst.data() + cursor.offset, count, output.data());
            cursor.offset += count;
            output = output.subspan(count);
        }
        std::ranges::fill(output, uint8_t(0));
        return output.size();
    }

private:
    std::deque<std::vector<uint8_t>> bursts_;
    size_t bytes_ = 0;
};

} // namespace veyra::sink::detail
