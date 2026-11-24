// Opens the first XCAP card through the product's own capture source and reports
// what the engine would see. veyra_xcap_probe [seconds] [cpu|hdmi]
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/XcapAudio.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/pipeline/CaptureUploadFrame.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace veyra;
using Clock = std::chrono::steady_clock;
int main(int argc, char** argv) {
    const double seconds = argc > 1 ? atof(argv[1]) : 8;
    const bool cpu = argc > 2 && std::string(argv[2]) == "cpu";
    const bool sound = argc > 3 && std::string(argv[3]) == "sound";
    if (sound) for (const auto& e : sink::enumerateRenderEndpoints()) if (!source::xcapture::tvName().empty() && e.name.find(source::xcapture::tvName()) != std::wstring::npos) sink::setPreferredRenderEndpoint(e.id);
    const bool hdmi = argc > 2 && std::string(argv[2]) == "hdmi";
    const std::wstring_view prefix = hdmi ? L"xcap-native:hdmi:" : L"xcap-native:pattern:";
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto all = source::CaptureCardSource::deviceDetails();
    const source::CaptureDevice* device = nullptr;
    for (const auto& d : all) { wprintf(L"device: %ls\n", d.name.c_str()); if (!device && d.path.starts_with(prefix)) device = &d; }
    if (!device) { printf("no XCAP card\n"); return 2; }
    const auto formats = source::CaptureCardSource::formatsByPath(device->path);
    for (const auto& f : formats) wprintf(L"format %d: %ls\n", f.index, f.label.c_str());
    Microsoft::WRL::ComPtr<ID3D12Device> d3d;
    if (!cpu) { const HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d)); printf("D3D12 device hr=0x%08lX\n", hr); }
    source::CaptureCardSource src;
    source::SourceOpenDesc desc;
    desc.path = source::CaptureCardSource::makeCapturePath(0, *device, formats.empty() ? 0 : formats[0].index, sound ? source::kCaptureAudioFromVideoDevice : source::kCaptureAudioDisabled, nullptr, 0, 0, formats.empty() ? L"" : formats[0].key);
    desc.d3d12Device = d3d.Get();
    if (!src.configure(desc)) { const auto& e = src.errorMessage(); char u[1024]{}; WideCharToMultiByte(CP_UTF8, 0, e.c_str(), -1, u, sizeof(u), nullptr, nullptr); printf("configure failed: %s\n", u); return 1; }
    printf("configured %ux%u %.3f fps %s via %s\n", src.info().width, src.info().height, src.info().averageFps, src.info().videoPixelFormatName.c_str(), src.info().videoDecodePath.c_str());
    { const auto& c = src.info().color; printf("colour: transfer %d (6 = PQ) primaries %d (4 = BT.2020) matrix %d (3 = BT.2020 NCL) range %d hdr %d MaxCLL %.0f MaxFALL %.0f mastering %.0f\n",
        int(c.transfer), int(c.primaries), int(c.matrix), int(c.range), c.isHdrPath(), c.hdrMaxCllNits, c.hdrMaxFallNits, c.hdrMasteringPeakNits); }
    src.setPictureCompare(true);
    if (!src.start()) { wprintf(L"start failed: %ls\n", src.errorMessage().c_str()); return 1; }
    uint64_t frames = 0, repeats = 0, direct = 0, drops = 0, disc = 0, upload = 0; std::vector<double> age, gap; int64_t lastPts = 0; bool wrong = false;
    const auto end = Clock::now() + std::chrono::duration<double>(seconds);
    auto last = Clock::now();
    while (Clock::now() < end) {
        WaitForSingleObject(src.frameEvent(), 100);
        pipeline::FramePacket packet; const AVFrame* frame = nullptr;
        const auto r = src.tryRead(packet, &frame);
        if (r == source::SourceReadStatus::Waiting) continue;
        if (r != source::SourceReadStatus::Frame) { wprintf(L"read status %d %ls\n", int(r), src.errorMessage().c_str()); wrong = true; break; }
        const auto now = Clock::now();
        ++frames;
        age.push_back(double(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() / 100 - packet.arrivalHost100ns) / 10.0);
        if (frames > 1) gap.push_back(std::chrono::duration<double, std::micro>(now - last).count());
        last = now;
        if (sound) src.videoPresented(double(frame->pts) / 10000, std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() / 100, packet.arrivalHost100ns);
        if (frame->pts <= lastPts && frames > 1) wrong = true;
        lastPts = frame->pts;
        if (packet.flags & uint32_t(pipeline::FrameFlagBits::Drop)) ++drops;
        if (packet.flags & uint32_t(pipeline::FrameFlagBits::Discontinuity)) ++disc;
        if (auto* u = pipeline::captureUploadFrame(frame)) { ++upload; if (u->direct) ++direct; if (u->pictureValid && u->pictureRepeat) ++repeats; }
    }
    const auto audioState = src.audioState();
    const auto m = src.metrics();
    src.close();
    const auto show = [](const char* n, std::vector<double>& v) { if (v.empty()) return; std::sort(v.begin(), v.end()); printf("%-34s p50 %8.1f p99 %8.1f max %8.1f us\n", n, v[v.size() / 2], v[size_t(v.size() * .99)], v.back()); };
    printf("frames %llu uploadBacked %llu direct %llu repeats %llu flaggedDrops %llu discontinuities %llu  received %llu dropped %llu callbackFps %.2f\n", frames, upload, direct, repeats, drops, disc, m.received, m.dropped, m.callbackFps);
    if (sound) printf("audio skew %.2f ms  drift correction %.1f ppm  resets %llu  underruns %llu  running %d\n", audioState.skewMs.value_or(-999), audioState.driftCorrectionPpm, (unsigned long long)audioState.resets, (unsigned long long)audioState.underruns, int(audioState.running));
    if (sound) printf("audio received %llu blocks, error %s\n", (unsigned long long)audioState.inputBlocks, audioState.error.empty() ? "none" : "set");
    show("last line readable -> engine has frame", age);
    show("frame interval", gap);
    const bool ok = !wrong && frames > seconds * 59 && m.dropped <= 2;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
