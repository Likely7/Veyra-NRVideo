// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <cmath>
#include <cstdint>

namespace veyra::engine {
// Live frame-generation output hint (claude/latency-min-20261013).
//
// A panel shows at most one new picture per refresh. Generated pictures above
// that rate are replaced before scanout (Automatic) or dropped/waited for
// (VSync), and every extra multiplier step makes the real frame wait longer:
// graph FG shows it (M-1)/M of a source interval after it arrived. The
// multiplier stays the user's choice (decision 2026-10-14: no automatic
// lowering); for live sources the status line suggests a better one:
//   1. the output fits the refresh (content x M <= 1.02 x refresh): nothing;
//   2. the source alone already fills the panel (>= 0.97 x refresh, e.g.
//      60 fps on 60 Hz): turn generation off - no generated picture can add a
//      refresh, and real frames would present without waiting behind one;
//   3. otherwise the largest selectable lower multiplier whose output fits;
//   4. if none fits but the source is slower than the panel (60 fps on a
//      100 Hz panel): Automatic still shows more distinct pictures per refresh
//      with 2X, so suggest 2X only when more was requested; under VSync the
//      surplus waits or is dropped, so suggest turning generation off.
inline constexpr double kDisplayFgFitTolerance=1.02;
inline constexpr double kDisplayFgFillRatio=0.97;

// Multipliers the user can pick for the backend (fgMultiplierChoices: XeSS
// stops at 4X, FSR offers 2X only). Lower values than an accepted request
// are always within the runtime limits.
constexpr bool displayFgMultiplierSelectable(FrameGenerationBackend backend,unsigned multiplier){
    switch(backend){
    case FrameGenerationBackend::Dlss: return multiplier==2||multiplier==3||multiplier==4||multiplier==6;
    case FrameGenerationBackend::XeSS: return multiplier>=2&&multiplier<=4;
    case FrameGenerationBackend::Vfg: return multiplier>=2&&multiplier<=8;
    case FrameGenerationBackend::Fsr:
    case FrameGenerationBackend::Fsr4: return multiplier==2;
    }
    return false;
}

// 「自动」倍率：按源帧率和目标上限挑档位。
// 用户定义（2026-10-24）：24fps 配 120 上限选 5X，30fps 选 4X，以此类推；
// 后端没有该档时向上取（DLSS 无 5X，24fps 就选 6X=144，再由输出上限裁回 120）。
// 与 displayFgSuggestion 的区别：那条只往下建议、永不往上；这里允许往上取档。
// 容差沿用 kDisplayFgFitTolerance（2%）：23.976×5=119.88 视作已达 120，选 5X 而非 6X。
// ceiling：该后端此刻真正可选的最大倍率（显卡能力、DLSS 6 / VFG 8 / FSR 2）。
// 与 FrameBatch::Capacity 一致，倍率的绝对上限是 8（一个批最多 1 真帧 + 7 生成帧）。
inline constexpr unsigned kDisplayFgMultiplierCeiling=8;
// 连最低档（2X）都超出上限时，只有源帧率离上限足够远才值得补：增益 = 上限-源，
// 浪费 = 2×源-上限。两者相等时源 = 上限×2/3，超过这个比例就不补了
// （上限 120 / 源 100、上限 144 / 源 100 都落在这一侧 → 关闭）。
// 这是唯一需要"省 GPU"判断的地方：2X 都装不下时，补出来的大半会被裁掉。
inline constexpr double kDisplayFgAutoCloseAbove=2.0/3.0;
// 返回 0 = 帧率未知/无可用档位（调用方保持原档），1 = 关闭补帧，其余为倍率。
//
// 选档规则（用户 2026-10-24 定稿）：在自身可行档位里取「够得着 need 的最小档」，
// 即让源帧率 × 倍率 刚好打满输出上限；后端没有该档时向上取。不额外做"省 GPU"的
// 降档 —— 24/100 该给 5X（120 帧，超出 20 帧由上限裁掉），100/240 该给 3X，
// 都是公式的自然结果，用户也明确要求这两格按公式走。
inline unsigned displayFgAutoMultiplier(FrameGenerationBackend backend,double contentFps,double targetFps,unsigned ceiling=kDisplayFgMultiplierCeiling){
    if(!std::isfinite(contentFps)||!std::isfinite(targetFps)||contentFps<=0||targetFps<=0||ceiling<2)return 0;
    // 源帧率本身已达/超过上限：补帧没有增益，关闭。
    if(contentFps>=targetFps/kDisplayFgFitTolerance)return 1;
    const double doubled=contentFps*2.0;
    // 连 2X 都放不进上限：只有源帧率远低于上限时才值得补，否则关掉
    // （补了多半是被裁掉的帧，白耗 GPU）。
    if(displayFgMultiplierSelectable(backend,2)&&doubled>targetFps*kDisplayFgFitTolerance){
        if(contentFps>targetFps*kDisplayFgAutoCloseAbove)return 1;
        return 2u;
    }
    const auto need=unsigned(std::ceil(targetFps/(contentFps*kDisplayFgFitTolerance)-1e-9));
    unsigned best=0;
    for(unsigned m=2;m<=ceiling&&m<=kDisplayFgMultiplierCeiling;++m){
        if(!displayFgMultiplierSelectable(backend,m))continue;
        best=m;                 // 递增遍历，最后一个可行档即最大可用档
        if(m>=need)return m;    // 第一个够得着 need 的档即最小够用档
    }
    // 没有档位够得到 need（例如 DLSS 上限 6 对 24fps/200 上限）：取最大可用档，
    // 输出上限仍会把多出来的帧裁回目标值。
    return best;
}

// 0 = no suggestion, 1 = turn generation off, otherwise a lower multiplier.
// queued: presents above the refresh wait or are dropped (VSync) instead of
// being replaced (Automatic). Unknown rates suggest nothing.
inline unsigned displayFgSuggestion(FrameGenerationBackend backend,unsigned requested,double contentFps,double refreshHz,bool queued=false){
    if(requested<2||!std::isfinite(contentFps)||!std::isfinite(refreshHz)||contentFps<=0||refreshHz<=0)return 0;
    if(contentFps*requested<=refreshHz*kDisplayFgFitTolerance)return 0;
    if(contentFps>=refreshHz*kDisplayFgFillRatio)return 1;
    for(unsigned m=requested-1;m>=2;--m)
        if(displayFgMultiplierSelectable(backend,m)&&contentFps*m<=refreshHz*kDisplayFgFitTolerance)return m;
    if(queued)return 1;
    return requested>2&&displayFgMultiplierSelectable(backend,2)?2:0;
}

// A changed suggestion shows only after it held for the whole hold time, so a
// content rate wobbling around a boundary (30/40 fps games, the dedup
// estimate) or a brief refresh change never makes the hint flicker; a
// candidate that changes again restarts the hold. The first decision after
// reset() applies at once. Units: 100 ns.
class DisplayFgBudgetGate {
public:
    explicit DisplayFgBudgetGate(int64_t hold100ns=15000000):hold_(hold100ns){}
    void reset(){valid_=pendingValid_=false;}
    // Returns true when the applied decision changed.
    bool update(unsigned decision,int64_t now){
        if(!valid_){applied_=decision;valid_=true;pendingValid_=false;return true;}
        if(decision==applied_){pendingValid_=false;return false;}
        if(!pendingValid_||pending_!=decision){pending_=decision;since_=now;pendingValid_=true;return false;}
        if(now-since_<hold_)return false;
        applied_=decision;pendingValid_=false;return true;
    }
    bool valid()const{return valid_;}
    unsigned applied()const{return applied_;}
private:
    int64_t hold_,since_=0;
    unsigned applied_=0,pending_=0;
    bool valid_=false,pendingValid_=false;
};
}
