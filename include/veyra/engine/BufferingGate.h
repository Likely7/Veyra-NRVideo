#pragma once

// 缓冲闸门：音画共用的一套「要不要一起等」判定。
//
// 背景（2026-11-01 现场）：B 站的音轨是一条独立的 CDN 请求，画面走 demux 会话。
// 音频那条连接断掉时，demux 侧的队列一直是满的，于是画面继续跑、声音没了 ——
// 用户看到的是「卡住 + 静音」，日志里只有音频侧自己的 underrun 计数。
// 视频侧本来就有 Phase::Buffering，音频侧没有任何对应信号，这个类补的是那半个信号。
//
// 纯逻辑，不碰线程、不碰网络、不看时钟：引擎每轮把两路水位和一个毫秒时刻喂进来，
// 它回答「现在该不该保持」，以及在保持太久时给出「重连」或「放弃」。
// Pure logic: no threads, no network, no clock of its own. The engine feeds the two
// water levels and a millisecond timestamp once per loop turn; the gate answers
// "should we hold now", and - when the hold lasts too long - "reconnect" or "give up".
namespace veyra::engine {

struct BufferingGateLimits {
    // 音频水位（毫秒）。跌破 enter 进入保持，回到 exit 才解除。没有这段滞回，
    // 阈值附近会一秒钟抖好几次，声音断断续续。
    //
    // exit 必须明显低于**稳态**水位，否则永远解不了：PCM 环的容量就是约 1 秒，
    // 稳态一直在 987…1019 之间抖，把 exit 定在 1000 就意味着"永远差一点点"。
    // 2026-11-01 现场：打开的瞬间 pcmMs=0 触发保持，之后 PCM 稳在 987 → 闸门卡死
    // 5 秒 → 误触发重连 → 声音彻底没了，"继续上次播放"看起来坏了。
    // The exit thresholds must sit well below the *steady* level. The PCM ring holds
    // about one second and idles at 987..1019 ms, so an exit of 1000 never clears.
    double enterPcmMs = 300.0;
    double enterQueuedMs = 500.0;
    double exitPcmMs = 500.0;
    double exitQueuedMs = 1500.0;
    // 打开之后的宽限期：这段时间里 PCM 还是 0（刚 open、还没 prefill），
    // 不判水位。没有它，每次播放一开始都会被判成"音频干涸"。
    // Grace period after opening: the PCM ring is still empty, so no water level is
    // judged. Without it every playback starts by declaring the sound dry.
    long long warmupMs = 2000;
    // 保持这么久还没恢复：这是连接断了，不是慢。
    long long reconnectAfterMs = 5000;
    // 一次播放允许重连几次；用完就放弃，把选择权交回用户。
    int reconnectBudget = 2;
};

class BufferingGate {
public:
    enum class Action { None, Reconnect, GiveUp };

    explicit BufferingGate(BufferingGateLimits limits = BufferingGateLimits{}) : limits_(limits) {}

    // 每轮调用一次。audioAvailable=false（本地文件没开音频、或音轨已被关掉）时只看视频侧，
    // 闸门自己保持「不保持」。
    Action update(bool audioAvailable, double pcmMs, double queuedMs, long long nowMs) {
        // 第一次调用记下起点，宽限期内一律不保持：此刻的 0 是"还没开始填"，不是"断了"。
        // The first call records the start; nothing is held during the grace period,
        // because a zero here means "not filled yet", not "run dry".
        if (!armed_) {
            armed_ = true;
            armedAtMs_ = nowMs;
            holding_ = false;
            gaveUp_ = false;
            sinceMs_ = nowMs;
        }
        if (nowMs - armedAtMs_ < limits_.warmupMs) {
            holding_ = false;
            return Action::None;
        }
        if (!audioAvailable) {
            holding_ = false;
            gaveUp_ = false;
            sinceMs_ = nowMs;
            return Action::None;
        }
        const bool low = holding_ ? (pcmMs < limits_.exitPcmMs || queuedMs < limits_.exitQueuedMs)
                                  : (pcmMs < limits_.enterPcmMs || queuedMs < limits_.enterQueuedMs);
        if (low && !holding_) {
            holding_ = true;
            sinceMs_ = nowMs;
            gaveUp_ = false;
        } else if (!low && holding_) {
            holding_ = false;
            gaveUp_ = false;
        }
        if (!holding_ || gaveUp_) return Action::None;
        if (nowMs - sinceMs_ < limits_.reconnectAfterMs) return Action::None;
        if (reconnects_ < limits_.reconnectBudget) {
            ++reconnects_;
            sinceMs_ = nowMs;   // 给重连一次新的等待窗口
            return Action::Reconnect;
        }
        gaveUp_ = true;
        return Action::GiveUp;
    }

    // 重连也失败时调用：解除保持、保留「已放弃」，让引擎通知用户一次之后画面还能继续
    // （静音）。只 clear 保持不 clear 放弃，是为了不把闸门打回重来一轮的起点。
    // Called when reopening failed as well: release the hold but keep "gave up", so the
    // engine can tell the user once and the picture is not frozen for good.
    void abandon() { holding_ = false; gaveUp_ = true; }

    // 跳转或换音轨之后调用：缓冲会先被清空再重新填，这段不该被当成「断了」。
    // 只重置宽限期与保持状态，**不动**重连计数 —— 否则一次断流可以被无限重连。
    // Called after a seek or a track change: the buffer empties before it refills and
    // that stretch must not read as a dead stream. The reconnect count is kept, so a
    // broken stream cannot buy itself an unlimited number of reconnects.
    void rearm(long long nowMs) {
        armed_ = true;
        armedAtMs_ = nowMs;
        holding_ = false;
        gaveUp_ = false;
        sinceMs_ = nowMs;
    }

    bool holding() const { return holding_; }
    bool gaveUp() const { return gaveUp_; }
    int reconnects() const { return reconnects_; }
    const BufferingGateLimits& limits() const { return limits_; }

private:
    BufferingGateLimits limits_{};
    bool armed_ = false;
    bool holding_ = false;
    bool gaveUp_ = false;
    int reconnects_ = 0;
    long long armedAtMs_ = 0;
    long long sinceMs_ = 0;
};

}  // namespace veyra::engine
