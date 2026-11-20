// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS include/veyra/engine/PushAdaptiveFg.h (commit 3be641ca) and adapted: renamed and wired
// into Veyra's settings, engine and interface.
#pragma once
#include "veyra/engine/DisplayFgBudget.h"
#include <cmath>
#include <cstdint>

namespace veyra::engine {
// Adaptive frame generation multiplier (EnhancementSettings::fgAdaptive).
//
// Console games rarely hold one frame rate: a "30 FPS" game dips to 25, a 40 FPS
// mode wobbles between 35 and 45, an unlocked game runs anywhere from 30 to 60.
// With a fixed multiplier the output follows every dip. Adaptive mode treats the
// chosen multiplier as a ceiling and, per source pair, picks the multiplier whose
// output lands closest to the target (the output cap, or the display refresh),
// the same integer-switching idea as NVIDIA's DLSS 4.5 Dynamic Multi Frame
// Generation, but driven by the measured content rate of the console picture
// (ContentDedup) and usable with every multi-frame backend.
//
// Why integers per pair: the graph already evaluates any multiplier from 2X up to
// the one it was built for on every pair (EnhanceGraph::process previewMultiplier,
// the DLSS/ANR multiFrameCount and the VFG subframe phase both follow it), which
// Veyra exercises on real hardware with its output-cap reduction and its reduced
// 2X groups. Fractional rates would need interpolation at arbitrary phases, which
// DLSS frame generation does not offer.
//
// Choice: among the backend's selectable multipliers from 2 to the ceiling, the
// one whose output content x M is closest to the target on a logarithmic scale
// (10 % over counts the same as 10 % under), lower on a tie. A lower multiplier
// also means less delay: graph FG holds a real frame (M-1)/M of a source interval.
// Examples at a 120 FPS target: 30 -> 4X, 40 -> 3X, 36 -> 3X (108), 33 -> 4X (132),
// 25 -> 4X on DLSS (100; 6X would be 150), 5X on ANR/VFG (125).
inline unsigned adaptiveFgMultiplier(FrameGenerationBackend backend,unsigned ceiling,double contentFps,double targetFps){
    if(ceiling<2)return ceiling;
    if(!std::isfinite(contentFps)||!std::isfinite(targetFps)||contentFps<=0||targetFps<=0)return ceiling;
    unsigned best=0;double bestError=0;
    for(unsigned m=2;m<=ceiling;++m){
        if(!displayFgMultiplierSelectable(backend,m))continue;
        const double error=std::abs(std::log(contentFps*m/targetFps));
        if(best==0||error<bestError-1e-9){best=m;bestError=error;}
    }
    return best?best:ceiling;
}

// Keeps a multiplier change from flickering when the content rate sits right on a
// boundary (a 34-35 FPS game at 120 FPS): a new choice applies only after it held
// for the hold time (0.5 s by default; the content rate itself is measured over up
// to 0.75 s). The first choice applies at once. Units: 100 ns.
class AdaptiveFgGate {
public:
    explicit AdaptiveFgGate(int64_t hold100ns=5000000):gate_(hold100ns){}
    void reset(){gate_.reset();}
    // Returns the multiplier to use now; changed is set when it switched.
    unsigned update(unsigned candidate,int64_t now,bool* changed=nullptr){
        const bool c=gate_.update(candidate,now);
        if(changed)*changed=c;
        return gate_.applied();
    }
    bool valid()const{return gate_.valid();}
    unsigned applied()const{return gate_.applied();}
private:
    DisplayFgBudgetGate gate_;
};
}
