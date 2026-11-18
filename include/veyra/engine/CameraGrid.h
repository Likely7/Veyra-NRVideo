// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>

namespace veyra::engine {
// Which presented frames the virtual camera takes, and the time it stamps them
// with (2026-11-17). Veyra presents 60-240 frames a second (frame generation
// 2X-4X); the camera takes the frame nearest each point of its own even grid at
// the consumer's rate and stamps it with that grid point, so the camera module
// can hand frames out evenly whatever the presentation jitter.
//
// A frame counts for the next grid point from `edge` before it, where edge is
// 45 % of the shorter of the camera interval and the presented-frame interval:
// the frame before is then always outside, even at 240 presented frames a second
// (a fixed quarter of the camera interval sat exactly on that frame there). A
// frame nearer a later point moves the grid on; a long gap starts it again. The
// grid follows the presentation timing slowly (1/16 per frame), so drift between
// the display's rhythm and the clock never walks frames onto the edge. Simulated
// over 60 s with +-1 ms presentation jitter, every integer ratio (60-240 frames
// into 60 or 120) takes exactly every n-th frame. Times: 100 ns, one clock
// (video_queue_now_100ns).
struct CameraGrid {
    int64_t next = 0;   // the next grid point; 0 = none yet
    int64_t last = 0;   // the previous presented frame
    int64_t period = 0; // presented-frame interval, smoothed
    // The grid time for a frame presented at `now`, or -1: skip it.
    int64_t take(int64_t now, int64_t interval) {
        if (interval <= 0) return -1;
        if (last && now > last && now - last < interval * 4) period = period ? period + (now - last - period) / 8 : now - last;
        last = now;
        const int64_t edge = period ? std::min(interval, period) * 9 / 20 : interval / 4;
        if (next && (now - next > interval * 4 || next - now > interval * 4)) next = 0; // a pause or a stall
        if (!next) next = now;
        if (now >= next + interval - edge) next += (now - next + edge) / interval * interval;
        if (now + edge < next) return -1; // this frame's grid point is already taken
        const int64_t at = next;
        next = at + interval + (now - at) / 16;
        return at;
    }
    void reset() { next = last = period = 0; }
};
}
