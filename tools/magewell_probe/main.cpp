// Magewell probe: loads LibMWCapture.dll the way the player does, lists the SDK's capture
// channels and, given a DirectShow device path, tries to start the low-latency capture for a few
// seconds. Without a Pro Capture card it shows that the runtime loads and that no channel matches.
//
// usage: veyra_magewell_probe [directshow-device-path] [width height]
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "veyra/source/MagewellCapture.h"

using namespace veyra::source;

int wmain(int argc, wchar_t** argv) {
    std::wstring detail;
    const bool available = magewell::runtimeAvailable(&detail);
    std::wprintf(L"runtime available=%d (%s)\n", available, detail.c_str());
    if (!available) return 2;
    magewell::VideoRequest request;
    request.directShowPath = argc > 1 ? argv[1] : L"\\\\?\\pci#ven_1cd7&dev_0000#probe#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global";
    request.width = argc > 3 ? unsigned(_wtoi(argv[2])) : 1920;
    request.height = argc > 3 ? unsigned(_wtoi(argv[3])) : 1080;
    request.format = magewell::Format::Nv12;
    request.stride = request.width;
    request.frameBytes = size_t(request.stride) * request.height * 3 / 2;
    magewell::LowLatencyVideo video;
    std::wstring error;
    uint64_t frames = 0;
    if (!video.start(request, [&](const uint8_t*, size_t, double, bool) { ++frames; }, error)) {
        std::wprintf(L"start: %s\n", error.c_str());
        std::wprintf(L"status: %s\n", magewell::statusText().c_str());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
    const auto stats = video.stats();
    video.stop();
    std::wprintf(L"frames=%llu timeouts=%llu frameStartToHostMs=%.2f\n", (unsigned long long)stats.frames,
                 (unsigned long long)stats.timeouts, stats.frameStartToHostMs);
    return stats.frames ? 0 : 1;
}
