#pragma once

// DLSSG 2X truth harness (Playbook 14.2/14.3). Synthetic translation clip,
// real NGX Feature 11 create/evaluate, GPU readback, and structural proof
// that generated frames are real interpolations (not duplicates, not blends,
// direction/midpoint correct, PTS monotonic, scene cut honored).

#include <cstdint>
#include <string>

namespace veyra::harness {

struct FgTestArgs {
    std::wstring runtimeDir;
    std::string runId;
    std::wstring logFile;
    std::wstring jsonFile;
    std::wstring captureDir;
    bool capabilityOnly = false; // --fg-cap: query + honest JSON, no create
    bool planarSix = false; // Direct NGX diagnostic; no player/EnhanceGraph.
    bool alternateGroups = false; // planarSix with multiFrameCount 1/5 alternating, no reset.
    // "ada" or "ampere": patch the 310.9.1 provider with DlssgTransfusion before
    // NGX Init (diagnostic; on non-Ada/Ampere hosts this forces that path).
    std::string transfusion;
};

// Runs the full FG truth test and writes JSON to args.jsonFile.
// Returns 0 only when every Playbook 14.3 proof passes.
int runFgTest(const FgTestArgs& args);

} // namespace veyra::harness
