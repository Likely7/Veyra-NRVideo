// NR inference reuse: frame selection, the Auto controller and the settings
// contract (docs/NR_INFERENCE_REUSE_2026-11-07.md). GPU-free.
#include "veyra/engine/AutoNrReuseController.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/pipeline/NrReuse.h"
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(const std::string& name, bool ok)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << std::endl;
    if (!ok) ++failures;
}
// 'K' inferred, 'R' reused, for `frames` frames at `level`.
std::string pattern(unsigned level, unsigned frames, bool available = true)
{
    veyra::pipeline::NrReuseScheduler s;
    s.setLevel(level);
    std::string out;
    for (unsigned i = 0; i < frames; ++i) out += s.next(available) ? 'R' : 'K';
    return out;
}
}

int main()
{
    using namespace veyra;
    using pipeline::NrReuseScheduler;
    using engine::NrReuseMode;

    // --- frame selection ----------------------------------------------------
    check("level 0 infers every frame", pattern(0, 12) == "KKKKKKKKKKKK");
    check("1/2 alternates", pattern(3, 8) == "KRKRKRKR");
    check("1/3 reuses one frame in three", pattern(2, 9) == "KKRKKRKKR");
    check("1/4 reuses one frame in four", pattern(1, 12) == "KKKRKKKRKKKR");
    check("no stored inference: never reuses", pattern(3, 6, false) == "KKKKKK");
    check("manual modes pin their levels",
          NrReuseScheduler::levelFor(NrReuseMode::Half) == 3 && NrReuseScheduler::levelFor(NrReuseMode::Third) == 2 &&
          NrReuseScheduler::levelFor(NrReuseMode::Quarter) == 1 && NrReuseScheduler::levelFor(NrReuseMode::Auto) == 0 &&
          NrReuseScheduler::levelFor(NrReuseMode::Off) == 0);
    check("frames per reuse", NrReuseScheduler::framesPerReuse(3) == 2 && NrReuseScheduler::framesPerReuse(2) == 3 &&
          NrReuseScheduler::framesPerReuse(1) == 4 && NrReuseScheduler::framesPerReuse(0) == 0);
    {
        // A reused frame is always followed by an inferred one, at any level
        // and whatever the availability pattern.
        bool never = true;
        for (unsigned level = 0; level <= 3; ++level) {
            NrReuseScheduler s;
            s.setLevel(level);
            bool last = false;
            for (unsigned i = 0; i < 200; ++i) {
                const bool reused = s.next(i % 7 != 3);
                if (reused && last) never = false;
                last = reused;
            }
        }
        check("never two reused frames in a row", never);
    }
    {
        NrReuseScheduler s;
        s.setLevel(3);
        std::string out;
        for (unsigned i = 0; i < 4; ++i) out += s.next(true) ? 'R' : 'K';
        s.reset();
        for (unsigned i = 0; i < 4; ++i) out += s.next(true) ? 'R' : 'K';
        check("a reset makes the next frame infer and restarts the pattern", out == "KRKRKRKR");
        NrReuseScheduler t;
        t.setLevel(3);
        out.clear();
        out += t.next(true) ? 'R' : 'K';
        t.reset();
        out += t.next(true) ? 'R' : 'K';
        out += t.next(true) ? 'R' : 'K';
        check("reset between an inferred frame and its reuse delays the reuse", out == "KKR");
    }
    {
        NrReuseScheduler s;
        s.setLevel(9);
        check("level clamps to the maximum", s.level() == NrReuseScheduler::kMaxLevel);
    }
    {
        // Only the inferred frame right before a possible reuse keeps its chain.
        for (unsigned level = 1; level <= 3; ++level) {
            NrReuseScheduler s;
            s.setLevel(level);
            std::string keeps, frames;
            for (unsigned i = 0; i < 12; ++i) {
                const bool reused = s.next(true);
                frames += reused ? 'R' : 'K';
                keeps += !reused && s.reuseMayFollow() ? 's' : '-';
            }
            // Every reused frame is preceded by a kept inferred frame, and a kept
            // frame is always followed by a reuse when nothing interferes.
            bool consistent = true;
            for (unsigned i = 0; i + 1 < 12; ++i) consistent &= (keeps[i] == 's') == (frames[i + 1] == 'R');
            check(std::string("level ") + char('0' + level) + " keeps exactly the frames a reuse follows (" + frames + " " + keeps + ")", consistent);
        }
        NrReuseScheduler idle;
        idle.next(true);
        check("level 0 never keeps a chain", !idle.reuseMayFollow());
    }

    // --- Auto controller ----------------------------------------------------
    {
        engine::AutoNrReuseController c;
        const int64_t second = 10000000;
        c.reset(0);
        // 60 fps transport: budget 13.333 ms. Enhancement mean 18 ms with NR 10 ms.
        std::optional<unsigned> change;
        int64_t t = 0;
        for (; t <= 3 * second && !change; t += second / 4) change = c.observe(t, 18.0, 60, 10.0, 13.333);
        check("over budget raises the level within ~2 s", change && *change == 1 && t <= 2 * second);
        // At level 1 the mean drops; still over: keeps raising, one step at a time.
        change.reset();
        const int64_t start = t;
        for (; t <= start + 3 * second && !change; t += second / 4) change = c.observe(t, 15.5, 60, 10.0, 13.333);
        check("still over budget raises again after settling", change && *change == 2 && t - start >= second);
        // Comfortably under budget, but one level down would add (1/3-1/4)*10 ms:
        // 9 + 0.83 = 9.83 < 0.85*13.333 = 11.33, so it steps down after 3 s.
        change.reset();
        const int64_t low = t;
        for (; t <= low + 6 * second && !change; t += second / 4) change = c.observe(t, 9.0, 60, 10.0, 13.333);
        check("sustained headroom lowers the level after ~3 s", change && *change == 1 && t - low >= 3 * second);
        // Under budget, but the lower level would not fit with room: hold.
        change.reset();
        const int64_t hold = t;
        for (; t <= hold + 8 * second && !change; t += second / 4) change = c.observe(t, 10.5, 60, 10.0, 13.333);
        check("no step down when the lower level would not fit", !change && c.level() == 1);
        // Too few samples or no budget: no decision.
        engine::AutoNrReuseController d;
        d.reset(0);
        bool none = true;
        for (int64_t u = 0; u <= 5 * second; u += second / 4)
            none &= !d.observe(u, 30.0, 4, 10.0, 13.333) && !d.observe(u, 30.0, 60, 10.0, 0.0);
        check("no decision without samples or budget", none && d.level() == 0);
        // Never beyond 1/2.
        engine::AutoNrReuseController e;
        e.reset(0);
        for (int64_t u = 0; u <= 30 * second; u += second / 4) e.observe(u, 40.0, 60, 30.0, 13.333);
        check("Auto never goes beyond one reused frame in two", e.level() == engine::AutoNrReuseController::kMaxLevel);
    }

    // --- settings contract --------------------------------------------------
    {
        engine::EnhancementSettings s;
        check("default is off", s.nrReuse == NrReuseMode::Off && s.validate().empty());
        s.nrReuse = static_cast<NrReuseMode>(7);
        check("an unknown mode is rejected", !s.validate().empty());
        s.nrReuse = NrReuseMode::Third;
        check("a manual mode validates", s.validate().empty());
        s.nr = false;
        check("NR off normalises reuse away", s.activeVideoSettings().nrReuse == NrReuseMode::Off);
        s.nr = true;
        check("NR on keeps the mode", s.activeVideoSettings().nrReuse == NrReuseMode::Third);

        engine::EnhancementSettings off;
        off.nr = true;
        auto on = off;
        on.nrReuse = NrReuseMode::Auto;
        auto half = off;
        half.nrReuse = NrReuseMode::Half;
        check("turning reuse on rebuilds", engine::requiresGraphRebuild(off, on));
        check("turning reuse off rebuilds", engine::requiresGraphRebuild(half, off));
        check("switching Auto to a manual ratio is live", !engine::requiresGraphRebuild(on, half));
        auto offNoNr = off;
        offNoNr.nr = false;
        auto onNoNr = on;
        onNoNr.nr = false;
        check("changing the mode while NR is off does not rebuild", !engine::requiresGraphRebuild(offNoNr, onNoNr));

        pipeline::EnhanceGraphDesc desc;
        engine::describeNrLayers(half, desc);
        check("the description carries the mode", desc.nrReuse == NrReuseMode::Half);
        check("only the preview builder marks a graph for reuse", !desc.nrReusePreview);
        engine::describeNrLayers(onNoNr, desc);
        check("NR off describes no reuse", desc.nrReuse == NrReuseMode::Off);
    }

    std::cout << (failures ? "FAILED " : "ALL PASS ") << failures << std::endl;
    return failures ? 1 : 0;
}
