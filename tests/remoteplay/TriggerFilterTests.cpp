// Standalone checks for the Remote Play L2/R2 filter. No SDL, console or Chiaki.
#include "veyra/remoteplay/TriggerFilter.h"
#include <cstdio>
#include <cstdlib>
#include <deque>

using veyra::remoteplay::HostTime;
using veyra::remoteplay::TriggerFilter;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
constexpr HostTime ms(int v) { return HostTime(v) * 10000; }

// The scenario from the report, driven through a model of Chiaki's history
// window: each value change is one event, a packet repeats the newest 8, and a
// burst of packets is lost right after L2 is released while R2 keeps jittering.
// Returns true if the console ends up seeing L2 released.
template <class Source>
bool consoleSeesL2Release(Source source) {
    struct Event { int trigger; int value; };
    std::deque<Event> window;          // newest 8 events, as in every packet
    int consoleL2 = 0;
    std::uint8_t prevL2 = 0, prevR2 = 0;
    for (int tick = 0; tick < 150; ++tick) { // 4 ms controller cadence, 600 ms
        const HostTime now = ms(tick * 4);
        std::uint8_t l2 = tick < 50 ? 200 : 0;                 // L2 released at 200 ms
        std::uint8_t r2 = std::uint8_t(180 + (tick % 3) - 1);  // R2 held, jittering 179..181
        auto out = source(l2, r2, now);
        bool pushed = false;
        auto push = [&](int t, int v) {
            window.push_back({t, v});
            if (window.size() > 8) window.pop_front();
            pushed = true;
        };
        if (out.l2 != prevL2) push(0, out.l2);
        if (out.r2 != prevR2) push(1, out.r2);
        prevL2 = out.l2; prevR2 = out.r2;
        const bool lost = tick >= 50 && tick < 60;            // 40 ms of lost packets
        // A packet goes out for each new event; with nothing new, the last
        // packet is resent (Chiaki tail resend at 20/50/100/200 ms).
        const bool tailResend = !pushed && (tick == 55 || tick == 62 || tick == 75 || tick == 100);
        if ((pushed || tailResend) && !lost)
            for (const auto& e : window) if (e.trigger == 0) consoleL2 = e.value;
    }
    return consoleL2 == 0;
}
} // namespace

int main() {
    {
        TriggerFilter f;
        auto v = f.apply(3, 252, 0);
        check(v.l2 == 0 && v.r2 == 255, "ends snap to 0 and 255");
    }
    {
        TriggerFilter f;
        f.apply(100, 0, 0);
        auto a = f.apply(102, 0, ms(4));
        auto b = f.apply(104, 0, ms(8));
        check(a.l2 == 100 && b.l2 == 104, "in-between changes below the step are held, a full step is sent");
    }
    {
        TriggerFilter f;
        f.apply(150, 0, 0);
        auto v = f.apply(0, 0, ms(4));
        check(v.l2 == 0, "release is sent at once from any value");
        v = f.apply(40, 0, ms(8));
        check(v.l2 == 40, "first press is sent at once");
    }
    {
        TriggerFilter f;
        f.apply(200, 120, 0);
        f.apply(0, 120, ms(4));                       // L2 released
        auto held = f.apply(0, 125, ms(40));
        check(held.r2 == 120, "nearby noise is held after a release");
        auto changed = f.apply(0, 160, ms(50));
        check(changed.r2 == 160, "real analog travel continues after releasing the other trigger");
        auto full = f.apply(0, 255, ms(60));
        check(full.r2 == 255, "other trigger reaching 255 still goes through during the hold");
        auto after = f.apply(0, 160, ms(200));
        check(after.r2 == 160, "held trigger catches up after the hold");
    }
    {
        TriggerFilter f;
        f.apply(0, 0, 0);
        f.apply(30, 0, ms(4));
        auto ramp = f.apply(90, 0, ms(8));
        check(ramp.l2 == 90, "the trigger being pressed is never held");
    }
    {
        TriggerFilter f;
        f.apply(200, 200, 0);
        f.reset();
        auto v = f.apply(100, 0, ms(4));
        check(v.l2 == 100, "reset starts from released triggers");
    }
    {
        auto rawSource = [](std::uint8_t l2, std::uint8_t r2, HostTime) {
            return TriggerFilter::Values{l2, r2};
        };
        TriggerFilter f;
        auto filtered = [&f](std::uint8_t l2, std::uint8_t r2, HostTime now) { return f.apply(l2, r2, now); };
        check(!consoleSeesL2Release(rawSource), "model reproduces the stuck L2 without the filter");
        check(consoleSeesL2Release(filtered), "with the filter the console sees the L2 release");
    }
    std::printf(failures ? "FAILED %d\n" : "ALL PASSED\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
