// veyra_quality_probe - headless quality-core runner (Playbook R3.3).
// Links the product libraries ONLY (veyra_pipeline EnhanceGraph +
// veyra_sources MediaFileSource): no window, no swapchain, no audio. Runs
// the deterministic corpus (or a single looping input) through the real GPU
// chain and emits the R1.1 phase5-gate JSON contract. Guidance statistics
// (non-zero motion, confidence percentiles) come from a small diagnostic
// readback on sampled frames - the normal playback path never reads back.
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <d3d12sdklayers.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "veyra/FileIdentity.h"
#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/sink/ImageExportSink.h"

#include "../nr_harness/harness_util.h"

namespace {

using veyra::Status;
using veyra::gfx::ComPtr;

int g_failures = 0;

void noteFail(const std::string& msg)
{
    ++g_failures;
    veyra::log::error("quality", msg);
}

struct GuidanceStats {
    uint64_t sampledTexels = 0;
    uint64_t nonZeroMotion = 0;
    std::vector<double> confidences;
};

// Diagnostic readback of a strided sample from the flow (RG16F) and
// confidence (R8) textures. Runs on ring slot 3; waits on the slot fence.
bool sampleGuidanceStats(veyra::gfx::D3D12DeviceContext& ctx,
                         veyra::gfx::CommandSlotRing& ring,
                         veyra::pipeline::EnhanceGraph& graph,
                         GuidanceStats& stats)
{
    Status st = Status::Ok;
    if(!graph.flowResource()||!graph.confidenceResource())return false;
    const auto flow=graph.flowResource()->GetDesc(),confidence=graph.confidenceResource()->GetDesc();
    if(flow.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||confidence.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||
       flow.Width!=confidence.Width||flow.Height!=confidence.Height||flow.Width>16384||
       flow.Format!=DXGI_FORMAT_R16G16_FLOAT||confidence.Format!=DXGI_FORMAT_R8_UNORM)return false;
    const auto width=uint32_t(flow.Width),height=flow.Height;
    veyra::log::info("quality",std::format("guidance sample source-space={}x{} (actual resources)",width,height));
    // Sample a strided 64x36 window to keep the staging copy small.
    constexpr uint32_t kCols = 64, kRows = 36;
    if (width < kCols || height < kRows) return false;
    const uint32_t stepX = width / kCols, stepY = height / kRows;

    // Full-width row copies: flow needs width*4 bytes, confidence width*1.
    const uint64_t flowPitch = (static_cast<uint64_t>(width) * 4 + 511) & ~511ull;
    const uint64_t confPitch = (static_cast<uint64_t>(width) + 511) & ~511ull;
    const uint64_t size = (flowPitch + confPitch) * kRows;

    D3D12_HEAP_PROPERTIES rp{};
    rp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = size; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> staging;
    if (FAILED(ctx.device()->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)))) {
        return false;
    }
    // Use ring slot 3 (the headless graph path uses 0-2): an ad-hoc list
    // outside the ring fence timeline raced in-flight work and removed the
    // device (observed at frame 60, execute#59).
    ID3D12GraphicsCommandList* list = ring.acquire(3, st);
    if (list == nullptr) return false;

    auto copyStrided = [&](ID3D12Resource* tex, DXGI_FORMAT fmt, uint64_t dstOffset, uint64_t pitch) {
        for (uint32_t r = 0; r < kRows; ++r) {
            D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
            src.pResource = tex;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            // One sampled row per output row (CPU side strides the columns).
            D3D12_TEXTURE_COPY_LOCATION s = src, d{};
            d.pResource = staging.Get();
            d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            d.PlacedFootprint.Offset = dstOffset + r * pitch;
            d.PlacedFootprint.Footprint.Format = fmt;
            d.PlacedFootprint.Footprint.Width = width;
            d.PlacedFootprint.Footprint.Height = 1;
            d.PlacedFootprint.Footprint.Depth = 1;
            d.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(pitch);
            D3D12_BOX rb{0, r * stepY, 0, width, std::min(height, r * stepY + 1), 1};
            list->CopyTextureRegion(&d, 0, 0, 0, &s, &rb);
        }
    };
    // Flow: SRV state transition then copy; confidence likewise.
    // flowTex ends NON_PIXEL_SHADER_RESOURCE after densify; confTex ends
    // COMMON (see EnhanceGraph::process section 6).
    D3D12_RESOURCE_BARRIER toSrc[2]{};
    for (auto& b : toSrc) {
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    toSrc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toSrc[0].Transition.pResource = graph.flowResource();
    toSrc[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toSrc[1].Transition.pResource = graph.confidenceResource();
    list->ResourceBarrier(2, toSrc);
    copyStrided(graph.flowResource(), DXGI_FORMAT_R16G16_FLOAT, 0, flowPitch);
    copyStrided(graph.confidenceResource(), DXGI_FORMAT_R8_UNORM, flowPitch * kRows, confPitch);
    D3D12_RESOURCE_BARRIER back[2]{};
    for (int i = 0; i < 2; ++i) {
        back[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        back[i].Transition.pResource = toSrc[i].Transition.pResource;
        back[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back[i].Transition.StateAfter = toSrc[i].Transition.StateBefore;
        back[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    list->ResourceBarrier(2, back);
    if (!ring.submitAndSignal(3)) return false;
    if (!ring.waitIdle()) return false;

    uint8_t* mapped = nullptr;
    D3D12_RANGE wr{0, size};
    if (FAILED(staging->Map(0, &wr, reinterpret_cast<void**>(&mapped)))) return false;
    for (uint32_t r = 0; r < kRows; ++r) {
        const uint8_t* flowRowBytes = mapped + r * flowPitch;
        const uint8_t* confRowBytes = mapped + flowPitch * kRows + r * confPitch;
        for (uint32_t c = 0; c < kCols; ++c) {
            const uint32_t x = c * stepX;
            if (x >= width) continue;
            const uint16_t m0 = *reinterpret_cast<const uint16_t*>(flowRowBytes + x * 4);
            const uint16_t m1 = *reinterpret_cast<const uint16_t*>(flowRowBytes + x * 4 + 2);
            ++stats.sampledTexels;
            if (m0 != 0 || m1 != 0) ++stats.nonZeroMotion;
            stats.confidences.push_back(confRowBytes[x] / 255.0);
        }
    }
    static int dumpCount = 0;
    if (dumpCount < 2) {
        ++dumpCount;
        std::string line = "sample-dump flow[0..7]:";
        const uint8_t* f0 = mapped;
        for (int i = 0; i < 8; ++i) {
            line += std::format(" {:04X}", *reinterpret_cast<const uint16_t*>(f0 + i * 4));
        }
        line += std::format(" | conf[0..7]:");
        const uint8_t* c0 = mapped + flowPitch * kRows;
        for (int i = 0; i < 8; ++i) line += std::format(" {:02X}", c0[i]);
        veyra::log::info("quality", line);
    }
    staging->Unmap(0, nullptr);
    return true;
}

uint64_t queryVramBudgetHeadroomMiB(veyra::gfx::D3D12DeviceContext& ctx)
{
    // Adapter query via the device LUID.
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return 0;
    const LUID luid = ctx.device()->GetAdapterLuid();
    ComPtr<IDXGIAdapter1> adapter1;
    if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter1)))) return 0;
    ComPtr<IDXGIAdapter3> adapter;
    if (FAILED(adapter1.As(&adapter))) return 0;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return 0;
    return info.Budget > info.CurrentUsage
        ? (info.Budget - info.CurrentUsage) / (1024 * 1024) : 0;
}

double percentile(std::vector<double> v, double q)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t idx = std::min(v.size() - 1, static_cast<size_t>(q * (v.size() - 1)));
    return v[idx];
}

