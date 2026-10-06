// veyra_player_probe — Phase 6 realtime engine probe (Playbook section 22).
// Full chain on one D3D12 device: FFmpeg demux/decode (software) -> NV12
// upload -> YuvToLinearRgb -> DLSS SR (bypass at 1:1) -> ParityEncode ->
// Feature 18 NR -> ParityDecode -> ScaleBlit to the SDR RGBA8 working frame
// -> NVOF per-pixel forward flow -> DLSSG 2X -> PresentSink flip-discard
// present of [generated, real]. Audio is a WASAPI shared event-mode renderer
// and the master clock. Scenario mode exercises play/pause, 10 seeks,
// window resize, NR/FG toggles and loop; endurance mode runs 4K30 and 4K60
// pacing passes. The normal path performs ZERO GPU->CPU readbacks.
//
// SYSTEM CONSTRAINTS (diagnosed 2026-09-04 on this machine; an injected
// D3D12 layer - consistent with third-party screen-capture software - makes
// CreateCommittedResource, Buffer::Map AND command-list Close fail after
// descriptor views exist, while the device itself keeps working):
//   1. Allocate ALL committed resources (and Map upload buffers) BEFORE the
//      first CreateShaderResourceView.
//   2. After views exist, never record CopyTextureRegion/CopyResource (they
//      poison Close). All per-frame data movement goes through compute
//      shaders (Nv12Upload, ScaleBlit).
//   3. The local experimental NR snippet cannot Evaluate on a command list
//      with a bound compute PSO/descriptor heaps; the NR evaluate gets its
//      own freshly-reset list.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/hwcontext_d3d12va.h>
}

#pragma warning(push, 0)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssg.h>
#pragma warning(pop)

#include "../nr_harness/harness_util.h"
#include "veyra/Log.h"
#include "veyra/Result.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/sink/WasapiAudioSink.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/media/FFmpegDemuxer.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/ngx/DlssFgBackend.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/DlssSrBackend.h"
#include "veyra/ngx/NgxCoreHost.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/ngx/NvOfSession.h"

namespace {

using veyra::gfx::ComPtr;
namespace utilns = veyra::harness::util;
using utilns::jsonEscape;
using utilns::writeTextFileUtf8;

struct EngineMetrics {
    uint64_t presentCount = 0;
    uint64_t fgGeneratedFrames = 0;
    uint64_t realFramesPresented = 0;
    uint64_t normalPathReadbackCount = 0;
    uint64_t nrEvaluateCount = 0;
    uint64_t srEvaluateCount = 0;
    uint64_t nvofExecuteCount = 0;
    double maxAvDriftMs = 0.0;
    int64_t maxInFlight = 0;
};

// GPU helper facilities now live in the veyra_pipeline product library.
using veyra::pipeline::makeTexture;
using veyra::pipeline::makeUploadBuffer;
using veyra::pipeline::StateTracker;
using veyra::pipeline::ComputePass;
using veyra::pipeline::loadShaderBytes;
using veyra::pipeline::makeSrv;
using veyra::pipeline::makeUav;
using veyra::pipeline::DescriptorStager;
using veyra::pipeline::GraphicsPass;
} // namespace

namespace {
const char chrQ = '"';
const wchar_t chrWHelper() { return L'W'; } // unused placeholder
const std::wstring& absRuntimeBare()
{
    static std::wstring cached = [] {
        wchar_t buf[MAX_PATH * 2]{};
        GetFullPathNameW(L"runtime_local\\nvidia", MAX_PATH * 2, buf, nullptr);
        return std::wstring(buf);
    }();
    return cached;
}
} // namespace

namespace {
// s10 diagnostic: the 0x87D teardown exception under the D3D12 debug layer
// has no local dump (cdb/WER-LocalDumps unavailable without admin). This SEH
// wrapper converts it into logged evidence: exception code, faulting address
// and owning module. The run STILL FAILS when it fires - nothing is excused.
DWORD g_sehCode = 0;
void* g_sehAddr = nullptr;
char g_sehMod[MAX_PATH] = {};
int sehFilter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord) {
        g_sehCode = ep->ExceptionRecord->ExceptionCode;
        g_sehAddr = ep->ExceptionRecord->ExceptionAddress;
        HMODULE m = nullptr;
        if (g_sehAddr &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(g_sehAddr), &m) && m) {
            wchar_t wpath[MAX_PATH]{};
            GetModuleFileNameW(m, wpath, MAX_PATH);
            size_t k = 0;
            for (; wpath[k] && k < MAX_PATH - 1; ++k) g_sehMod[k] = static_cast<char>(wpath[k]);
            g_sehMod[k] = 0;
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool shutdownSinkSeh(veyra::gfx::PresentSink& sink)
{
    __try {
        sink.shutdown();
        return true;
    }
    __except (sehFilter(GetExceptionInformation())) {
        return false;
    }
}
} // namespace

