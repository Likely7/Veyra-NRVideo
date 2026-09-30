// Stage-6 anti-flicker tier acceptance
// (docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md §5A).
//
// Ported from SAOG0721/Magpie, GPL-3.0, commit
// 3841698348bfb246623d4acf791984c8b68a577b, tests/DLSSNRTemporalTests.cpp.
// The upstream tests are value-level checks on the tier arithmetic and the
// state machine; they are re-expressed here against Veyra's own types, and the
// GPU behaviour is covered by veyra_nr_temporal_gpu_tests.
//
// What each tier must guarantee:
//   * Static      never accumulates across a moving patch (this is the tier's
//                 whole point), so a moving block cannot smear.
//   * Flow        is the pre-tier behaviour and must stay bit-identical for a
//                 session that only ever knew `temporal=true`.
//   * FlowPlus    keeps its support channel in history alpha and refuses a
//                 correction whose direction reverses.
//   * LowFrequency must still publish history when the half-resolution support
//                 is missing, rather than fabricating a zero observation.

#include "veyra/engine/EnhancementSettings.h"
#include "veyra/engine/EffectChain.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(const char* name, bool ok)
{
    std::cout << name << '=' << (ok ? 1 : 0) << std::endl;
    if (!ok) ++failures;
}

// The tier is an enum the wire, presets and the bridge all narrow to an int.
// An out-of-range value must be rejected rather than silently truncated into a
// different tier, which would change behaviour on load.
void checkTierNarrowing()
{
    using namespace veyra::engine;
    check("off is valid", validNrAntiFlicker(NrAntiFlicker::Off));
    check("static is valid", validNrAntiFlicker(NrAntiFlicker::Static));
    check("flow is valid", validNrAntiFlicker(NrAntiFlicker::Flow));
    check("flow plus is valid", validNrAntiFlicker(NrAntiFlicker::FlowPlus));
    check("low frequency is valid", validNrAntiFlicker(NrAntiFlicker::LowFrequency));
    check("above range rejected", !validNrAntiFlicker(NrAntiFlicker(5)));
    check("far above range rejected", !validNrAntiFlicker(NrAntiFlicker(200)));
    // The default has to be the pre-tier behaviour: a chain that was created
    // before the tiers existed carries no tier field, and must keep meaning
    // what it meant.
    check("default tier preserves pre-tier behaviour", NrLayerSettings{}.antiFlicker == NrAntiFlicker::Flow);
    check("flat default tier preserves pre-tier behaviour", EnhancementSettings{}.nrAntiFlicker == NrAntiFlicker::Flow);
}

// The flat mirror and the list entry must agree, because the graph builds from
// whichever the path uses (describeNrLayers reads the list, the flat fields
// drive the single-layer API).
void checkFlatAndListAgree()
{
    using namespace veyra::engine;
    EnhancementSettings s;
    s.nr = true;
    s.nrLayerCount = 1;
    s.nrLayers[0].enabled = true;
    s.nrLayers[0].temporal = true;
    s.nrLayers[0].antiFlicker = NrAntiFlicker::LowFrequency;
    s.nrAntiFlicker = NrAntiFlicker::LowFrequency;
    check("flat and list agree on the tier", s.nrLayer(0).antiFlicker == s.nrAntiFlicker);
    // A layer's tier is only consulted when its temporal accumulation is on.
    // Presets store the tier unconditionally so toggling temporal back on
    // restores the user's chosen tier rather than resetting to the default.
    s.nrLayers[0].temporal = false;
    check("tier survives a temporal toggle", s.nrLayers[0].antiFlicker == NrAntiFlicker::LowFrequency);
}

// Validation must accept every tier and reject only genuinely invalid settings.
void checkValidation()
{
    using namespace veyra::engine;
    EnhancementSettings s;
    for (auto tier : {NrAntiFlicker::Off, NrAntiFlicker::Static, NrAntiFlicker::Flow,
                      NrAntiFlicker::FlowPlus, NrAntiFlicker::LowFrequency}) {
        s.nrAntiFlicker = tier;
        s.nrLayers[0].antiFlicker = tier;
        check("every tier validates", s.validate().empty());
    }
    s = EnhancementSettings{};
    s.nrHoldStrength = 1.5f;
    check("stabiliser strength above 1 rejected", !s.validate().empty());
    s = EnhancementSettings{};
    s.nrHoldStrength = -0.1f;
    check("stabiliser strength below 0 rejected", !s.validate().empty());
    s = EnhancementSettings{};
    s.nrHoldTolerance = 2.0f;
    check("stabiliser tolerance above 1 rejected", !s.validate().empty());
    s = EnhancementSettings{};
    s.nrHoldStrength = 0.8f;
    s.nrHoldTolerance = 0.02f;
    check("stabiliser defaults are valid", s.validate().empty());
}

// The chain node carries the tier so the node editor and the list page edit the
// same value, per the 2026-09-28 user decision.
void checkChainCarriesTier()
{
    using namespace veyra::engine;
    ChainNode node;
    node.type = EffectType::NrEnhance;
    node.enabled = true;
    node.nr.temporal = true;
    node.nr.antiFlicker = NrAntiFlicker::Static;
    check("chain node defaults to flow", ChainNrParams{}.antiFlicker == NrAntiFlicker::Flow);
    check("chain node stores the tier", node.nr.antiFlicker == NrAntiFlicker::Static);
    ChainNode other = node;
    other.nr.antiFlicker = NrAntiFlicker::Static;
    check("equal tiers compare equal", node == other);
    other.nr.antiFlicker = NrAntiFlicker::FlowPlus;
    check("different tiers compare unequal", !(node == other));
}

} // namespace

int main()
{
    std::cout << "=== NR anti-flicker tier assertions ===" << std::endl;
    checkTierNarrowing();
    checkFlatAndListAgree();
    checkValidation();
    checkChainCarriesTier();
    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