std::string sha256FileUpper(const std::wstring& path)
{
    veyra::FileIdentity ident;
    veyra::IdentityError err = veyra::IdentityError::None;
    if (!veyra::computeFileIdentity(path, ident, err)) return {};
    return ident.sha256Upper;
}

} // namespace

int main(int argc, char** argv)
{
    std::string corpusManifest, guidanceMode = "motion", runId = "quality-probe", jsonPath, logPath, inputPath;
    int durationSeconds = 0;
    int maxFrames = 60;
    bool native = false, diag = false, legacyMotion = false;
    // SR stage selection for deterministic A/B dumps: -1 = SR stage requested
    // but disabled (plain scale to the work extent), 0 = DLSS SR, 5 = AMD FSR.
    int srMode = 0;
    // Watchdog: a hung provider (FidelityFX + GPU-based validation was measured
    // to stall) must never be able to wedge an unattended run again. 0 = off.
    int timeoutSeconds = 0;
    std::string dumpPath;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // argv is in the ANSI code page, which cannot hold every Unicode path (the
    // logs live under E:\项目). Take the arguments from the UTF-16 command line
    // and keep them as UTF-8 in the std::string fields below.
    std::vector<std::string> args;
    {
        int wideCount = 0;
        LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        for (int n = 0; wide && n < wideCount; ++n) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[n], -1, nullptr, 0, nullptr, nullptr);
            std::string utf8(bytes > 0 ? bytes - 1 : 0, '\0');
            if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, wide[n], -1, utf8.data(), bytes, nullptr, nullptr);
            args.push_back(std::move(utf8));
        }
        LocalFree(wide);
        if (args.empty()) for (int n = 0; n < argc; ++n) args.emplace_back(argv[n]);
        argc = int(args.size());
    }
    auto widen = [](const std::string& utf8) {
        const int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        std::wstring wide(chars > 0 ? chars - 1 : 0, L'\0');
        if (chars > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), chars);
        return wide;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = args[i];
        auto value = [&]() -> std::string { return (i + 1 < argc) ? args[++i] : std::string(); };
        if (arg == "--diag") diag = true;
        else if(arg=="--legacy-motion")legacyMotion=true;
        else if (arg == "--native") native = true;
        else if (arg == "--sr-mode") srMode = std::atoi(value().c_str());
        else if (arg == "--timeout-seconds") timeoutSeconds = std::max(0, std::atoi(value().c_str()));
        else if (arg == "--frames") maxFrames = std::atoi(value().c_str());
        else if (arg == "--dump") dumpPath = value();
        else if (arg == "--corpus") corpusManifest = value();
        else if (arg == "--input") inputPath = value();
        else if (arg == "--guidance") guidanceMode = value();
        else if (arg == "--duration-seconds") durationSeconds = std::atoi(value().c_str());
        else if (arg == "--run-id") runId = value();
        else if (arg == "--log-file") logPath = value();
        else if (arg == "--json-file") jsonPath = value();
        else { std::fprintf(stderr, "unknown arg %s\n", arg.c_str()); return 2; }
    }
    if (durationSeconds < 0 || durationSeconds > 240 || maxFrames <= 0) return 2;
    if (timeoutSeconds > 0) {
        // Unattended hardening: a provider that stalls mid-dispatch (measured
        // with FidelityFX + GPU-based validation) must die on its own instead of
        // holding memory/VRAM until the machine runs out - 2026-09-16 incident.
        std::thread watchdog([timeoutSeconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(timeoutSeconds));
            std::fprintf(stderr, "[quality-probe] watchdog: %d s budget exceeded, terminating (hung provider?)\n",
                         timeoutSeconds);
            std::fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 4);
        });
        watchdog.detach();
        std::fprintf(stderr, "[quality-probe] watchdog armed: %d s\n", timeoutSeconds);
    }
    if (corpusManifest.empty() && inputPath.empty()) {
        std::fprintf(stderr, "--corpus or --input required\n");
        return 2;
    }
    if (!logPath.empty()) (void)veyra::Logger::instance().openFile(widen(logPath));

    // Build the run list (single input looping, or every corpus clip).
    struct RunClip { std::wstring path; std::string scenario; uint32_t w, h; };
    std::vector<RunClip> clips;
    std::string inputHashSource;
    if (!inputPath.empty()) {
        clips.push_back({widen(inputPath), "input",
            0, 0});
        inputHashSource = inputPath;
    } else {
        std::ifstream f(corpusManifest, std::ios::binary);
        if (!f) { noteFail("corpus manifest missing"); return 1; }
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        inputHashSource = corpusManifest;
        // Minimal manifest scan (full parse happens in the gate; here we
        // only need the file list).
        size_t p = 0;
        while ((p = text.find("\"path\": \"", p)) != std::string::npos) {
            const size_t s = p + 9;
            const size_t e = text.find('"', s);
            if (e == std::string::npos) break;
            std::string rel = text.substr(s, e - s);
            const size_t slash = corpusManifest.find_last_of("/\\");
            const std::string base = slash == std::string::npos ? "." : corpusManifest.substr(0, slash);
            const std::string full = base + "/" + rel;
            clips.push_back({widen(full), rel, 0, 0});
            p = e;
        }
        if (clips.empty()) { noteFail("corpus manifest has no clips"); return 1; }
    }

    veyra::log::info("quality", std::format("clips={} mode={}", clips.size(), guidanceMode));
    const auto tStart = std::chrono::steady_clock::now();
    PROCESS_MEMORY_COUNTERS pmcStart{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmcStart, sizeof(pmcStart));

    veyra::gfx::D3D12DeviceContext ctx;
    veyra::gfx::DeviceContextDesc ddesc;
    ddesc.commandSlotCount = 4;
    if(diag){ComPtr<ID3D12Debug> debug;ComPtr<ID3D12Debug1> gbv;
        if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))||FAILED(debug.As(&gbv)))return 1;
        debug->EnableDebugLayer();
        // GPU-based validation does not complete the FidelityFX upscale dispatch:
        // the standalone probe (tools/fsr_upscale_probe --diag, minimal verified
        // states) stalls before its dispatch line, so this is a validation-layer
        // interaction, not a state bug in the graph. Keep the debug layer for the
        // FSR SR configuration and say so instead of hanging the run.
        // GPU-based validation roughly triples working-set requirements; the
        // 2026-09-16 incident was system virtual-memory exhaustion while it ran.
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        const bool enoughMemory = GlobalMemoryStatusEx(&memory) &&
            memory.ullAvailPhys >= (6ull << 30);  // 6 GiB headroom
        if(srMode==int(veyra::engine::kVideoSrFsr)){
            veyra::log::warn("quality-probe","GPU-based validation skipped for the AMD FSR SR configuration (validation layer does not complete the FidelityFX upscale dispatch); debug layer stays on");
        }else if(!enoughMemory){
            veyra::log::warn("quality-probe","GPU-based validation skipped: less than 6 GiB available physical memory (it is the most memory-hungry mode in this tool)");
        }else{
            gbv->SetEnableGPUBasedValidation(TRUE);
        }
        ddesc.enableDebugLayer=true;}
    Status st = Status::Ok;
    if (!ctx.initialize(ddesc, st)) { noteFail("device init failed"); return 1; }
    veyra::gfx::CommandSlotRing ring;
    if (!ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, st)) {
        noteFail("ring init failed");
        return 1;
    }

    // Config string -> configHash (mode + extents + feature switches).
    const std::string configText = "guidance=" + guidanceMode
        + (native ? ";workExtent=native;nrPerFrame=1;fg=0;nvofStandalone=" : ";workExtent=3840x2160;nrPerFrame=1;fg=0;nvofStandalone=")
        + (guidanceMode == "motion" || guidanceMode == "motion-depth" || guidanceMode == "auto" ? "1" : "0")
        + (legacyMotion?";motionValidation=0;":";motionValidation=3;");
    const std::string configHash = veyra::sha256Hex(
        reinterpret_cast<const uint8_t*>(configText.data()), configText.size());

    // Hashes.
    wchar_t exePath[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    const std::string exeHash = sha256FileUpper(exePath);
    const std::string inputHash = sha256FileUpper(
        std::wstring(inputHashSource.begin(), inputHashSource.end()));
    wchar_t runtimeDir[MAX_PATH * 2]{};
    GetFullPathNameW(L"runtime_local\\nvidia", MAX_PATH * 2, runtimeDir, nullptr);
    const std::string runtimeHash = sha256FileUpper(
        std::wstring(runtimeDir) + L"\\nvngx_dlssnr.dll");

    // Aggregates for the JSON contract.
    uint64_t sourceFrames = 0, processedFrames = 0;
    uint64_t resetSeek = 0;
    uint64_t aggNr = 0, aggSr = 0, aggNvof = 0, aggFg = 0, aggNrMotion = 0, aggCuts = 0;
    uint32_t firstSourceW = 0, firstSourceH = 0;
    std::string guidanceProvenance = guidanceMode == "off" ? "off" : "zero";
    GuidanceStats stats;
    std::vector<double> gpuPassMs;
    uint64_t normalPathReadbackCount = 0; // diagnostic stats copies are not the normal path
    SIZE_T wsPeak = pmcStart.WorkingSetSize;
    uint64_t vramHeadroom = UINT64_MAX;
    uint32_t actualWorkW=0, actualWorkH=0;

    const bool nrOn = guidanceMode != "off";
    const bool nvofOn = guidanceMode == "motion" || guidanceMode == "motion-depth" || guidanceMode == "auto";

    for (const RunClip& clip : clips) {
        veyra::source::MediaFileSource source;
        veyra::source::SourceOpenDesc od;
        od.path = clip.path;
        od.preferHardwareDecode = false;
        if (!source.open(od)) { noteFail("source open failed"); continue; }
        const uint32_t srcW = source.info().width, srcH = source.info().height;
        if (firstSourceW == 0) { firstSourceW = srcW; firstSourceH = srcH; }

        veyra::pipeline::EnhanceGraphDesc gd{};gd.validateMotion=!legacyMotion;
        gd.sourceWidth = srcW;
        gd.sourceHeight = srcH;
        gd.workWidth = native ? srcW : 3840; gd.workHeight = native ? srcH : 2160;
        gd.enableSr = nrOn && (srcW != gd.workWidth || srcH != gd.workHeight) && srMode >= 0;
        if (srMode > 0) gd.videoSrQuality = uint32_t(srMode);
        actualWorkW = gd.workWidth; actualWorkH = gd.workHeight;
        gd.enableNr = nrOn;
        gd.enableFg = false;
        gd.enableNvofStandalone = nvofOn;
        gd.runtimeAbsPath = runtimeDir;
        gd.stageMark = [&](const char*) {}; // per-stage timing handled below

        veyra::pipeline::EnhanceGraph graph(ctx, ring);
        if (!graph.initialize(gd)) { noteFail("graph init failed"); continue; }
        if (!graph.createViews()) { noteFail("graph views failed"); continue; }

        uint64_t framesThisClip = 0;
        const auto clipStart = std::chrono::steady_clock::now();
        bool resetNext = true;
        veyra::pipeline::EnhanceGraph::FrameOutputs lastOut;
        for (;;) {
            if (durationSeconds > 0) {
                const double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - clipStart).count();
                if (elapsed >= durationSeconds) break;
            }
            veyra::pipeline::FramePacket pkt;
            const AVFrame* frame = nullptr;
            const auto rs = source.read(pkt, &frame);
            if (rs == veyra::source::SourceReadStatus::Eos) {
                if (durationSeconds > 0) {
                    if (!source.seek(veyra::pipeline::Rational{0, 1})) { noteFail("loop seek failed"); break; }
                    ++resetSeek;
                    resetNext = true;
                    continue;
                }
                break;
            }
            if (rs != veyra::source::SourceReadStatus::Frame || frame == nullptr) {
                noteFail("source read error");
                break;
            }
            ++sourceFrames;
            const double ptsMs = 1000.0 * pkt.pts.toDouble();
            const auto mark0 = std::chrono::steady_clock::now();
            veyra::pipeline::EnhanceGraph::FrameOutputs out;
            if (!graph.process(frame, ptsMs, resetNext, out)) {
                noteFail("graph process failed");
                break;
            }
            gpuPassMs.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - mark0).count());
            lastOut = out;
            resetNext = false;
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) wsPeak = std::max(wsPeak, pmc.WorkingSetSize);
            vramHeadroom = std::min(vramHeadroom, queryVramBudgetHeadroomMiB(ctx));
            ++processedFrames;
            ++framesThisClip;

            // Sample guidance statistics every 60th frame.
            if (nvofOn && framesThisClip % 60 == 0) {
                if (!sampleGuidanceStats(ctx, ring, graph, stats)) {
                    veyra::log::warn("quality", "guidance stats sample failed");
                }
            }
            if (durationSeconds == 0 && framesThisClip >= static_cast<uint64_t>(maxFrames)) break; // safety
        }
        if (!dumpPath.empty() && framesThisClip > 0) {
            veyra::sink::RgbaImage image;
            if (!veyra::sink::readRgba8(ctx, ring, graph.videoFrameResource(lastOut.videoSlot), image) ||
                !veyra::sink::saveImage(widen(dumpPath), image)) noteFail("diagnostic image write failed");
        }
        aggNr += graph.metrics().nrEvaluateCount;
        aggNrMotion += graph.metrics().nrMotionFrames;
        aggCuts += graph.metrics().sceneCutCount;
        aggSr += graph.metrics().srEvaluateCount;
        aggNvof += graph.metrics().nvofExecuteCount;
        aggFg += graph.metrics().fgGeneratedFrames;
        if (nvofOn && graph.metrics().nvofExecuteCount > 0) {
            guidanceProvenance = "nvof";
        }
        (void)ring.waitIdle();
        graph.shutdown();
    }

    uint64_t diagErrors=0;
    if(diag){ComPtr<ID3D12InfoQueue> iq;if(FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&iq))))noteFail("InfoQueue missing");
        else for(UINT64 i=0;i<iq->GetNumStoredMessages();++i){SIZE_T size=0;iq->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(FAILED(iq->GetMessage(i,msg,&size))){noteFail("InfoQueue retrieval failed");break;}if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++diagErrors;veyra::log::error("d3d",msg->pDescription);}}
        if(diagErrors)noteFail("D3D12 validation errors");
    }
    PROCESS_MEMORY_COUNTERS pmcEnd{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmcEnd, sizeof(pmcEnd));
    const double durationS = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    uint32_t removedReason = 0;
    const bool deviceAlive = ctx.checkDeviceAlive(removedReason);
    if (vramHeadroom == UINT64_MAX) vramHeadroom = 0;
    if (wsPeak < pmcEnd.WorkingSetSize) wsPeak = pmcEnd.WorkingSetSize;

    const uint64_t nrEvaluateCount = aggNr;
    const uint64_t srEvaluateCount = aggSr;
    const uint64_t nvofExecuteCount = aggNvof;

    std::string j;
    j += "{\n";
    j += "  \"probe\": \"veyra_quality_probe\",\n";
    j += std::format("  \"runId\": \"{}\",\n", veyra::harness::util::jsonEscape(runId));
    j += std::format("  \"guidanceMode\": \"{}\",\n", guidanceMode);
    j += std::format("  \"motionValidation\": {},\n", legacyMotion ? 0 : 3);
    j += std::format("  \"exeHash\": \"{}\",\n", exeHash.empty() ? "unavailable" : exeHash);
    j += std::format("  \"inputHash\": \"{}\",\n", inputHash.empty() ? "unavailable" : inputHash);
    j += std::format("  \"configHash\": \"{}\",\n", configHash);
    j += std::format("  \"runtimeHash\": \"{}\",\n", runtimeHash.empty() ? "unavailable" : runtimeHash);
    j += std::format("  \"sourceExtent\": {{\"width\": {}, \"height\": {}}},\n", firstSourceW, firstSourceH);
    const uint32_t workJsonW = actualWorkW;
    const uint32_t workJsonH = actualWorkH;
    j += std::format("  \"workingExtent\": {{\"width\": {}, \"height\": {}}},\n", workJsonW, workJsonH);
    j += std::format("  \"outputExtent\": {{\"width\": {}, \"height\": {}}},\n", workJsonW, workJsonH);
    j += std::format("  \"sourceFrames\": {},\n", sourceFrames);
    j += std::format("  \"processedFrames\": {},\n", processedFrames);
    j += std::format("  \"nrEvaluateCount\": {},\n", nrEvaluateCount);
    j += std::format("  \"srEvaluateCount\": {},\n", srEvaluateCount);
    j += std::format("  \"nvofExecuteCount\": {},\n", nvofExecuteCount);
    j += std::format("  \"guidanceProvenance\": \"{}\",\n", guidanceProvenance);
    j += std::format("  \"nonZeroMotionCount\": {},\n", stats.nonZeroMotion);
    j += std::format("  \"confidenceP05\": {:.4f},\n", percentile(stats.confidences, 0.05));
    j += std::format("  \"confidenceP50\": {:.4f},\n", percentile(stats.confidences, 0.50));
    j += std::format("  \"confidenceP95\": {:.4f},\n", percentile(stats.confidences, 0.95));
    j += std::format("  \"depthMode\": \"{}\",\n", "disabled");
    j += std::format("  \"depthFallbackReason\": \"{}\",\n",
        "DAV2 provider not implemented (R4.4 pending); motion-only/zero path");
    j += "  \"depthAgeP95\": 0.0,\n";
    j += std::format("  \"resetCountsByReason\": {{\"Open\": {}, \"Seek\": {}}},\n", clips.size(), resetSeek);
    j += std::format("  \"sceneCutCount\": {},\n",aggCuts);
    j += std::format("  \"nrMotionFrames\": {},\n",aggNrMotion);
    j += std::format("  \"d3dDiagErrors\": {},\n",diagErrors);
    j += std::format("  \"diagnosticsEnabled\": {},\n",diag);
    j += "  \"crossCutHistoryCount\": null,\n";
    j += std::format("  \"cpuProcessP50Ms\": {:.3f},\n", percentile(gpuPassMs, 0.50));
    j += std::format("  \"cpuProcessP95Ms\": {:.3f},\n", percentile(gpuPassMs, 0.95));
    j += std::format("  \"normalPathReadbackCount\": {},\n", normalPathReadbackCount);
    j += "  \"gpuPassP50Ms\": null,\n  \"gpuPassP95Ms\": null,\n";
    j += std::format("  \"cpuFenceWaitCount\": {},\n",ring.cpuWaitCount());
    j += std::format("  \"gpuCommandListP50Ms\": {:.4f},\n",percentile(ring.gpuCommandTimesMs(),0.5));
    j += std::format("  \"gpuCommandListP95Ms\": {:.4f},\n",percentile(ring.gpuCommandTimesMs(),0.95));
    j += "  \"queueHighWater\": null,\n";
    j += "  \"resourcePoolPeakMiB\": null,\n";
    j += std::format("  \"vramBudgetHeadroomMiB\": {},\n", vramHeadroom);
    j += std::format("  \"workingSetStartMiB\": {:.1f},\n", pmcStart.WorkingSetSize / 1048576.0);
    j += std::format("  \"workingSetPeakMiB\": {:.1f},\n", wsPeak / 1048576.0);
    j += std::format("  \"workingSetEndMiB\": {:.1f},\n", pmcEnd.WorkingSetSize / 1048576.0);
    j += std::format("  \"deviceRemovedCount\": {},\n", deviceAlive ? 0 : 1);
    j += std::format("  \"durationSeconds\": {:.1f},\n", durationS);
    j += std::format("  \"failures\": {}\n", g_failures);
    j += "}\n";

    if (!jsonPath.empty()) {
        (void)veyra::harness::util::writeTextFileUtf8(
            widen(jsonPath), j);
    }
    std::printf("%s", j.c_str());
    veyra::Logger::instance().flush();

    ring.shutdown();
    ctx.shutdown();
    return (g_failures == 0 && processedFrames > 0) ? 0 : 1;
}
