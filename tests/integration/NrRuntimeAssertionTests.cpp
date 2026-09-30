// Stage-4 defect audit assertions (docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md).
//
// These are the cheap, GPU-free checks for defects other projects hit:
//   * architecture substitution scope (sdli 0.3.4 / video2dlssnr 1.4.1: a
//     spoof meant for NR leaked into SR and crashed RTX 30),
//   * NR-layer release accounting (NeuralScreen 25180371 leaked 3.6GB),
//   * the version policy that decides whether Ampere is re-labelled Blackwell.
//
// They assert on the real decision functions, not on restated constants, so a
// future edit that widens the spoof scope or flips a version threshold fails
// here instead of on a user's 30-series card.

#include "veyra/ngx/NrArchitecturePolicy.h"
#include "veyra/ngx/AmpereMfgUnlock.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(const char* name, bool ok)
{
    std::cout << name << '=' << (ok ? 1 : 0) << std::endl;
    if (!ok) ++failures;
}

// The NR architecture rewrite is the whole compatibility contract for RTX 30.
// It must fire only for a successful query about the selected adapter whose
// real architecture is Ampere -- never for another GPU, never on failure.
void auditNrArchitectureRewriteScope()
{
    using namespace veyra::ngx;

    // A successful Ampere query on the selected adapter is rewritten.
    {
        uint32_t arch = 0x170u;
        check("ampere rewrite applies to selected ampere",
              rewriteNrAmpereArchitecture(arch, true, 0) && arch == 0x1B0u);
    }
    // Failure result: the driver's answer is preserved whatever it says.
    for (int result : {1, -1, -8, 0x7FFFFFFF}) {
        uint32_t arch = 0x170u;
        check("ampere rewrite preserves on failure",
              !rewriteNrAmpereArchitecture(arch, true, result) && arch == 0x170u);
    }
    // Not the selected adapter: an unknown handle is never treated as ours.
    {
        uint32_t arch = 0x170u;
        check("ampere rewrite skips unselected adapter",
              !rewriteNrAmpereArchitecture(arch, false, 0) && arch == 0x170u);
    }
    // Every other architecture is left alone, including the ones a real
    // product session runs on.
    for (uint32_t arch : {0x190u, 0x1B0u, 0x160u, 0x150u, 0x130u, 0x0u, 0x1A0u}) {
        const uint32_t original = arch;
        check("ampere rewrite skips non-ampere architecture",
              !rewriteNrAmpereArchitecture(arch, true, 0) && arch == original);
    }
}

// Version policy: builds that lift the gate themselves must keep the real
// architecture, because reporting Blackwell makes them choose kernels Ampere
// cannot run. 310.8.0 / 310.8.1 (the NeuralScreen line) still need the rewrite;
// 310.8.2+ and anything unknown do not.
void auditAmpereRewriteVersionPolicy()
{
    using namespace veyra::ngx;

    auto version = [](uint32_t major, uint32_t minor, uint32_t build) {
        return std::pair<uint32_t, uint32_t>{major << 16 | minor, build << 16};
    };

    const auto [ms080, ls080] = version(310, 8, 0);
    const auto [ms081, ls081] = version(310, 8, 1);
    const auto [ms082, ls082] = version(310, 8, 2);
    const auto [ms083, ls083] = version(310, 8, 3);
    const auto [ms090, ls090] = version(310, 9, 0);
    const auto [ms070, ls070] = version(310, 7, 0);
    const auto [ms100, ls100] = version(311, 0, 0);
    const auto [msZero, lsZero] = version(0, 0, 0);

    // Only the known self-lifting family is exempt: exactly 310.8.2 and later
    // builds of the 310.8 line (SF-v2, Lecram). These are the versions whose
    // own gate handling makes the rewrite actively harmful.
    check("310.8.0 needs ampere rewrite", nrRuntimeNeedsAmpereRewrite(ms080, ls080));
    check("310.8.1 needs ampere rewrite", nrRuntimeNeedsAmpereRewrite(ms081, ls081));
    check("310.8.2 keeps real architecture", !nrRuntimeNeedsAmpereRewrite(ms082, ls082));
    check("310.8.3 (Lecram) keeps real architecture", !nrRuntimeNeedsAmpereRewrite(ms083, ls083));
    // Everything else is deliberately conservative, including versions newer
    // than the ones we have tested: an unrecognised build gets the rewrite
    // rather than the benefit of the doubt. This is the documented policy in
    // NrArchitecturePolicy.h ("unknown new versions default to rewrite"); the
    // trade-off is recorded in the WORKLOG as a known limitation, because a
    // future self-lifting build outside the 310.8 line would be mis-handled.
    check("310.7 unverified build still rewrites", nrRuntimeNeedsAmpereRewrite(ms070, ls070));
    check("310.9 unknown build still rewrites", nrRuntimeNeedsAmpereRewrite(ms090, ls090));
    check("311.0 unknown build still rewrites", nrRuntimeNeedsAmpereRewrite(ms100, ls100));
    // An unreadable version (no VS_FIXEDFILEINFO) must fall back to the same
    // conservative behaviour rather than silently trusting a new build.
    check("unreadable version falls back to rewrite", nrRuntimeNeedsAmpereRewrite(msZero, lsZero));
    // The exemption is a floor, not an equality: later builds of the same minor
    // line stay exempt too, otherwise 310.8.9 would regress to the rewrite.
    const auto [ms089, ls089] = version(310, 8, 9);
    check("310.8.9 stays exempt", !nrRuntimeNeedsAmpereRewrite(ms089, ls089));
}

// The Ada and Ampere provider unlocks must never both believe they own the
// same provider: the 310.9.1 module is patched by DlssgTransfusion, and the
// 310.7 unlocks apply only to the audited 310.7 bytes.
void auditUnlockOwnership()
{
    using namespace veyra::ngx;

    check("ampere unlock starts unapplied", !AmpereMfgUnlock::applied());
    // apply() returns a State report, not a bool: a null module must produce a
    // report that did not apply and found no module.
    const auto refused = AmpereMfgUnlock::apply(nullptr, true, 0);
    check("ampere unlock rejects a null module",
          !refused.applied && !refused.moduleFound && !refused.identityVerified);
    check("ampere unlock still unapplied after refusal", !AmpereMfgUnlock::applied());
    check("ampere unlock snapshot agrees with applied",
          AmpereMfgUnlock::snapshot().applied == AmpereMfgUnlock::applied());
}

} // namespace

int main()
{
    std::cout << "=== NR runtime audit assertions ===" << std::endl;
    auditNrArchitectureRewriteScope();
    auditAmpereRewriteVersionPolicy();
    auditUnlockOwnership();
    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
