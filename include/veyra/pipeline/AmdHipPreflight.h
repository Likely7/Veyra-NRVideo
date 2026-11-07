// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// HIP self-test for the in-process RX 9000 NR runtime (lmxxf).
//
// GitHub issue 88 (RX 9060 XT gfx1200, driver 32.0.32015.12012, amdhip64_7.dll
// 10.0.3683.0): the first hipStreamCreate the runtime makes on the first NR
// frame let a HIP-internal thread read through a null object
// (amdhip64_7.dll+0x4A40A0, read of 0x390) and the whole player went down,
// 14 times on 2.1.2 and 2.1.3. The same gfx1200 card runs these kernels in
// Magpie (upstream lmxxf issue 18), whose process does not hold the Realtime
// GPU scheduling class this player requests by default.
//
// HIP therefore meets this process only after a child (this executable,
// --amd-hip-preflight) has made the same first calls on the same adapter: a
// D3D12 device, the parent's GPU scheduling class, hipInit, the device with the
// adapter's LUID, hipSetDevice, hipStreamCreate and a small allocation cleared on
// that stream. A crash ends the child, not the player. When the current class
// fails, High and then Normal are tried; the first class that passes caps this
// process while AMD NR is in use. When none passes AMD NR is refused with the
// failing step, instead of crashing.
#include <cstdint>
#include <string>
#include <vector>

namespace veyra::pipeline::amdhip {
struct PreflightAttempt {
    int priority=0;       // D3DKMT scheduling class the child used; 0 = left unchanged
    bool ok=false;
    std::string step;     // the last step the child started
    std::string detail;   // failure, crash or timeout text
    std::string info;     // the child's info lines (class granted, HIP runtime, devices), " | " joined
    uint32_t exitCode=0;
};
struct PreflightOutcome {
    bool ok=false;
    int priority=0;       // class of the passing attempt
    std::vector<PreflightAttempt> attempts;
    std::string summary;  // one line for the log and the refusal
};

// Child mode: argv = exe --amd-hip-preflight <hip dll> <adapter LUID hex> <priority class>.
int preflightMain(int argc, wchar_t** argv);

// The classes to try: the current one, then High and Normal below it. An unknown
// current class (-1) runs once without changing the child's class.
std::vector<int> ladder(int current);
// Reads one child's output; a missing final line means it did not finish.
PreflightAttempt parseOutput(const std::string& output, uint32_t exitCode, int priority);
std::string summarize(const std::vector<PreflightAttempt>& attempts);

// The executable that implements --amd-hip-preflight. Empty (the default, e.g.
// in tests that do not opt in): no self-test, HIP is loaded as before.
void setPreflightExecutable(const std::wstring& path);
std::wstring preflightExecutable();
// Runs the ladder (no cache). VEYRA_TEST_AMD_HIP_DLL replaces amdhip64_7.dll in tests.
PreflightOutcome runLadder(uint64_t adapterLuid, int currentPriority);
// runLadder once per adapter per process; later calls return the first outcome.
PreflightOutcome preflightOnce(uint64_t adapterLuid, int currentPriority);
} // namespace veyra::pipeline::amdhip
