// Multi-instance NR probe: can one snippet session hold several Feature-18
// handles at once, evaluate each, and release them cleanly?
//
// This is the decisive question for NR stacking. Magpie (GPLv3, commit
// 3841698) stacks DLSSNR passes, each with its own feature handle on one shared
// snippet session and one IAT shim. This probe verifies the same contract on
// this machine before EnhanceGraph is restructured: if the runtime refuses a
// second handle, stacking is off the table and the UI must not offer it.
//
// Usage: veyra_nr_probe --runtime-dir <runtime_local\nvidia> [--count N] [--width W] [--height H]
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/ngx/DlssNrParameters.h"
#include "veyra/ngx/NgxParameters.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/ngx/DlssNrRuntimeAdapter.h"
#include "veyra/ngx/NgxCoreHost.h"

#include <fstream>
#include <iterator>

using namespace veyra;

namespace {
struct Options {
    std::wstring runtimeDir;
    uint32_t count = 4;
    uint32_t width = 1920;
    uint32_t height = 1080;
};
bool parse(int argc, wchar_t** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : std::wstring(); };
        if (arg == L"--runtime-dir") options.runtimeDir = next();
        else if (arg == L"--count") options.count = uint32_t(wcstoul(next().c_str(), nullptr, 10));
        else if (arg == L"--width") options.width = uint32_t(wcstoul(next().c_str(), nullptr, 10));
        else if (arg == L"--height") options.height = uint32_t(wcstoul(next().c_str(), nullptr, 10));
        else { std::fwprintf(stderr, L"unknown arg %s\n", arg.c_str()); return false; }
    }
    if (options.runtimeDir.empty() || options.count == 0 || options.count > 8) return false;
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        std::fprintf(stderr, "usage: --runtime-dir <dir> [--count N] [--width W] [--height H]\n");
        return 2;
    }
    const auto runtimeDir = std::wstring(runtime::localRuntimeDirectory().wstring());
    const auto probeDir = options.runtimeDir.empty() ? runtimeDir : options.runtimeDir;

    gfx::D3D12DeviceContext context;
    gfx::DeviceContextDesc desc;
    desc.commandSlotCount = 4;
    Status status = Status::Ok;
    if (!context.initialize(desc, status)) { std::fprintf(stderr, "device init failed\n"); return 1; }
    gfx::CommandSlotRing ring;
    if (!ring.initialize(context.device(), context.directQueue(), context.fence(), context.fenceEvent(), 4, status)) return 1;

    // The local NGX project identity lives beside the runtime directory, in
    // runtime_local/config/ngx-local.json -- the same file the product reads.
    std::string projectId, engineVersion;
    {
        std::ifstream stream(probeDir + L"\\..\\config\\ngx-local.json", std::ios::binary);
        if (!stream) { std::fprintf(stderr, "no local NGX identity beside the runtime dir\n"); return 1; }
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto field = [&text](const char* name) -> std::string {
            const std::string key = std::string("\"") + name + "\"";
            const auto at = text.find(key);
            if (at == std::string::npos) return {};
            const auto first = text.find('"', at + key.size() + 1);
            if (first == std::string::npos) return {};
            const auto last = text.find('"', first + 1);
            return last == std::string::npos ? std::string{} : text.substr(first + 1, last - first - 1);
        };
        projectId = field("ngxProjectId");
        engineVersion = field("engineVersion");
    }
    if (projectId.empty() || engineVersion.empty()) { std::fprintf(stderr, "local NGX identity is incomplete\n"); return 1; }
    ngx::NgxCoreHost coreHost;
    if (!coreHost.initialize(context.device(), probeDir, projectId.c_str(), engineVersion.c_str(), status)) return 1;

    ngx::DlssNrRuntimeAdapter adapter;
    if (!adapter.load(probeDir, status) || !adapter.installCallerCompatibility(status)) {
        std::fprintf(stderr, "snippet load or caller-compat shim failed\n");
        return 1;
    }
    uint64_t result = 0;
    uint32_t seh = 0;
    if (!adapter.snippetInitExt(context.device(), probeDir, result, seh) ||
        result != uint64_t(NVSDK_NGX_Result_Success)) {
        std::fprintf(stderr, "snippet Init_Ext failed\n");
        adapter.unload();
        return 1;
    }

    // One parameter block per handle, matching the product's create contract.
    std::vector<NVSDK_NGX_Parameter*> parameters;
    std::vector<NVSDK_NGX_Handle*> handles;
    int created = 0, evaluated = 0, released = 0;
    for (uint32_t n = 0; n < options.count; ++n) {
        auto* params = coreHost.allocateParameters(status);
        if (params == nullptr) break;
        {
            ngx::ParameterBlock pb(params);
            namespace p = ngx::dlssnr;
            pb.setU32(p::kWidth, options.width); pb.setU32(p::kHeight, options.height);
            pb.setU32(p::kInputWidth, options.width); pb.setU32(p::kInputHeight, options.height);
            pb.setU32(p::kOutputWidth, options.width); pb.setU32(p::kOutputHeight, options.height);
            pb.setU32(p::kOutputDotWidth, options.width); pb.setU32(p::kOutputDotHeight, options.height);
            pb.setU32(p::kUpscaling, 0);
            pb.setF32(p::kScale, 1.0f); pb.setF32(p::kScalingRatio, 1.0f);
            pb.setVoid(p::kComputeScalingRatioCallback, reinterpret_cast<void*>(&ngx::DlssNrRuntimeAdapter::scalingRatioCallback));
            pb.setI32(p::kHintRenderPreset, 0);
            pb.setU32(p::kStdWidth, options.width); pb.setU32(p::kStdHeight, options.height);
            pb.setI32(p::kPerfQualityValue, 1);
            pb.setU32(p::kCreationNodeMask, 1); pb.setU32(p::kVisibilityNodeMask, 1);
        }
        NVSDK_NGX_Handle* handle = nullptr;
        auto* list = ring.acquire(0, status);
        if (list == nullptr) break;
        const bool ok = adapter.snippetCreateFeature(list, params, &handle, result, seh) &&
            result == uint64_t(NVSDK_NGX_Result_Success) && handle != nullptr;
        if (!ring.submitAndSignal(0) || !ring.waitIdle()) break;
        std::printf("create #%u: %s handle=%s\n", n + 1, ok ? "OK" : "FAILED", handle ? "non-null" : "null");
        if (!ok) break;
        parameters.push_back(params);
        handles.push_back(handle);
        ++created;
    }

    // Evaluate every handle on the shared ring with real bound resources.
    // CreateFeature alone does not prove stacking: the runtime has to accept
    // each handle's own colour/motion/depth/output set.
    if (uint32_t(created) == options.count) {
        using pipeline::ComPtr;
        std::vector<ComPtr<ID3D12Resource>> keepAlive;
        auto bindTextures = [&](size_t index) {
            namespace p = ngx::dlssnr;
            auto color = pipeline::makeTexture(context.device(), options.width, options.height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
            auto output = pipeline::makeTexture(context.device(), options.width, options.height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
            auto mvec = pipeline::makeTexture(context.device(), options.width, options.height, DXGI_FORMAT_R16G16_FLOAT, true);
            auto depth = pipeline::makeTexture(context.device(), options.width, options.height, DXGI_FORMAT_R32_FLOAT, true);
            if (!color || !output || !mvec || !depth) return false;
            ngx::ParameterBlock pb(parameters[index]);
            pb.setD3D12Resource(p::kColor, color.Get());
            pb.setD3D12Resource(p::kOutput, output.Get());
            pb.setD3D12Resource(p::kMVec, mvec.Get());
            pb.setD3D12Resource(p::kDepth, depth.Get());
            pb.setU32(p::kColorSubrectWidth, options.width); pb.setU32(p::kColorSubrectHeight, options.height);
            pb.setU32(p::kOutputSubrectWidth, options.width); pb.setU32(p::kOutputSubrectHeight, options.height);
            pb.setU32(p::kMVecSubrectWidth, options.width); pb.setU32(p::kMVecSubrectHeight, options.height);
            pb.setU32(p::kDepthSubrectWidth, options.width); pb.setU32(p::kDepthSubrectHeight, options.height);
            pb.setF32(p::kMVecScaleX, 1.0f); pb.setF32(p::kMVecScaleY, 1.0f);
            pb.setI32(p::kDepthInverted, 1);
            pb.setI32(p::kIndicatorInvertX, 0); pb.setI32(p::kIndicatorInvertY, 0);
            pb.setI32(p::kEnabled, 1);
            pb.setI32(p::kReset, index == 0 ? 1 : 0);
            pb.setI32(p::kStyle, 0);
            pb.setF32(p::kIntensity, 1.0f);
            pb.setF32(p::kLocalToneStrength, 1.0f);
            pb.setF32(p::kLocalStructureStrength, 1.0f);
            pb.setF32(p::kSkinStructureStrength, -1.0f);
            pb.setI32(p::kUseAutoMask, 0);
            pb.setI32(p::kUICorrection, 0);
            keepAlive.push_back(color); keepAlive.push_back(output);
            keepAlive.push_back(mvec); keepAlive.push_back(depth);
            return true;
        };
        for (size_t n = 0; n < handles.size(); ++n) {
            if (!bindTextures(n)) { std::printf("evaluate #%zu: bind failed\n", n + 1); break; }
            auto* list = ring.acquire(0, status);
            if (list == nullptr) break;
            uint64_t er = 0;
            uint32_t es = 0;
            const bool ok = adapter.snippetEvaluateFeature(list, handles[n], parameters[n], er, es) &&
                er == uint64_t(NVSDK_NGX_Result_Success);
            if (!ring.submitAndSignal(0) || !ring.waitIdle()) break;
            std::printf("evaluate #%zu: %s\n", n + 1, ok ? "OK" : "FAILED");
            if (!ok) break;
            ++evaluated;
        }
    }

    for (auto* handle : handles) {
        uint64_t rr = 0;
        uint32_t rs = 0;
        if (adapter.snippetReleaseFeature(handle, rr, rs) && rr == uint64_t(NVSDK_NGX_Result_Success)) ++released;
    }
    std::printf("RESULT requested=%u created=%d evaluated=%d released=%d\n",
                options.count, created, evaluated, released);

    adapter.restoreCallerCompatibility();
    adapter.unload();
    coreHost.shutdown();
    ring.shutdown();
    context.shutdown();

    const bool pass = created == int(options.count) && evaluated == created && released == created;
    return pass ? 0 : 1;
}
