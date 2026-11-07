// Issue 88: the AMD HIP self-test runs in a child, so a HIP fault ends the child
// and not the player, and a class that HIP survives is found before HIP is loaded.
// The child part needs an AMD adapter and VEYRA_TEST_AMD_HIP_DLL (e.g. amdhip64_6.dll).
#include "veyra/gfx/GpuSchedulingPriority.h"
#include "veyra/pipeline/AmdHipPreflight.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <iostream>
#include <string>
#include <vector>

using namespace veyra::pipeline::amdhip;

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
uint64_t firstAmdAdapter() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 0;
    uint64_t luid = 0;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; !luid && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x1002 && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            luid = (uint64_t(uint32_t(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart;
        adapter->Release();
    }
    factory->Release();
    return luid;
}
void crashAt(const wchar_t* value) { SetEnvironmentVariableW(L"VEYRA_TEST_AMD_HIP_PREFLIGHT_CRASH", value); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && wcscmp(argv[1], L"--amd-hip-preflight") == 0) return preflightMain(argc, argv);
    bool ok = true;

    ok &= check(ladder(5) == std::vector<int>{5, 4, 2}, "Realtime tries High, then Normal");
    ok &= check(ladder(4) == std::vector<int>{4, 2}, "High tries Normal");
    ok &= check(ladder(2) == std::vector<int>{2}, "Normal has nothing lower");
    ok &= check(ladder(-1) == std::vector<int>{0}, "an unknown class runs once and leaves the child's class alone");

    auto attempt = parseOutput("amd-hip-preflight start luid=0x1 class=5\namd-hip-preflight info class=5 status=0x00000000\n"
                               "amd-hip-preflight step=stream\namd-hip-preflight info runtime=70253211 device=0 arch=gfx1200\namd-hip-preflight step=memory\n"
                               "amd-hip-preflight step=release\namd-hip-preflight done status=ok\n", 0, 5);
    ok &= check(attempt.ok && attempt.step == "release" && attempt.detail.empty(), "a finished child passes");
    ok &= check(attempt.info == "class=5 status=0x00000000 | runtime=70253211 device=0 arch=gfx1200", "the child's info lines reach the log");
    attempt = parseOutput("amd-hip-preflight step=stream\r\namd-hip-preflight crash code=0xC0000005 at=amdhip64_7.dll+0x4A40A0 step=stream\r\n", 0xC0000005, 5);
    ok &= check(!attempt.ok && attempt.step == "stream" && attempt.detail.find("amdhip64_7.dll+0x4A40A0") != std::string::npos,
                "a crash keeps the module offset and the step");
    attempt = parseOutput("amd-hip-preflight step=init\namd-hip-preflight failed step=init code=100\n", 13, 4);
    ok &= check(!attempt.ok && attempt.detail == "failed step=init code=100", "a HIP error is reported as it came");
    attempt = parseOutput("amd-hip-preflight step=stream\n", 1, 5);
    ok &= check(!attempt.ok && attempt.detail.rfind("ended without a result", 0) == 0, "a child that never finished fails");
    attempt = parseOutput("amd-hip-preflight done status=ok\n", 0xC0000005, 5);
    ok &= check(!attempt.ok, "a fault after the last line still fails");
    const std::string summary = summarize({parseOutput("amd-hip-preflight step=stream\namd-hip-preflight crash code=0xC0000005 at=x+0x1 step=stream\n", 0xC0000005, 5),
                                           parseOutput("amd-hip-preflight done status=ok\n", 0, 4)});
    ok &= check(summary == "class 5: crash code=0xC0000005 at=x+0x1 step=stream; class 4: ok", "the summary lists every class tried");

    // The class the self-test chose is what this process really runs at.
    ID3D12Device* device = nullptr;   // WDDM schedules a process only once it has a GPU context
    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        using veyra::gfx::GpuPriority;
        veyra::gfx::requestGpuPriority(GpuPriority::Realtime);
        const int realtime = veyra::gfx::gpuPriorityStatus().actual;
        veyra::gfx::setGpuPriorityComputeCap(GpuPriority::High);
        const auto computeCapped = veyra::gfx::gpuPriorityStatus();
        veyra::gfx::setGpuPriorityCap(GpuPriority::Normal);
        const int bothCaps = veyra::gfx::gpuPriorityStatus().actual;
        veyra::gfx::setGpuPriorityCap(GpuPriority::Realtime);
        veyra::gfx::setGpuPriorityComputeCap(GpuPriority::Realtime);
        const auto lifted = veyra::gfx::gpuPriorityStatus();
        std::cout << "gpu class realtime=" << realtime << " compute-capped=" << computeCapped.actual
                  << " both-caps=" << bothCaps << " lifted=" << lifted.actual << '\n';
        ok &= check(realtime == 5 && computeCapped.actual == 4 && computeCapped.capped && computeCapped.requested == GpuPriority::Realtime,
                    "the compute cap applies High under a Realtime request");
        ok &= check(bothCaps == 2, "the lower of the overlay cap and the compute cap applies");
        ok &= check(lifted.actual == 5 && !lifted.capped, "lifting both caps restores Realtime");
        device->Release();
    } else {
        std::cout << "SKIP gpu class: no D3D12 device\n";
    }

    const uint64_t luid = firstAmdAdapter();
    wchar_t dll[MAX_PATH]{};
    if (!luid || !GetEnvironmentVariableW(L"VEYRA_TEST_AMD_HIP_DLL", dll, MAX_PATH)) {
        std::cout << "SKIP children: no AMD adapter or VEYRA_TEST_AMD_HIP_DLL\n";
    } else {
        wchar_t self[MAX_PATH]{};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        ok &= check(preflightExecutable().empty(), "no self-test until a host opts in");
        setPreflightExecutable(self);

        crashAt(nullptr);
        auto outcome = runLadder(luid, 5);
        std::cout << "working HIP: " << outcome.summary << '\n';
        ok &= check(outcome.ok && outcome.priority == 5 && outcome.attempts.size() == 1, "a working HIP passes at the current class");

        crashAt(L"stream:5");
        outcome = runLadder(luid, 5);
        std::cout << "crash at Realtime only: " << outcome.summary << '\n';
        ok &= check(outcome.ok && outcome.priority == 4 && outcome.attempts.size() == 2 &&
                    outcome.attempts[0].exitCode == 0xC0000005 && outcome.attempts[0].step == "stream",
                    "a stream crash at Realtime is survived and High is chosen");

        crashAt(L"stream:all");
        outcome = runLadder(luid, 5);
        std::cout << "crash at every class: " << outcome.summary << '\n';
        ok &= check(!outcome.ok && outcome.attempts.size() == 3, "a crash at every class refuses HIP");

        crashAt(nullptr);
        const auto first = preflightOnce(luid, 5);
        crashAt(L"stream:all");
        const auto second = preflightOnce(luid, 5);
        ok &= check(first.ok && second.ok && second.summary == first.summary, "one self-test per adapter per process");
        crashAt(nullptr);

        SetEnvironmentVariableW(L"VEYRA_TEST_AMD_HIP_DLL", L"veyra-no-such-hip.dll");
        outcome = runLadder(luid, 5);
        std::cout << "missing library: " << outcome.summary << '\n';
        ok &= check(!outcome.ok && outcome.attempts.size() == 1 && outcome.attempts[0].step == "load",
                    "a missing HIP library is not retried at lower classes");
        SetEnvironmentVariableW(L"VEYRA_TEST_AMD_HIP_DLL", dll);
    }
    std::cout << (ok ? "PASS" : "FAIL") << " amd hip preflight\n";
    return ok ? 0 : 1;
}
