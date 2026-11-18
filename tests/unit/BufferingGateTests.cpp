#include "veyra/engine/BufferingGate.h"

#include <cstdio>

using veyra::engine::BufferingGate;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

namespace {
// 单元测试只关心水位与超时的判定，宽限期单独测。
BufferingGate makeGate() {
    veyra::engine::BufferingGateLimits limits;
    limits.warmupMs = 0;
    return BufferingGate(limits);
}
}  // namespace

int main() {
    // 刚打开、PCM 还是 0：宽限期内不判水位（现场 2026-11-01 的误判就是这里）。
    {
        BufferingGate g;   // 默认 2000 ms 宽限
        check(g.update(true, 0.0, 0.0, 0) == BufferingGate::Action::None, "warm-up: an empty ring right after open is not dryness");
        check(!g.holding(), "warm-up: nothing is held while the ring fills");
        g.update(true, 0.0, 0.0, 1999);
        check(!g.holding(), "warm-up: still filling one millisecond before the grace period ends");
        g.update(true, 0.0, 0.0, 2100);
        check(g.holding(), "warm-up: past the grace period a dry ring does hold");
    }
    // PCM 环的稳态约 1 秒（实测 987…1019）：恢复阈值必须低于它，否则永远解不了。
    {
        BufferingGate g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        check(g.holding(), "a dry start holds");
        g.update(true, 987.0, 2517.0, 100);
        check(!g.holding(), "the steady PCM level (987 ms) releases the hold");
    }
    // 稳态不保持。
    {
        auto g = makeGate();
        check(g.update(true, 1000.0, 2517.0, 0) == BufferingGate::Action::None, "steady levels never ask for an action");
        check(!g.holding(), "steady levels do not hold");
    }
    // 跌破进入阈值就保持，但还没到重连的时候。
    {
        auto g = makeGate();
        check(g.update(true, 250.0, 400.0, 1000) == BufferingGate::Action::None, "entering the hold is not an action");
        check(g.holding(), "below the enter thresholds the gate holds");
    }
    // 滞回：回到 enter 与 exit 之间仍然保持。
    {
        auto g = makeGate();
        g.update(true, 250.0, 400.0, 0);
        g.update(true, 800.0, 1200.0, 1000);
        check(g.holding(), "a partial recovery between enter and exit keeps holding");
    }
    // 两路都回到 exit 才解除。
    {
        auto g = makeGate();
        g.update(true, 250.0, 400.0, 0);
        g.update(true, 1100.0, 1600.0, 1000);
        check(!g.holding(), "reaching both exit thresholds releases the hold");
    }
    // 单路低也进入保持：预读队列断、PCM 还剩一点。
    {
        auto g = makeGate();
        g.update(true, 900.0, 100.0, 0);
        check(g.holding(), "a dry read-ahead queue alone is enough to hold");
    }
    // 5 秒窗口、重连预算、放弃。
    {
        auto g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        check(g.update(true, 100.0, 100.0, 4999) == BufferingGate::Action::None, "nothing happens before 5 s");
        check(g.update(true, 100.0, 100.0, 5000) == BufferingGate::Action::Reconnect, "5 s of dryness asks for a reconnect");
        check(g.reconnects() == 1, "the reconnect is counted");
        check(g.update(true, 100.0, 100.0, 9000) == BufferingGate::Action::None, "a reconnect restarts the 5 s window");
        check(g.update(true, 100.0, 100.0, 10000) == BufferingGate::Action::Reconnect, "the budget allows a second reconnect");
        check(g.update(true, 100.0, 100.0, 15000) == BufferingGate::Action::GiveUp, "a spent budget gives up");
        check(g.gaveUp(), "giving up is sticky");
        check(g.update(true, 100.0, 100.0, 60000) == BufferingGate::Action::None, "a given-up gate stops asking");
    }
    // 重连后数据回来：解除保持，重连计数保留。
    {
        auto g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        g.update(true, 100.0, 100.0, 5000);
        check(g.update(true, 1100.0, 1600.0, 6000) == BufferingGate::Action::None, "recovery after a reconnect is silent");
        check(!g.holding() && !g.gaveUp(), "recovery clears both hold and give-up");
        check(g.reconnects() == 1, "recovery keeps the reconnect count");
    }
    // 重连也失败：解除保持、保留「已放弃」—— 引擎据此提示一次，画面不再被冻住。
    {
        auto g = makeGate();
        g.update(true, 100.0, 100.0, 0);
        g.update(true, 100.0, 100.0, 5000);
        g.update(true, 100.0, 100.0, 10000);
        check(g.update(true, 100.0, 100.0, 15000) == BufferingGate::Action::GiveUp, "a third dry window gives up");
        g.abandon();
        check(!g.holding() && g.gaveUp(), "abandon releases the hold but keeps giving up");
    }
    // 跳转或换音轨之后重新宽限：清空再填不算断流，但重连计数不重置。
    {
        BufferingGate g;   // 默认 2000 ms 宽限
        g.update(true, 1000.0, 2517.0, 0);
        g.rearm(1000);
        check(g.update(true, 0.0, 0.0, 1500) == BufferingGate::Action::None, "rearm: an empty ring after a seek is not dryness");
        check(!g.holding(), "rearm: nothing is held while the ring refills");
        g.update(true, 0.0, 0.0, 3100);
        check(g.holding(), "rearm: past the grace period a dry ring holds again");
    }
    // 没有音频（本地文件没开音轨、或音轨已被关掉）时不保持。
    {
        auto g = makeGate();
        check(g.update(false, 0.0, 0.0, 0) == BufferingGate::Action::None, "no audio means no action");
        check(!g.holding(), "no audio never holds");
    }
    // 阈值可以从外面改：现场按网络调参。
    {
        veyra::engine::BufferingGateLimits limits;
        limits.warmupMs = 0;
        limits.enterPcmMs = 150.0;
        limits.exitPcmMs = 400.0;
        BufferingGate g(limits);
        g.update(true, 200.0, 5000.0, 0);
        check(!g.holding(), "a lowered enter threshold ignores 200 ms of PCM");
        g.update(true, 100.0, 5000.0, 1000);
        check(g.holding(), "the lowered threshold still holds at 100 ms");
    }

    if (failures) std::printf("buffering gate: %d failure(s)\n", failures);
    else std::printf("buffering gate: all checks passed\n");
    return failures ? 1 : 0;
}