int main(int argc, char** argv)
{
    std::wstring input, runtimeDir = L"runtime_local\\nvidia", logFile, jsonFile;
    std::string runId = "player-probe";
    bool endurance = false;
    int durationSeconds = 300;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        auto wvalue = [&]() -> std::wstring { const std::string v = value(); return std::wstring(v.begin(), v.end()); };
        if (arg == "--input") input = wvalue();
        else if (arg == "--runtime-dir") runtimeDir = wvalue();
        else if (arg == "--log-file") logFile = wvalue();
        else if (arg == "--json-file") jsonFile = wvalue();
        else if (arg == "--run-id") runId = value();
        else if (arg == "--endurance") endurance = true;
        else if (arg == "--duration-seconds") durationSeconds = std::atoi(value().c_str());
        else { std::fprintf(stderr, "unknown arg %s\n", arg.c_str()); return 2; }
    }
    if (input.empty()) { std::fprintf(stderr, "--input required\n"); return 2; }
    if (!logFile.empty()) (void)veyra::Logger::instance().openFile(logFile);

    bool playPauseWorks = false, seekWorks = false, resizeWorks = false;
    bool fgToggleWorks = false, nrToggleWorks = false, srToggleWorks = false;
    int seekCount = 0;
    EngineMetrics metrics;
    uint64_t audioUnderruns = 0, audioOverruns = 0;
    double g_audioEndBufferedMs = 0.0, g_audioEndHeadPtsMs = -1.0, g_audioEndClockPtsMs = -1.0;
    double driftMinMs = 0.0, driftP50Ms = 0.0, driftP95Ms = 1e9, driftP99Ms = 0.0, driftMaxMs = 0.0;
    uint64_t latenessSampleCount = 0;
    uint64_t g_droppedSourceFrames = 0, g_droppedGeneratedFrames = 0;
    uint64_t g_graphNrEval = 0, g_graphSrEval = 0, g_graphNvofExec = 0, g_graphFgGen = 0, g_graphNvofFail = 0;
    std::string g_graphMvecSource = "nvof";
    std::string mvecSource = "nvof";
    uint64_t droppedLatePresents = 0;
    bool g_d3dDiagEnabled = false;
    ID3D12InfoQueue* g_d3dDiagQueue = nullptr;
    uint64_t g_d3dDiagErrors = 0, g_d3dDiagCorruption = 0;
    uint64_t g_d3dDiagWarnings = 0, g_d3dDiagInfo = 0;
    bool g_diagRetrievalComplete = true;
    UINT64 g_diagStoredRuntime = 0, g_diagRetrievedRuntime = 0, g_diagRetrievalFailures = 0;
    UINT64 g_diagQueueCapacity = 0;
    bool g_diagQueueSaturated = false;
    UINT64 g_diagStartupStored = 0, g_diagTeardownStored = 0, g_diagTeardownRetrieved = 0;
    uint64_t g_diagTeardownErrors = 0, g_diagTeardownCorruption = 0;
    double g_audioLastPrefillMs = 0.0, g_audioFirstPtsAfterSeekMs = -1.0;
    uint64_t g_audioSeekCount = 0;
    uint64_t nvofFrameFailures = 0;
    uint64_t lastNvofSignal = 0;  // set inside the run scope; JSON uses it after
    uint64_t invalidDriftSamples = 0;  // legacy field (kept for schema compat)
    double end4k30Duration = 0, end4k60Duration = 0;
    uint64_t end4k60Fg = 0, end4k30Fg = 0;
    double end4k60Hz = 0, end4k30Hz = 0;
    double workingSetGrowthMB = 0;
    bool overall = false;

    PROCESS_MEMORY_COUNTERS memStart{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memStart, sizeof(memStart));

    veyra::media::FFmpegDemuxer demuxer;
    veyra::media::FFmpegVideoDecoder decoder;
    veyra::sink::AudioPipeline audioPipe;
    veyra::sink::AudioRenderer audio;
    veyra::gfx::D3D12DeviceContext context;
    veyra::gfx::CommandSlotRing ring;
    veyra::gfx::PresentSink sink;
    // The NGX/NVOF backend stack now lives inside the EnhanceGraph product
    // library (R3.2); this probe only assembles, schedules, presents, reports.
    SwsContext* nv12Ctx = nullptr;
    HANDLE nvofOutEvent = nullptr;

    // s8 item 5 (hoisted to main scope for s10): parsed BEFORE the run
    // scope so device creation and the post-scope teardown scans both see it.
    // s8 item 5: explicit diagnostic switch. VEYRA_D3D_DIAG=1 enables the
    // D3D12 debug layer + GPU-Based Validation + synchronized queue
    // validation + DRED BEFORE device creation, and an InfoQueue scan at
    // engine end that fails the run on any ERROR/CORRUPTION.
    int d3dDiagLevel = 0;
    {
        char lv[8]{};
        GetEnvironmentVariableA("VEYRA_D3D_DIAG", lv, sizeof(lv));
        if (lv[0]) d3dDiagLevel = std::atoi(lv);
    }
    const bool d3dDiag = d3dDiagLevel >= 1;
    // s10-II item 1: ONE owning InfoQueue ComPtr at OUTER scope; it must
    // outlive the resource scope because the teardown/final scans run
    // after ring/swapchain shutdown, right before context.shutdown.
    ComPtr<ID3D12InfoQueue> d3dDiagQueue;
    std::unordered_map<std::string, uint64_t> diagHistogram;
    std::vector<std::string> diagErrorSamples, diagCorruptionSamples, diagWarningSamples;

    // ---- s10-II: reliable InfoQueue retrieval (one function, all phases)
    auto scanInfoQueue = [&](const char* phase,
                             uint64_t& outErr, uint64_t& outCorr,
                             uint64_t& outWarn, uint64_t& outInfo,
                             UINT64& outStored, UINT64& outRetrieved, UINT64& outFailures,
                             std::unordered_map<std::string, uint64_t>& hist,
                             std::vector<std::string>& errSamples,
                             std::vector<std::string>& corrSamples) -> bool {
        outErr = outCorr = outWarn = outInfo = 0;
        outStored = outRetrieved = outFailures = 0;
        hist.clear(); errSamples.clear(); corrSamples.clear();
        if (d3dDiagQueue.Get() == nullptr) return false;
        outStored = d3dDiagQueue->GetNumStoredMessages();
        std::vector<char> buf(4096);
        for (UINT64 k = 0; k < outStored; ++k) {
            SIZE_T len = buf.size();
            D3D12_MESSAGE* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (d3dDiagQueue->GetMessageW(k, m, &len) != S_OK) { ++outFailures; continue; }
            ++outRetrieved;
            switch (m->Severity) {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                ++outCorr;
                if (corrSamples.size() < 4) corrSamples.push_back(std::format("id={} {}",
                    static_cast<unsigned>(m->ID), m->pDescription ? m->pDescription : ""));
                break;
            case D3D12_MESSAGE_SEVERITY_ERROR:
                ++outErr;
                if (errSamples.size() < 4) errSamples.push_back(std::format("id={} {}",
                    static_cast<unsigned>(m->ID), m->pDescription ? m->pDescription : ""));
                break;
            case D3D12_MESSAGE_SEVERITY_WARNING: ++outWarn; break;
            case D3D12_MESSAGE_SEVERITY_INFO: ++outInfo; break;
            default: break;
            }
            hist[std::to_string(static_cast<unsigned>(m->ID))]++;
        }
        veyra::log::info("player", std::format(
            "diag({}): stored={} retrieved={} failures={} err={} corr={} warn={} info={}",
            phase, outStored, outRetrieved, outFailures, outErr, outCorr, outWarn, outInfo));
        return outFailures == 0 && outRetrieved == outStored;
    };

    auto stage = [](const char* phase) {
        veyra::log::info("teardown", std::string("before ") + phase);
        veyra::Logger::instance().flush();
    };
    auto stageDone = [](const char* phase) {
        veyra::log::info("teardown", std::string("after ") + phase);
        veyra::Logger::instance().flush();
    };

    do { // one scope; break = early exit with teardown at the end
        veyra::Status st = veyra::Status::Ok;
        if (!demuxer.open(input)) { veyra::log::error("player", "demuxer open failed"); break; }
        const int64_t durationUs = demuxer.durationUs();

        const bool hasAudio = audioPipe.open(input);
        if (!hasAudio) veyra::log::info("player", "no audio; explicit fallback clock required");

        // --- GPU context ----------------------------------------------------
        if (d3dDiag) {
            ComPtr<ID3D12Debug1> dbg1;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg1)))) {
                dbg1->EnableDebugLayer();
                if (d3dDiagLevel >= 2) {
                    dbg1->SetEnableGPUBasedValidation(TRUE);
                    dbg1->SetEnableSynchronizedCommandQueueValidation(TRUE);
                    veyra::log::info("player", "diag level 2: layer + GBV + sync queue validation ON");
                } else {
                    veyra::log::info("player", "diag level 1: layer + DRED ON (GBV off; see iso matrix note)");
                }
            }
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredS;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredS)))) {
                dredS->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dredS->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }
        }
        veyra::gfx::DeviceContextDesc ddesc{};
        ddesc.enableDebugLayer = d3dDiag;
        ddesc.commandSlotCount = 4;
        if (!context.initialize(ddesc, st)) break;
        if (!ring.initialize(context.device(), context.directQueue(), context.fence(),
                context.fenceEvent(), 4, st)) break;

        // BARE-PRESENT isolation matrix REMOVED (R3.3a, 2026-09-06): the
        // investigation it served closed at the MipLevels=0 SRV root cause
        // (session 3); the standing diagnostic is descriptor_present_probe
        // (21/21). The full matrix remains in git history at 5d3ad85^.



        // Software decode is the working path: D3D12VA decodes are slowed to
        // ~8fps by the injected layer once descriptor views exist (pool
        // warm-up fixes allocation, not the per-decode recording). The sw
        // decoder leaks ~0.5-1.2% of frame bytes per frame in this vcpkg
        // build (proportional to resolution), so the decoder context is
        // recycled periodically to release it; the demuxer stays open.
        bool hwDecode = false;
        if (GetEnvironmentVariableW(L"VEYRA_HW_DECODE", nullptr, 0) != 0) {
            hwDecode = decoder.openD3D12VA(demuxer.videoCodecParameters(),
                demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen(),
                context.device(), context.directQueue());
        }
        if (!hwDecode) {
            if (!decoder.openSoftware(demuxer.videoCodecParameters(),
                    demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen())) {
                veyra::log::error("player", "decoder open failed");
                break;
            }
        }
        const uint32_t srcW = static_cast<uint32_t>(decoder.width());
        const uint32_t srcH = static_cast<uint32_t>(decoder.height());
        uint32_t decodeRecycleFrames = 1200;
        {
            char buf[16]{};
            GetEnvironmentVariableA("VEYRA_DECODE_RECYCLE", buf, sizeof(buf));
            if (buf[0]) decodeRecycleFrames = static_cast<uint32_t>(std::atoi(buf));
        }
        uint64_t framesSinceRecycle = 0;
        veyra::log::info("player", std::format("source {}x{} durationMs={} hwDecode={} recycleEvery={}",
            srcW, srcH, durationUs / 1000, hwDecode ? 1 : 0, decodeRecycleFrames));

        if (hwDecode) {
            int warmed = 0;
            for (int i = 0; i < 8; ++i) {
                bool eof = false;
                if (!demuxer.readVideoPacket(eof)) break;
                if (!decoder.sendPacket(demuxer.currentPacket())) continue;
                if (decoder.receiveFrame() != nullptr) ++warmed;
            }
            (void)ring.waitIdle();
            demuxer.seekToUs(0);
            decoder.flushBuffers();
            veyra::log::info("player", std::format("D3D12VA pool warmed ({} frames) and rewound", warmed));
        }

        const uint32_t workW = 3840, workH = 2160;
        const bool srNeeded = (srcW != workW) || (srcH != workH);

        // --- EnhanceGraph product library (R3.2): resources, NGX/NVOF backends,
        // compute passes and static views are owned and ordered by the graph
        // itself. The probe keeps scheduling, presenting, and reporting only.
        const bool noFeatures = GetEnvironmentVariableW(L"VEYRA_NO_FEATURES", nullptr, 0) != 0;
        const bool noNgx = GetEnvironmentVariableW(L"VEYRA_NO_NGX", nullptr, 0) != 0;
        const bool srEnabled = srNeeded; // 1:1 bypass otherwise
        const bool nrEnabled = GetEnvironmentVariableW(L"VEYRA_NR_OFF", nullptr, 0) == 0 &&
                               (noFeatures || noNgx ? false : true);
        const bool fgEnabled = GetEnvironmentVariableW(L"VEYRA_FG_OFF", nullptr, 0) == 0 &&
                               !noNgx;
        wchar_t absRuntime[MAX_PATH * 2]{};
        GetFullPathNameW(runtimeDir.c_str(), MAX_PATH * 2, absRuntime, nullptr);
        veyra::pipeline::EnhanceGraphDesc graphDesc{};
        graphDesc.sourceWidth = srcW;
        graphDesc.sourceHeight = srcH;
        graphDesc.workWidth = workW;
        graphDesc.workHeight = workH;
        graphDesc.enableSr = srEnabled;
        graphDesc.enableNr = nrEnabled;
        graphDesc.enableFg = fgEnabled;
        graphDesc.noFeatures = noFeatures;
        graphDesc.noNgx = noNgx;
        graphDesc.runtimeAbsPath = absRuntime;
        veyra::pipeline::EnhanceGraph graph(context, ring);
        if (!graph.initialize(graphDesc)) {
            veyra::log::error("player", "EnhanceGraph initialize failed");
            break;
        }
        const auto& gmetrics = graph.metrics();

        // --- PresentSink (swapchain allocs also precede views) ---------------
        veyra::gfx::PresentSink::Desc sinkDesc{};
        sinkDesc.width = std::min<uint32_t>(workW, 1920);
        sinkDesc.height = std::min<uint32_t>(workH, 1080);
        {
            char bw[8]{}, bh[8]{};
            GetEnvironmentVariableA("VEYRA_SINK_W", bw, sizeof(bw));
            GetEnvironmentVariableA("VEYRA_SINK_H", bh, sizeof(bh));
            if (bw[0]) sinkDesc.width = static_cast<uint32_t>(std::atoi(bw));
            if (bh[0]) sinkDesc.height = static_cast<uint32_t>(std::atoi(bh));
        }
        sinkDesc.vsync = false;
        sinkDesc.title = L"Veyra Player Probe";
        if (!sink.initialize(context.device(), context.directQueue(), sinkDesc, st)) break;

        // --- Present pass (probe-owned: blits graph output to backbuffers) ---
        const bool skipPasses = GetEnvironmentVariableW(L"VEYRA_SKIP_PASSES", nullptr, 0) != 0;
        const bool skipViews = GetEnvironmentVariableW(L"VEYRA_SKIP_VIEWS", nullptr, 0) != 0;
        const bool viewsTex  = GetEnvironmentVariableW(L"VEYRA_VIEWS_TEX", nullptr, 0) != 0;
        GraphicsPass presentPass;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        UINT rtvIncrement = 0;
        {
            if (!skipPasses) {
                std::vector<uint8_t> vs, ps;
                if (!loadShaderBytes("PresentBlit_vs.dxil", vs) ||
                    !loadShaderBytes("PresentBlit_ps.dxil", ps) ||
                    !presentPass.create(context.device(), vs, ps, 8)) {
                    veyra::log::error("player", "present graphics pass creation failed");
                    break;
                }
            }
            D3D12_DESCRIPTOR_HEAP_DESC rh{};
            rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rh.NumDescriptors = 3;
            if (FAILED(context.device()->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap)))) {
                veyra::log::error("player", "RTV heap creation failed");
                break;
            }
            rtvIncrement = context.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        auto refreshBackbufferRtvs = [&]() {
            for (UINT bb = 0; bb < 3; ++bb) {
                ComPtr<ID3D12Resource> backBuffer;
                if (SUCCEEDED(sink.swapChain()->GetBuffer(bb, IID_PPV_ARGS(&backBuffer)))) {
                    context.device()->CreateRenderTargetView(backBuffer.Get(), nullptr,
                        { rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bb * rtvIncrement });
                }
            }
        };
        refreshBackbufferRtvs();

        // --- Audio start (after GPU init) ------------------------------------
        if (hasAudio) {
            if (!audio.start(audioPipe.pcmFormat())) {
                veyra::log::error("player", "audio renderer open failed");
                break;
            }
            audioPipe.startThread(&audio);  // prefill + anchored Start on thread
        }

        // --- Static descriptor views (graph's own, then present SRVs) ---------
        if (!skipPasses && !skipViews) {
            if (!graph.createViews()) {
                veyra::log::error("player", "EnhanceGraph static views failed");
                break;
            }
            // Present pass SRVs: 0/1=genFrame[0/1], 2/3=videoFrame[0/1].
            // Unconditional since the RAW-SRV isolation (2026-09-06): texture
            // SRV initialization is proven present-safe; leaving them
            // uninitialized was the Draw-path GBV id=938 source.
            makeSrv(context.device(), graph.generatedFrameResource(0), DXGI_FORMAT_R8G8B8A8_UNORM,
                { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr });
            makeSrv(context.device(), graph.generatedFrameResource(1), DXGI_FORMAT_R8G8B8A8_UNORM,
                { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr + presentPass.increment });
            makeSrv(context.device(), graph.videoFrameResource(0), DXGI_FORMAT_R8G8B8A8_UNORM,
                { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr + 2ull * presentPass.increment });
            makeSrv(context.device(), graph.videoFrameResource(1), DXGI_FORMAT_R8G8B8A8_UNORM,
                { presentPass.heap->GetCPUDescriptorHandleForHeapStart().ptr + 3ull * presentPass.increment });
        }

        // k-incremental staged-SRV experiment (present-path isolation).
        {
            char texN[8]{};
            GetEnvironmentVariableA("VEYRA_TEX_N", texN, sizeof(texN));
            const int k = texN[0] ? std::atoi(texN) : 0;
            if (k > 0) {
                D3D12_DESCRIPTOR_HEAP_DESC hk{};
                hk.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                hk.NumDescriptors = 64;
                hk.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ComPtr<ID3D12DescriptorHeap> heapK;
                if (FAILED(context.device()->CreateDescriptorHeap(&hk, IID_PPV_ARGS(&heapK)))) break;
                ComPtr<ID3D12Resource> texK = makeTexture(context.device(), 64, 64,
                    DXGI_FORMAT_R8G8B8A8_UNORM, false);
                if (!texK) break;
                D3D12_SHADER_RESOURCE_VIEW_DESC sk{};
                sk.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sk.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sk.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                DescriptorStager stagerK;
                if (stagerK.initialize(context.device(), 64)) {
                    for (int i = 0; i < k; ++i) {
                        stagerK.stageSrv(texK.Get(), &sk, heapK.Get(), static_cast<UINT>(i));
                    }
                }
                veyra::log::info("player", std::format("k-experiment: staged {} SRVs into a fresh visible heap", k));
            }
        }

        // --- State tracker ----------------------------------------------------
        uint64_t d3dDiagErrors = 0, d3dDiagCorruption = 0;
        uint64_t d3dDiagWarnings = 0, d3dDiagInfo = 0;
        UINT64 diagStartupStored = 0, diagStartupRetrieved = 0;
        bool diagRetrievalComplete = true;
        UINT64 diagStoredRuntime = 0, diagRetrievedRuntime = 0, diagRetrievalFailures = 0;
        UINT64 diagQueueCapacity = 0;
        bool diagQueueSaturated = false;
        bool diagActive = false;
        if (d3dDiag) {
            if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&d3dDiagQueue))) && d3dDiagQueue.Get()) {
                D3D12_INFO_QUEUE_FILTER noFilter{};
                d3dDiagQueue->PushStorageFilter(&noFilter);  // record all
                // Default storage is 1024 messages; a 4K run generates more
                // INFO and the queue DROPS messages when full (and
                // GetMessageW can then fail wholesale - observed failures=
                // 1024 with cap=1024 saturated=1). Unlimited (-1) storage
                // removes the cap; the runtime phase is cleared after
                // startup so counts stay meaningful.
                d3dDiagQueue->SetMessageCountLimit(static_cast<UINT64>(-1));
                diagActive = true;
                g_d3dDiagQueue = d3dDiagQueue.Get();
                // s9-B phase 1 (startup): count creation-time messages, then
                // clear so the measured-runtime phase starts empty.
                diagStartupStored = d3dDiagQueue->GetNumStoredMessages();
                d3dDiagQueue->ClearStoredMessages();
                veyra::log::info("player", std::format("diag(startup): stored-cleared={}", diagStartupStored));
            } else {
                veyra::log::error("player", "diag: requested but InfoQueue attach FAILED (fail closed)");
                diagRetrievalComplete = false;
            }
        }
        // P0.3: bounded 6-slot display pool with explicit slot ownership.
        // genFrame[2] joins videoFrame[2] as dedicated slots (real frames use
        // slots 0..1, generated frames own gen slots); a 6-entry queue cap
        // provides backpressure - frames are never dropped for sync.
        StateTracker presentTracker; // present-path source textures only
        uint64_t resetEpoch = 1;                 // bumped on seek/loop/reset
        std::vector<double> latenessSamples;     // P0.2 signed, pre-decision
        bool slotFree[2] = { true, true };       // P0.3 real-slot liveness
        uint64_t droppedSourceFrames = 0;
        uint64_t droppedGeneratedFrames = 0;
        // P0.3: every queue item owns its exact texture slot; the present
        // path may only display the item's own resource. Bounded 6-slot
        // pool gives backpressure instead of frame dropping.
        struct PresentItem {
            double dueMs;            // target present time (media clock)
            int kind;                // 0 = real, 1 = generated
            uint64_t ptsMs;          // media PTS of the content
            uint64_t frameSeq;       // producer frame sequence
            uint32_t textureSlot;    // real: videoFrame[slot]; gen: genFrame[slot]
            uint64_t epoch;          // reset epoch (seek/loop); older epoch invalidates
            uint64_t fenceValue;     // producer fence: item displayable once completed
        };
        std::deque<PresentItem> presentQueue;
        uint64_t realFrameIndex = 0;
        int lastParity = 0;
        bool prevValid = false;
        double prevPtsMs = 0.0;

        const auto qpcNowMs = [] {
            return 1000.0 * static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1e9;
        };
        double wallEpochMs = qpcNowMs();
        const auto mediaTimeMs = [&]() -> double {
            return hasAudio ? audio.mediaTimeMs() : (qpcNowMs() - wallEpochMs);
        };

        // --- P0.5: per-pass GPU timing (diagnostic, serialized) --------------
        const bool gpuTs = GetEnvironmentVariableW(L"VEYRA_GPU_TS", nullptr, 0) != 0;
        std::vector<std::pair<const char*, double>> passTimings; // name, ms
        auto gpuMark = [&](const char* name) {
            if (!gpuTs) return;
            // Signal a private fence and wait: the QPC delta at completion
            // approximates this pass's GPU cost (serialized mode).
            static ComPtr<ID3D12Fence> tsFence;
            static HANDLE tsEvent = nullptr;
            static uint64_t tsVal = 0;
            static bool tsInit = false;
            if (!tsInit) {
                context.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tsFence));
                tsEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                tsInit = true;
            }
            const auto t0 = std::chrono::steady_clock::now();
            ++tsVal;
            context.directQueue()->Signal(tsFence.Get(), tsVal);
            tsFence->SetEventOnCompletion(tsVal, tsEvent);
            WaitForSingleObject(tsEvent, 2000);
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            passTimings.emplace_back(name, ms);
        };
        auto gpuReport = [&]() {
            if (!gpuTs) return;
            // Aggregate per pass name.
            std::unordered_map<std::string, std::vector<double>> agg;
            for (auto& [n, ms] : passTimings) agg[n].push_back(ms);
            std::string line = "gpu-pass-ms:";
            for (auto& [n, v] : agg) {
                std::sort(v.begin(), v.end());
                const double p50 = v[v.size() / 2];
                const double p95 = v[std::min(v.size() - 1, static_cast<size_t>(0.95 * (v.size() - 1)))];
                line += std::format(" {} n={} p50={:.2f} p95={:.2f};", n, v.size(), p50, p95);
            }
            veyra::log::info("player-ts", line);
            passTimings.clear();
        };

        // --- Per-frame graph (thin wrapper: the chain lives in EnhanceGraph) -
        auto processOneFrame = [&](const AVFrame* frame) -> bool {
            if (frame->pts == AV_NOPTS_VALUE || frame->pts < 0) {
                return true; // no timestamp: skip this frame entirely
            }
            const double ptsMs = 1000.0 * frame->pts * demuxer.videoTimeBaseNum() /
                demuxer.videoTimeBaseDen();
            veyra::pipeline::EnhanceGraph::FrameOutputs out;
            if (!graph.process(frame, ptsMs, !prevValid, out)) return false;
            if (!out.passthrough && out.realFrameIndex == 0) {
                return true; // frame skipped inside the graph (no timestamp)
            }
            if (out.hasGenerated) {
                // Present order: previous real (already queued) -> generated -> current real.
                presentQueue.push_back({ out.generatedPtsMs, 1,
                    static_cast<uint64_t>(out.generatedPtsMs),
                    out.realFrameIndex, out.genSlot, resetEpoch,
                    out.genFenceValue });
            }
            presentQueue.push_back({ ptsMs, 0, static_cast<uint64_t>(ptsMs),
                out.realFrameIndex, out.videoSlot, resetEpoch,
                out.videoFenceValue });
            prevPtsMs = ptsMs;
            prevValid = true;
            lastParity = static_cast<int>(out.videoSlot);
            realFrameIndex = out.realFrameIndex;
            metrics.maxInFlight = std::max<int64_t>(metrics.maxInFlight,
                static_cast<int64_t>(presentQueue.size()));
            return true;
        };

        auto pumpPresents = [&]() -> bool {
            bool closed = false;
            if (!sink.processMessages(closed) || closed) return false;
            int budget = 8;
            while (!presentQueue.empty() && budget-- > 0) {
                const PresentItem item = presentQueue.front();
                // P0.2: signed lateness recorded BEFORE the present decision.
                const double actualMs = mediaTimeMs();
                const double lateness = actualMs - item.dueMs;
                if (item.epoch == resetEpoch) {   // only in-epoch items count
                    latenessSamples.push_back(lateness);
                }
                presentQueue.pop_front();

                // P0.3: display THIS item's own texture only.
                ID3D12Resource* source = item.kind == 1
                    ? graph.generatedFrameResource(item.textureSlot)
                    : graph.videoFrameResource(item.textureSlot);
                if (source == nullptr) {
                    veyra::log::error("player", "P5 null item texture");
                    return false;
                }
                ID3D12GraphicsCommandList* list = ring.acquire(3, st);
                if (list == nullptr) { veyra::log::error("player", "P2 acquire present"); return false; }
                ID3D12Resource* back = sink.currentBackBuffer();
                if (back == nullptr) { veyra::log::error("player", "P5 null backbuffer"); return false; }
                const UINT bbIndex = sink.swapChain()->GetCurrentBackBufferIndex();
                {
                    // Backbuffer transitions are unconditional: flip buffers
                    // rotate (and are replaced on resize), so the per-resource
                    // tracker does not apply to them.
                    D3D12_RESOURCE_BARRIER b[2]{};
                    b[0].Transition.pResource = source;
                    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b[1].Transition.pResource = back;
                    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    list->ResourceBarrier(2, b);
                    presentTracker.set(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                const bool clearPresent = GetEnvironmentVariableW(L"VEYRA_CLEAR_PRESENT", nullptr, 0) != 0;
                if (!clearPresent) {
                    const UINT srvSlot = item.kind == 1 ? 0 : (1 + item.textureSlot);
                    const float constants[16] = {
                        static_cast<float>(workW), static_cast<float>(workH), 0, 0,
                        static_cast<float>(sink.width()), static_cast<float>(sink.height()), 0, 0 };
                    ID3D12DescriptorHeap* heaps[] = { presentPass.heap.Get() };
                    list->SetDescriptorHeaps(1, heaps);
                    list->SetGraphicsRootSignature(presentPass.rootSig.Get());
                    list->SetPipelineState(presentPass.pso.Get());
                    list->SetGraphicsRoot32BitConstants(0, 16, constants, 0);
                    list->SetGraphicsRootDescriptorTable(1,
                        { presentPass.heap->GetGPUDescriptorHandleForHeapStart().ptr + srvSlot * presentPass.increment });
                    D3D12_VIEWPORT vp{ 0.0f, 0.0f,
                        static_cast<float>(sink.bufferWidth()), static_cast<float>(sink.bufferHeight()), 0.0f, 1.0f };
                    D3D12_RECT sc{ 0, 0, static_cast<LONG>(sink.bufferWidth()), static_cast<LONG>(sink.bufferHeight()) };
                    list->RSSetViewports(1, &vp);
                    list->RSSetScissorRects(1, &sc);
                    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{
                        rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bbIndex * rtvIncrement };
                    list->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                } else {
                    const float gray[4] = { 0.3f, 0.3f, 0.35f, 1.0f };
                    list->ClearRenderTargetView(
                        { rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + bbIndex * rtvIncrement },
                        gray, 0, nullptr);
                }
                {
                    D3D12_RESOURCE_BARRIER b[2]{};
                    b[0].Transition.pResource = source;
                    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b[1].Transition.pResource = back;
                    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    list->ResourceBarrier(2, b);
                    presentTracker.set(source, D3D12_RESOURCE_STATE_COMMON);
                }
                if (!ring.submitAndSignal(3)) { veyra::log::error("player", "P3 submit"); return false; }
                if (!sink.present(st)) { veyra::log::error("player", "P4 present"); return false; }
                ++metrics.presentCount;
                // Slot is free for reuse once this present is consumed.
                slotFree[item.textureSlot] = true;
                if (item.kind == 0) {
                    ++metrics.realFramesPresented;
                }
            }
            return true;
        };

        auto decodeNextFrame = [&]() -> const AVFrame* {
            static const bool demuxOnly = GetEnvironmentVariableW(L"VEYRA_DEMUX_ONLY", nullptr, 0) != 0;
            if (decodeRecycleFrames > 0 && framesSinceRecycle >= decodeRecycleFrames) {
                framesSinceRecycle = 0;
                decoder.close();
                const bool reopened = hwDecode
                    ? decoder.openD3D12VA(demuxer.videoCodecParameters(),
                          demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen(),
                          context.device(), context.directQueue())
                    : decoder.openSoftware(demuxer.videoCodecParameters(),
                          demuxer.videoTimeBaseNum(), demuxer.videoTimeBaseDen());
                veyra::log::info("player", std::format("decoder recycled (reopened={})", reopened ? 1 : 0));
                if (!reopened) return nullptr;
            }
            for (;;) {
                bool eof = false;
                if (!demuxer.readVideoPacket(eof)) {
                    if (eof) {
                        decoder.sendPacket(nullptr);
                        return decoder.receiveFrame();
                    }
                    return nullptr;
                }
                if (demuxOnly) continue; // ownership probe: demux path only
                if (!decoder.sendPacket(demuxer.currentPacket())) continue;
                const AVFrame* f = decoder.receiveFrame();
                if (f != nullptr) return f;
            }
        };

        auto resetAfterSeek = [&](double targetMs) {
            presentQueue.clear();
            ++resetEpoch;   // P0.3: old-epoch items can never display
            prevValid = false;
            if (hasAudio) {
                // Atomic: stop/reset WASAPI, flush, seek, prune, prefill,
                // re-anchor clock, start. Returns real first PTS >= target.
                (void)audioPipe.requestSeek(targetMs);
            }
            wallEpochMs = qpcNowMs() - targetMs;
        };

        auto resyncToAudio = [&]() {
            const double target = std::max(0.0, mediaTimeMs());
            if (!demuxer.seekToUs(static_cast<int64_t>(target) * 1000)) return;
            decoder.flushBuffers();
            resetAfterSeek(target);
            (void)ring.waitIdle();
            veyra::log::info("player", std::format("resynced video to audio at {:.0f}ms", target));
        };

        LARGE_INTEGER hpf{}, t0{};
        QueryPerformanceFrequency(&hpf);
        auto runPlayback = [&](double seconds, int frameStride) -> bool {
            QueryPerformanceCounter(&t0);
            uint64_t strideCounter = 0;
            uint64_t lastSamplePresents = 0, lastSampleFg = 0;
            double lastSampleSec = 0.0;
            for (;;) {
                LARGE_INTEGER now{};
                QueryPerformanceCounter(&now);
                const double elapsed = static_cast<double>(now.QuadPart - t0.QuadPart) /
                    static_cast<double>(hpf.QuadPart);
                if (elapsed - lastSampleSec >= 10.0) {
                    PROCESS_MEMORY_COUNTERS pmc{};
                    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
                    veyra::log::info("player", std::format("pace t={:.0f}s ws={}MB presents={}/10s fg={}/10s queue={}",
                        elapsed, pmc.WorkingSetSize / (1024 * 1024),
                        metrics.presentCount - lastSamplePresents,
                        g_graphFgGen - lastSampleFg,
                        presentQueue.size()));
                    lastSamplePresents = metrics.presentCount;
                    lastSampleFg = g_graphFgGen;
                    lastSampleSec = elapsed;
                }
                if (elapsed >= seconds) break;
                // Audio pump + decode production run on the dedicated
                // audio thread (watermarked); nothing to do here.
                const double nowMs = mediaTimeMs();
                if (presentQueue.size() < 3) {
                    const AVFrame* f = decodeNextFrame();
                    if (f == nullptr) {
                        demuxer.seekToUs(0);
                        decoder.flushBuffers();
                        resetAfterSeek(0.0);
                        continue;
                    }
                    ++strideCounter;
                    ++framesSinceRecycle;
                    if (frameStride > 1 && (strideCounter % frameStride) != 1) continue;
                    if (!processOneFrame(f)) return false;
                    if (gpuTs && (g_graphFgGen % 120 == 0)) gpuReport();
                }
                static const bool presentOff = GetEnvironmentVariableW(L"VEYRA_PRESENT_OFF", nullptr, 0) != 0;
                if (!presentOff && !pumpPresents()) return false;
                if (presentOff) {
                    bool closed2 = false;
                    (void)sink.processMessages(closed2);
                    // discard without presenting to keep the queue bounded
                    while (!presentQueue.empty() && presentQueue.front().dueMs <= mediaTimeMs()) presentQueue.pop_front();
                }
                std::this_thread::sleep_for(std::chrono::microseconds(400));
            }
            return true;
        };

        if (endurance) {
            resyncToAudio();
            veyra::log::info("player", "endurance: 4K30 pass start");
            const uint64_t p0 = metrics.presentCount, f0 = g_graphFgGen;
            if (!runPlayback(static_cast<double>(durationSeconds), 2)) break;
            end4k30Duration = durationSeconds;
            end4k30Fg = g_graphFgGen - f0;
            end4k30Hz = static_cast<double>(metrics.presentCount - p0) / end4k30Duration;
            veyra::log::info("player", std::format("4K30: presents={} fg={} hz={:.1f}",
                metrics.presentCount - p0, end4k30Fg, end4k30Hz));
            resyncToAudio();
            veyra::log::info("player", "endurance: 4K60 pass start");
            const uint64_t p1 = metrics.presentCount, f1 = g_graphFgGen;
            if (!runPlayback(static_cast<double>(durationSeconds), 1)) break;
            end4k60Duration = durationSeconds;
            end4k60Fg = g_graphFgGen - f1;
            end4k60Hz = static_cast<double>(metrics.presentCount - p1) / end4k60Duration;
            veyra::log::info("player", std::format("4K60: presents={} fg={} hz={:.1f}",
                metrics.presentCount - p1, end4k60Fg, end4k60Hz));
            overall = (end4k30Hz >= 55.0) && (end4k60Hz >= 110.0) && end4k60Fg >= 8000 &&
                      metrics.normalPathReadbackCount == 0;
        } else {
            // Scenario.
            if (!runPlayback(5.0, 1)) break;
            playPauseWorks = metrics.presentCount > 60;

            if (hasAudio) {
                audio.stopAndReset();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                bool closed = false;
                (void)sink.processMessages(closed);
                (void)audioPipe.requestSeek(std::max(0.0, audio.mediaTimeMs()));
            }
            if (!runPlayback(2.0, 1)) break;
            playPauseWorks = playPauseWorks && metrics.presentCount > 100;

            bool seeksOk = true;
            for (int i = 0; i < 10; ++i) {
                const double targetMs = (durationUs / 1000.0) * (0.1 + 0.08 * i);
                if (!demuxer.seekToUs(static_cast<int64_t>(targetMs) * 1000)) { seeksOk = false; break; }
                decoder.flushBuffers();
                resetAfterSeek(targetMs);
                if (!runPlayback(0.8, 1)) { seeksOk = false; break; }
                ++seekCount;
            }
            seekWorks = seeksOk && seekCount >= 10;

            sink.resize(1280, 720);
            refreshBackbufferRtvs();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sink.resize(sinkDesc.width, sinkDesc.height);
            refreshBackbufferRtvs();
            resizeWorks = sink.width() == sinkDesc.width && sink.height() == sinkDesc.height;
            if (!runPlayback(1.0, 1)) break;

            // NR toggle test only applies when the feature handle exists; in
            // NO_FEATURES/no-NGX control runs enabling nrEnabled without a
            // handle made the frame path call Evaluate(nullptr) (SEGV caught
            // by the adapter's SEH, run aborted, teardown skipped - t10-NF).
            nrToggleWorks = false;
            if (graph.nrCreated()) {
                graph.setNrEnabled(false);
                (void)ring.waitIdle();
                if (!runPlayback(1.0, 1)) break;
                graph.setNrEnabled(true);
                (void)ring.waitIdle();
                if (!runPlayback(1.0, 1)) break;
                nrToggleWorks = g_graphNrEval > 0;
            }

            srToggleWorks = !srNeeded; // 1:1 bypass by definition; scaling toggle is Phase 7 UI scope

            if (graph.fgCreated()) {
                graph.setFgEnabled(false);
                if (!runPlayback(1.0, 1)) break;
                graph.setFgEnabled(true);
                if (!runPlayback(1.0, 1)) break;
                fgToggleWorks = g_graphFgGen > 0;
            }

            audioUnderruns = audio.underruns();
            audioOverruns = audioPipe.overruns();
            // P0.2: drift percentiles from signed lateness samples.
            {
                std::vector<double>& v = latenessSamples;
                if (!v.empty()) {
                    std::sort(v.begin(), v.end());
                    const auto pick = [&](double q) {
                        const size_t idx = std::min(v.size() - 1,
                            static_cast<size_t>(q * (v.size() - 1)));
                        return v[idx];
                    };
                    driftMinMs = v.front();
                    driftP50Ms = pick(0.50);
                    driftP95Ms = pick(0.95);
                    driftP99Ms = pick(0.99);
                    driftMaxMs = v.back();
                }
                latenessSampleCount = v.size();
            }
            g_droppedSourceFrames = droppedSourceFrames;
            g_droppedGeneratedFrames = droppedGeneratedFrames;
            g_graphNrEval = gmetrics.nrEvaluateCount;
            g_graphSrEval = gmetrics.srEvaluateCount;
            g_graphNvofExec = gmetrics.nvofExecuteCount;
            g_graphFgGen = gmetrics.fgGeneratedFrames;
            g_graphNvofFail = gmetrics.nvofFrameFailures;
            g_graphMvecSource = graph.mvecSource();
            g_audioEndBufferedMs = audioPipe.bufferedMs();
            g_audioEndHeadPtsMs = audioPipe.headPtsMs();
            g_audioEndClockPtsMs = audio.mediaTimeMs();
            g_audioSeekCount = audioPipe.seekCount();
            g_audioLastPrefillMs = audioPipe.lastPrefillMs();
            g_audioFirstPtsAfterSeekMs = audioPipe.firstPtsAfterLastSeek();

            // ---- s9-B: MEASURED-RUNTIME InfoQueue scan BEFORE stats/overall.
            // Stats copy and the overall verdict happen AFTER this block, so
            // diagnostic counts can no longer be "accidentally green".
            if (d3dDiag) {
                if (!d3dDiagQueue.Get()) {
                    // Requested diagnostics but the InfoQueue is absent:
                    // fail closed.
                    diagRetrievalComplete = false;
                    veyra::log::error("player", "diag: requested but InfoQueue unavailable (fail closed)");
                } else {
                    const UINT64 stored = d3dDiagQueue->GetNumStoredMessages();
                    UINT64 retrieved = 0, failures = 0;
                    std::vector<char> buf(4096);
                    for (UINT64 k = 0; k < stored; ++k) {
                        SIZE_T len = buf.size();
                        // Single-call retrieval with a fixed buffer: the
                        // two-step (length query then fill) fails wholesale
                        // under GBV (observed failures==stored).
                        D3D12_MESSAGE* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
                        if (d3dDiagQueue->GetMessageW(k, m, &len) != S_OK) { ++failures; continue; }
                        ++retrieved;
                        diagStoredRuntime = stored;
                        switch (m->Severity) {
                        case D3D12_MESSAGE_SEVERITY_CORRUPTION: ++d3dDiagCorruption; break;
                        case D3D12_MESSAGE_SEVERITY_ERROR:      ++d3dDiagErrors; break;
                        case D3D12_MESSAGE_SEVERITY_WARNING:    ++d3dDiagWarnings; break;
                        case D3D12_MESSAGE_SEVERITY_INFO:       ++d3dDiagInfo; break;
                        default: break;
                        }
                        // Message-ID histogram + first samples per class.
                        diagHistogram[std::to_string(static_cast<unsigned>(m->ID))]++;
                        auto noteSample = [&](std::vector<std::string>& v) {
                            if (v.size() < 4) v.push_back(std::format("id={} sev={} {}",
                                static_cast<unsigned>(m->ID), static_cast<unsigned>(m->Severity),
                                m->pDescription ? m->pDescription : ""));
                        };
                        if (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR) noteSample(diagErrorSamples);
                        else if (m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) noteSample(diagCorruptionSamples);
                        else if (m->Severity == D3D12_MESSAGE_SEVERITY_WARNING) noteSample(diagWarningSamples);
                    }
                    diagRetrievalComplete = (failures == 0) && (retrieved == stored);
                    diagRetrievedRuntime = retrieved;
                    diagRetrievalFailures = failures;
                    // Queue-capacity check: the debug layer's default storage
                    // is bounded; stored==cap means messages may be dropped.
                    ComPtr<ID3D12InfoQueue1> iq1;
                    if (SUCCEEDED(d3dDiagQueue->QueryInterface(IID_PPV_ARGS(&iq1)))) {
                        const UINT64 cap = iq1->GetMessageCountLimit();
                        if (cap > 0) {
                            diagQueueCapacity = cap;
                            diagQueueSaturated = (static_cast<UINT64>(stored) >= cap);
                        }
                    }
                    veyra::log::info("player", std::format(
                        "diag(runtime): stored={} retrieved={} failures={} err={} corr={} warn={} info={} cap={} saturated={}",
                        stored, retrieved, failures, d3dDiagErrors, d3dDiagCorruption,
                        d3dDiagWarnings, d3dDiagInfo, diagQueueCapacity, diagQueueSaturated ? 1 : 0));
                    // Drain so the teardown phase sees only teardown messages.
                    d3dDiagQueue->ClearStoredMessages();
                }
            }

            // P0.2: p95-based drift gate; dropped frames are hard failures.
            // s9-B: when diagnostics were requested, the verdict additionally
            // requires the scan to have completed with 0 ERROR / 0 CORRUPTION.
            const bool diagVerdictOk = !d3dDiag ||
                (d3dDiagQueue.Get() != nullptr &&
                 diagRetrievalComplete &&
                 d3dDiagErrors == 0 &&
                 d3dDiagCorruption == 0);
            overall = playPauseWorks && seekWorks && resizeWorks &&
                      nrToggleWorks && fgToggleWorks &&
                      metrics.presentCount >= 200 &&
                      driftP95Ms <= 50.0 &&
                      droppedSourceFrames == 0 &&
                      droppedLatePresents == 0 &&
                      audioUnderruns == 0 && audioOverruns == 0 &&
                      metrics.normalPathReadbackCount == 0 &&
                      diagVerdictOk;
            // Stats copied AFTER the scan (s9-B).
            g_d3dDiagEnabled = d3dDiag && d3dDiagQueue.Get() != nullptr;
            g_d3dDiagErrors = d3dDiagErrors;
            g_d3dDiagCorruption = d3dDiagCorruption;
            g_d3dDiagWarnings = d3dDiagWarnings;
            g_d3dDiagInfo = d3dDiagInfo;
            g_diagRetrievalComplete = diagRetrievalComplete;
            g_diagStoredRuntime = diagStoredRuntime;
            g_diagRetrievedRuntime = diagRetrievedRuntime;
            g_diagRetrievalFailures = diagRetrievalFailures;
            g_diagQueueCapacity = diagQueueCapacity;
            g_diagQueueSaturated = diagQueueSaturated;
        }
        // ---- s10-I: the ENTIRE teardown runs INSIDE the resource scope, in
        // true dependency order, and the process only leaves this scope via
        // natural destruction after everything is released. GPU resources
        // (inputA/B/flow/cost ComPtrs) stay alive until nvof.shutdown()
        // has unregistered them.
        lastNvofSignal = graph.lastNvofSignal();
        veyra::log::info("teardown", std::format("saved lastNvofSignal={}", lastNvofSignal));

        // s10-III: crash-honest evidence stub BEFORE teardown; only success
        // overwrites it after context.shutdown completes.
        {
            std::string stub = std::string("{\n  \"probe\": \"veyra_player_probe\",\n") +
                std::format("  \"runId\": \"{}\",\n", jsonEscape(runId)) +
                "  \"processCompleted\": false,\n  \"teardownCompleted\": false,\n  \"verdict\": \"INCOMPLETE\"\n}\n";
            if (!jsonFile.empty()) (void)utilns::writeTextFileUtf8(jsonFile, stub);
        }

        // 1. Stop producers / audio thread / new-frame submission.
        stage("sws-free");
        // nv12Ctx is owned by EnhanceGraph; freed inside graph.shutdown().
        stageDone("sws-free");
        stage("audio-thread-stop");
        audioPipe.stopThread();
        stageDone("audio-thread-stop");
        stage("wasapi-shutdown");
        audio.shutdown();
        stageDone("wasapi-shutdown");

        // 2. Application queue/ring completion.
        stage("ring-wait-idle");
        if (ring.initialized()) (void)ring.waitIdle();
        stageDone("ring-wait-idle");

        // 3. NVOF out-fence drain while session/fence/event are all valid.
        stage("nvof-out-fence-drain");
        if (graph.nvofSessionInitialized() && graph.nvofOutFenceResource() != nullptr && lastNvofSignal > 0) {
            const UINT64 completedBefore = graph.nvofOutFenceResource()->GetCompletedValue();
            DWORD waitResult = WAIT_OBJECT_0;
            if (completedBefore < lastNvofSignal) {
                const HRESULT hrSet = graph.nvofOutFenceResource()->SetEventOnCompletion(lastNvofSignal, graph.nvofOutEventHandle());
                if (FAILED(hrSet)) {
                    veyra::log::error("teardown", std::format(
                        "SetEventOnCompletion FAILED hr=0x{:X} expected={} completed={}",
                        static_cast<unsigned>(hrSet), lastNvofSignal, completedBefore));
                    overall = false;
                } else {
                    waitResult = WaitForSingleObject(graph.nvofOutEventHandle(), 5000);
                    if (waitResult != WAIT_OBJECT_0) {
                        veyra::log::error("teardown", std::format(
                            "NVOF fence drain FAILED waitResult={} expected={} completed={}",
                            waitResult, lastNvofSignal, graph.nvofOutFenceResource()->GetCompletedValue()));
                        overall = false;
                    }
                }
            }
            veyra::log::info("teardown", std::format(
                "nvof-out-fence-drain expected={} completedBefore={} completedAfter={} waitResult={}",
                lastNvofSignal, completedBefore, graph.nvofOutFenceResource()->GetCompletedValue(), waitResult));
            if (graph.nvofOutFenceResource()->GetCompletedValue() < lastNvofSignal) overall = false;
        }
        stageDone("nvof-out-fence-drain");

        // 4. Consumers of NVOF output may still have queued work.
        stage("ring-wait-idle-2");
        if (ring.initialized()) (void)ring.waitIdle();
        stageDone("ring-wait-idle-2");

        // 4b. The LAST Present was submitted after the final fence signal;
        // waitIdle cannot cover it. Enqueue a fresh signal (queue-ordered
        // after the Present) and wait, or the debug layer flags the
        // swapchain's final-release as in-flight (id=921 -> 0x87D).
        stage("queue-final-drain");
        if (ring.initialized() && !ring.drainQueue()) overall = false;
        stageDone("queue-final-drain");

        // 5-7. Everything the graph owns (NGX features, NVOF three-phase
        // teardown, NGX params/shim/core, compute passes, textures) is
        // released by the graph in the proven s10 order.
        stage("graph-shutdown");
        graph.shutdown();
        stageDone("graph-shutdown");

        stage("release-rtv-heap");
        rtvHeap.Reset();
        stageDone("release-rtv-heap");
        stage("release-present-pass");
        presentPass = GraphicsPass{};
        stageDone("release-present-pass");
        // ring/sink/context are NOT shut down here: their explicit teardown
        // runs AFTER this scope closes, because the scope-end destructors
        // Release every GPU resource ComPtr and those Release calls need the
        // device alive (crash observed when context.shutdown() preceded
        // scope destruction). Correct order: resources -> ring -> swapchain
        // -> device.


        veyra::log::info("player", "in-scope teardown complete; resources destruct next");
        veyra::Logger::instance().flush();

        // Scope ends here: GPU resources destruct in reverse declaration
        // order AFTER all sessions/features have been released; ring,
    } while (false);

    // Device-level teardown AFTER resource destruction (scope close above).
    // The swapchain must be released BEFORE the D3D12 queue it presents on
    // (flip-model dependency; queue-first was part of the 0x87D matrix).
    stage("sink-shutdown");
    if (!shutdownSinkSeh(sink)) {
        const char* mod = g_sehMod[0] ? g_sehMod : "?";
        const char* base = std::strrchr(mod, '\\');
        const char* fname = base ? base + 1 : mod;
        veyra::log::error("teardown", std::format(
            "sink-shutdown RAISED EXCEPTION code=0x{:X} addr={} module={} (evidence capture; run FAILS)",
            g_sehCode, g_sehAddr ? "present" : "null", fname));
        overall = false;
        // The debug layer raised break-on-error; the ERROR message that
        // triggered it is already in the InfoQueue. Scan NOW (s10-II
        // evidence) so the actual layer complaint is on record.
        uint64_t xErr = 0, xCorr = 0, xWarn = 0, xInfo = 0;
        UINT64 xStored = 0, xRetr = 0, xFail = 0;
        std::unordered_map<std::string, uint64_t> xHist;
        std::vector<std::string> xErrSamples, xCorrSamples;
        (void)scanInfoQueue("sink-exception", xErr, xCorr, xWarn, xInfo,
            xStored, xRetr, xFail, xHist, xErrSamples, xCorrSamples);
        for (const auto& m : xErrSamples) {
            veyra::log::error("teardown", std::format("debug-layer ERROR: {}", m));
        }
    }
    stageDone("sink-shutdown");
    stage("ring-shutdown");
    ring.shutdown();
    stageDone("ring-shutdown");

    // s10-II sequence: teardown scan -> ReportLiveDeviceObjects -> final
    // scan -> InfoQueue.Reset -> context.shutdown.
    uint64_t tdErr = 0, tdCorr = 0, tdWarn = 0, tdInfo = 0;
    UINT64 tdStored = 0, tdRetrieved = 0, tdFailures = 0;
    std::unordered_map<std::string, uint64_t> tdHist;
    std::vector<std::string> tdErrSamples, tdCorrSamples;
    const bool tdComplete = d3dDiag
        ? scanInfoQueue("teardown", tdErr, tdCorr, tdWarn, tdInfo,
              tdStored, tdRetrieved, tdFailures, tdHist, tdErrSamples, tdCorrSamples)
        : true;
    if (d3dDiag && (!tdComplete || tdErr > 0 || tdCorr > 0)) {
        // s10-II item 6: teardown diagnostics affect the final verdict.
        overall = false;
    }
    g_diagTeardownStored = tdStored;
    g_diagTeardownRetrieved = tdRetrieved;
    g_diagTeardownErrors = tdErr;
    g_diagTeardownCorruption = tdCorr;

    stage("report-live-objects");
    if (d3dDiag && context.device() != nullptr) {
        ComPtr<ID3D12DebugDevice> dbgDev;
        if (SUCCEEDED(context.device()->QueryInterface(IID_PPV_ARGS(&dbgDev)))) {
            dbgDev->ReportLiveDeviceObjects(D3D12_RLDO_SUMMARY | D3D12_RLDO_IGNORE_INTERNAL);
            veyra::log::info("teardown", "ReportLiveDeviceObjects emitted");
        }
    }
    stageDone("report-live-objects");

    uint64_t finErr = 0, finCorr = 0, finWarn = 0, finInfo = 0;
    UINT64 finStored = 0, finRetrieved = 0, finFailures = 0;
    std::unordered_map<std::string, uint64_t> finHist;
    std::vector<std::string> finErrSamples, finCorrSamples;
    if (d3dDiag) {
        (void)scanInfoQueue("final", finErr, finCorr, finWarn, finInfo,
            finStored, finRetrieved, finFailures, finHist, finErrSamples, finCorrSamples);
    }

    const bool diagQueueWasActive = d3dDiag && d3dDiagQueue.Get() != nullptr;
    stage("infoqueue-release");
    d3dDiagQueue.Reset();
    g_d3dDiagQueue = nullptr;
    stageDone("infoqueue-release");
    stage("context-shutdown");
    context.shutdown();
    stageDone("context-shutdown");
    stage("demuxer-close");
    demuxer.close();
    stageDone("demuxer-close");
    stage("decoder-close");
    decoder.close();
    stageDone("decoder-close");

    // s10-III: final evidence ONLY after context.shutdown completed,
    // immediately before the natural return.
    PROCESS_MEMORY_COUNTERS memEnd{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memEnd, sizeof(memEnd));
    workingSetGrowthMB = (memEnd.WorkingSetSize > memStart.WorkingSetSize)
        ? (memEnd.WorkingSetSize - memStart.WorkingSetSize) / (1024.0 * 1024.0) : 0.0;

    // s10-III: single final JSON with real completion markers, three
    // diagnostic phases, histogram and samples. No duplicate keys.
    {
        std::string j;
        j += "{\n";
        j += std::format("  \"probe\": \"veyra_player_probe\",\n");
        j += std::format("  \"runId\": \"{}\",\n", jsonEscape(runId));
        j += std::format("  \"mode\": \"{}\",\n", endurance ? "endurance" : "scenario");
        j += std::format("  \"processCompleted\": true,\n");
        j += std::format("  \"teardownCompleted\": true,\n");
        j += std::format("  \"verdict\": \"{}\",\n", overall ? "PASS" : "FAIL");
        j += std::format("  \"playPauseWorks\": {},\n", playPauseWorks ? "true" : "false");
        j += std::format("  \"seekWorks\": {},\n", seekWorks ? "true" : "false");
        j += std::format("  \"seekCount\": {},\n", seekCount);
        j += std::format("  \"resizeWorks\": {},\n", resizeWorks ? "true" : "false");
        j += std::format("  \"driftMinMs\": {:.3f},\n", driftMinMs);
        j += std::format("  \"driftP50Ms\": {:.3f},\n", driftP50Ms);
        j += std::format("  \"driftP95Ms\": {:.3f},\n", driftP95Ms);
        j += std::format("  \"driftP99Ms\": {:.3f},\n", driftP99Ms);
        j += std::format("  \"driftMaxMs\": {:.3f},\n", driftMaxMs);
        j += std::format("  \"latenessSampleCount\": {},\n", latenessSampleCount);
        j += std::format("  \"droppedSourceFrames\": {},\n", g_droppedSourceFrames);
        j += std::format("  \"droppedGeneratedFrames\": {},\n", g_droppedGeneratedFrames);
        j += std::format("  \"droppedLatePresents\": {},\n", droppedLatePresents);
        j += std::format("  \"fgToggleWorks\": {},\n", fgToggleWorks ? "true" : "false");
        j += std::format("  \"nrToggleWorks\": {},\n", nrToggleWorks ? "true" : "false");
        j += std::format("  \"srToggleWorks\": {},\n", srToggleWorks ? "true" : "false");
        j += std::format("  \"normalPathReadbackCount\": {},\n", metrics.normalPathReadbackCount);
        j += std::format("  \"presentCount\": {},\n", metrics.presentCount);
        j += std::format("  \"realFramesPresented\": {},\n", metrics.realFramesPresented);
        j += std::format("  \"fgGeneratedFrames\": {},\n", g_graphFgGen);
        j += std::format("  \"nrEvaluateCount\": {},\n", g_graphNrEval);
        j += std::format("  \"srEvaluateCount\": {},\n", g_graphSrEval);
        j += std::format("  \"nvofExecuteCount\": {},\n", g_graphNvofExec);
        j += std::format("  \"nvofFrameFailures\": {},\n", nvofFrameFailures);
        j += std::format("  \"nvofLastSignal\": {},\n", lastNvofSignal);
        j += std::format("  \"mvecSource\": \"{}\",\n", jsonEscape(mvecSource));
        j += std::format("  \"maxInFlightFrames\": {},\n", metrics.maxInFlight);
        j += std::format("  \"audioUnderruns\": {},\n", audioUnderruns);
        j += std::format("  \"audioOverruns\": {},\n", audioOverruns);
        j += std::format("  \"audioBufferedMsEnd\": {:.1f},\n", g_audioEndBufferedMs);
        j += std::format("  \"audioHeadPtsMsEnd\": {:.1f},\n", g_audioEndHeadPtsMs);
        j += std::format("  \"audioClockPtsMsEnd\": {:.1f},\n", g_audioEndClockPtsMs);
        j += std::format("  \"audioSeekCount\": {},\n", g_audioSeekCount);
        j += std::format("  \"audioLastPrefillMs\": {:.1f},\n", g_audioLastPrefillMs);
        j += std::format("  \"audioFirstPtsAfterSeekMs\": {:.1f},\n", g_audioFirstPtsAfterSeekMs);
        j += std::format("  \"subtitlePolicy\": \"none-in-source\",\n");
        j += std::format("  \"workingSetGrowthMB\": {:.2f},\n", workingSetGrowthMB);
        j += std::format("  \"d3dDiagRequested\": {},\n", d3dDiag ? "true" : "false");
        j += std::format("  \"d3dDiagActive\": {},\n", diagQueueWasActive ? "true" : "false");
        j += std::format("  \"diagRetrievalComplete\": {},\n", g_diagRetrievalComplete && tdComplete ? "true" : "false");
        j += std::format("  \"diagRuntime\": {{\"stored\": {}, \"retrieved\": {}, \"failures\": {}, \"errors\": {}, \"corruption\": {}, \"warnings\": {}, \"info\": {}}},\n",
            g_diagStoredRuntime, g_diagRetrievedRuntime, g_diagRetrievalFailures,
            g_d3dDiagErrors, g_d3dDiagCorruption, g_d3dDiagWarnings, g_d3dDiagInfo);
        j += std::format("  \"diagTeardown\": {{\"stored\": {}, \"retrieved\": {}, \"failures\": {}, \"errors\": {}, \"corruption\": {}, \"warnings\": {}, \"info\": {}}},\n",
            tdStored, tdRetrieved, tdFailures, tdErr, tdCorr, tdWarn, tdInfo);
        j += std::format("  \"diagFinal\": {{\"stored\": {}, \"retrieved\": {}, \"failures\": {}, \"errors\": {}, \"corruption\": {}}},\n",
            finStored, finRetrieved, finFailures, finErr, finCorr);
        j += "  \"diagHistogram\": {";
        {
            bool firstH = true;
            for (const auto& [k, v] : diagHistogram) {
                if (!firstH) j += ",";
                firstH = false;
                j += std::format("\"{}\": {}", k, v);
            }
        }
        j += "},\n";
        j += "  \"diagErrorSamples\": [";
        {
            bool firstS = true;
            for (const auto& m : diagErrorSamples) {
                if (!firstS) j += ",";
                firstS = false;
                j += std::format("\"{}\"", jsonEscape(m));
            }
        }
        j += "],\n";
        j += "  \"diagTeardownErrorSamples\": [";
        {
            bool firstS = true;
            for (const auto& m : tdErrSamples) {
                if (!firstS) j += ",";
                firstS = false;
                j += std::format("\"{}\"", jsonEscape(m));
            }
        }
        j += "],\n";
        j += "  \"run4k30\": {\n";
        j += std::format("    \"durationSeconds\": {:.1f},\n", end4k30Duration);
        j += std::format("    \"fgGeneratedFrames\": {},\n", end4k30Fg);
        j += std::format("    \"internalTimelineHz\": {:.2f}\n", end4k30Hz);
        j += "  },\n";
        j += "  \"run4k60\": {\n";
        j += std::format("    \"durationSeconds\": {:.1f},\n", end4k60Duration);
        j += std::format("    \"fgGeneratedFrames\": {},\n", end4k60Fg);
        j += std::format("    \"internalTimelineHz\": {:.2f}\n", end4k60Hz);
        j += "  }\n";
        j += "}\n";
        if (!jsonFile.empty()) (void)utilns::writeTextFileUtf8(jsonFile, j);
    }
    veyra::log::info("player", std::format("player-probe: {} presents={} fg={} nr={} sr={} driftP95={:.1f}ms",
        overall ? "PASS" : "FAIL", metrics.presentCount, g_graphFgGen,
        g_graphNrEval, g_graphSrEval, driftP95Ms));
    veyra::Logger::instance().flush();

    veyra::log::info("player", "teardown-complete; process will return naturally");
    veyra::Logger::instance().flush();
    return overall ? 0 : 12;
}
