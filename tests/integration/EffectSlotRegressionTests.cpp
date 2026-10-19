// Byte-exact regression for the effect insertion slots.
//
// The graph runs colour grades in several places (fused into input conversion,
// extra list instances, and node-mode slots before SR, before NR, between NR
// layers and after everything). This executable renders one deterministic
// frame per configuration through the real EnhanceGraph and prints SHA-256 of
// the 8-bit output and of the FP16 intermediates. A build from before a change
// and a build after it must print identical lines; a runner script compares
// them against a recorded baseline. One case per process keeps the NGX core
// lifetime out of the comparison.
//
//   veyra_effect_slot_regression_tests.exe --list
//   veyra_effect_slot_regression_tests.exe --case <name>
#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/FileIdentity.h"
#include "veyra/RuntimePaths.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
using namespace veyra;
using engine::ChainNode;
using engine::EffectChain;
using engine::EffectType;

constexpr unsigned kWidth = 640, kHeight = 360;

// A deterministic picture with smooth ramps, hard edges, thin diagonal lines
// and mild texture, so every colour variant and the NR model have something to
// act on.
bool paint(AVFrame* f) {
    f->format = AV_PIX_FMT_BGR0;
    f->width = kWidth;
    f->height = kHeight;
    f->color_range = AVCOL_RANGE_UNSPECIFIED;
    f->color_trc = AVCOL_TRC_IEC61966_2_1;
    f->colorspace = AVCOL_SPC_RGB;
    if (av_frame_get_buffer(f, 32) < 0) return false;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const float u = float(x) / float(kWidth - 1), v = float(y) / float(kHeight - 1);
            float r = 50 + 160 * u, g = 40 + 130 * v, b = 130 + 90 * (1 - u) * (0.4f + v);
            const int dx = int(x) - 170, dy = int(y) - 190;
            if (dx * dx + dy * dy < 85 * 85) { r = 225; g = 78; b = 52; }
            if (x > 330 && x < 540 && y > 70 && y < 270) { r = 44; g = 164; b = 214; }
            if (((x * 3 + y * 2) % 41) < 2) { r = 238; g = 236; b = 228; }
            if (x > 40 && x < 120 && y > 40 && y < 120) { const bool cell = ((x / 8) + (y / 8)) & 1; r = g = b = cell ? 232 : 24; }
            uint32_t h = x * 2654435761u ^ (y * 40503u + 0x9E3779B9u);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            const int noise = int(h % 7u) - 3;
            auto q = [&](float c) { return uint8_t(std::clamp(int(c + 0.5f) + noise, 0, 255)); };
            auto* p = f->data[0] + size_t(y) * f->linesize[0] + size_t(x) * 4;
            p[0] = q(b); p[1] = q(g); p[2] = q(r); p[3] = 255;
        }
    return true;
}

engine::ColorSettings gradeVariant(int variant) {
    engine::ColorSettings c;
    c.enabled = true;
    switch (variant) {
    case 0: c.exposure = 0.7f; break;
    case 1: c.contrast = 45; c.saturation = -20; break;
    case 2: c.temperature = 35; c.tint = -10; c.vibrance = 30; break;
    case 3: c.highlights = -40; c.shadows = 30; c.blacks = 10; break;
    case 4: c.saturation = 55; c.whites = -12; break;
    default: c.exposure = -0.4f; c.contrast = 18; c.temperature = -22; break;
    }
    return c;
}
ChainNode colorNode(int variant) {
    ChainNode n; n.type = EffectType::Color; n.enabled = true; n.color = gradeVariant(variant); return n;
}
ChainNode srNode() { ChainNode n; n.type = EffectType::SuperResolution; n.enabled = true; return n; }
ChainNode nrNode(bool nrFirst) {
    ChainNode n; n.type = EffectType::NrEnhance; n.enabled = true;
    n.nr.sizePolicy = pipeline::NrSizePolicy::Realtime; n.nr.temporal = false; n.nr.lowLatencyPairing = nrFirst;
    return n;
}

struct Case {
    std::string name;
    engine::EnhancementSettings settings;
    std::optional<engine::ChainRuntimeOrder> order;
    bool nr = false, sr = false;
    unsigned expectedNr = 0;
};

Case nodeCase(std::string name, std::initializer_list<ChainNode> nodes) {
    Case c; c.name = std::move(name);
    auto chain = std::make_unique<EffectChain>();
    chain->mode = engine::ChainMode::Node;
    for (const auto& n : nodes) {
        chain->nodes[chain->nodeCount++] = n;
        c.nr = c.nr || (n.type == EffectType::NrEnhance && n.enabled);
        c.sr = c.sr || (n.type == EffectType::SuperResolution && n.enabled);
        if (n.type == EffectType::NrEnhance && n.enabled) ++c.expectedNr;
    }
    engine::fromChain(*chain, c.settings);
    c.settings.multiplier = 1;
    c.settings.nrMotion = engine::MotionSource::Zero;
    c.settings.srTarget = pipeline::SrTarget::Qhd;
    c.order = engine::runtimeOrder(*chain);
    return c;
}
Case listCase(std::string name, bool nr, bool sr, bool nrFirst, unsigned colors, unsigned nrLayers = 1, bool protection = false) {
    Case c; c.name = std::move(name); c.nr = nr; c.sr = sr; c.expectedNr = nr ? nrLayers : 0;
    auto& s = c.settings;
    s.nr = nr; s.sr = sr; s.lowLatency = nrFirst; s.multiplier = 1;
    s.nrMotion = engine::MotionSource::Zero; s.srTarget = pipeline::SrTarget::Qhd;
    if (colors > 0) s.color = gradeVariant(0);
    s.additionalColorCount = colors > 1 ? colors - 1 : 0;
    for (unsigned i = 0; i + 1 < colors && i < s.additionalColors.size(); ++i) s.additionalColors[i] = gradeVariant(int(i) + 1);
    if (nr && nrLayers > 1) {
        s.nrLayerCount = nrLayers;
        for (unsigned i = 0; i < nrLayers; ++i) {
            s.nrLayers[i].enabled = true; s.nrLayers[i].lowLatencyPairing = nrFirst;
            s.nrLayers[i].sizePolicy = pipeline::NrSizePolicy::Realtime;
        }
    }
    if (protection) { s.protection.enabled = true; s.protection.featherPixels = 4; s.protection.regions[0] = {0.1f, 0.1f, 0.45f, 0.5f, false}; }
    return c;
}

std::vector<Case> allCases() {
    std::vector<Case> cases;
    cases.push_back(listCase("list-default-colors", true, true, false, 3));
    cases.push_back(listCase("list-nrfirst-colors", true, true, true, 2));
    cases.push_back(listCase("list-six-colors-sr", false, true, false, 6));
    cases.push_back(listCase("list-nr2-protection-colors", true, true, false, 2, 2, true));
    cases.push_back(nodeCase("node-prenr", {srNode(), colorNode(1), nrNode(false)}));
    cases.push_back(nodeCase("node-presr", {nrNode(true), colorNode(2), srNode()}));
    cases.push_back(nodeCase("node-internr", {srNode(), nrNode(false), colorNode(3), nrNode(false)}));
    cases.push_back(nodeCase("node-tail", {srNode(), nrNode(false), colorNode(4)}));
    cases.push_back(nodeCase("node-tail-sr-only", {srNode(), colorNode(1), colorNode(2)}));
    cases.push_back(nodeCase("node-mixed-sr-first", {colorNode(0), srNode(), colorNode(1), nrNode(false), colorNode(2), nrNode(false), colorNode(3)}));
    cases.push_back(nodeCase("node-mixed-nr-first", {colorNode(0), nrNode(true), colorNode(1), nrNode(true), colorNode(2), srNode(), colorNode(3)}));
    cases.push_back(nodeCase("node-split", {nrNode(true), colorNode(2), srNode(), colorNode(3), nrNode(true), colorNode(4)}));
    return cases;
}

// Diagnostic readback of an FP16 RGBA texture (never used by the product).
bool readRaw(engine::PreviewGpuSession& session, ID3D12Resource* texture, std::vector<uint8_t>& pixels,
             D3D12_RESOURCE_STATES initial) {
    if (!texture) return false;
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes = 0;
    session.context.device()->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = bytes;
    buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(session.context.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
    Status status; uint32_t slot;
    auto* list = session.ring.acquireNext(slot, status);
    if (!list) return false;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, initial, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    if (!session.ring.submitAndSignal(slot) || !session.ring.waitIdle()) return false;
    void* data = nullptr; D3D12_RANGE range{0, SIZE_T(bytes)};
    if (FAILED(readback->Map(0, &range, &data))) return false;
    pixels.resize(size_t(desc.Width) * desc.Height * 8);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + size_t(y) * desc.Width * 8,
                    static_cast<uint8_t*>(data) + fp.Offset + size_t(y) * fp.Footprint.RowPitch, size_t(desc.Width) * 8);
    D3D12_RANGE empty{}; readback->Unmap(0, &empty);
    return true;
}

int runCase(const Case& test) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)com;
    engine::EnhancementSettings settings = test.settings;
    if (const auto error = settings.validate(); !error.empty()) { std::cout << "CASE_FAIL name=" << test.name << " invalid-settings=" << error << std::endl; return 2; }
    engine::StageRequest request; request.width = kWidth; request.height = kHeight; request.nr = test.nr; request.sr = test.sr;
    if (test.order) request.nodeOrder = &*test.order;
    pipeline::EnhanceGraphDesc desc;
    engine::describeStages(request, settings, desc);
    if (!desc.fixedExecutionPlanError.empty()) { std::cout << "CASE_FAIL name=" << test.name << " plan=" << desc.fixedExecutionPlanError << std::endl; return 2; }
    desc.rgbInput = true; desc.runtimeAbsPath = runtime::localRuntimeDirectory().wstring();
    engine::PreviewGpuSession session;
    if (!session.initialize()) { std::cout << "CASE_FAIL name=" << test.name << " device" << std::endl; return 2; }
    if (!session.graph->initialize(desc) || !session.graph->createViews()) { std::cout << "CASE_FAIL name=" << test.name << " graph-init" << std::endl; return 2; }
    AVFrame* frame = av_frame_alloc();
    if (!frame || !paint(frame)) return 2;
    pipeline::EnhanceGraph::FrameOutputs out;
    bool ok = session.graph->process(frame, 0.0, true, out, 1) && session.ring.waitIdle();
    sink::RgbaImage image;
    std::vector<uint8_t> raw, sourceRef, baseRef;
    ok = ok && sink::readRgba8(session.context, session.ring, session.graph->videoFrameResource(out.videoSlot), image);
    ok = ok && readRaw(session, session.graph->diagnosticLinearInput(), raw, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ok = ok && readRaw(session, session.graph->sourceReference(out.videoSlot), sourceRef, D3D12_RESOURCE_STATE_COMMON);
    ok = ok && readRaw(session, session.graph->baseReference(out.videoSlot), baseRef, D3D12_RESOURCE_STATE_COMMON);
    const auto nrEvaluations = session.graph->metrics().nrEvaluateCount;
    const auto srEvaluations = session.graph->metrics().srEvaluateCount;
    ok = ok && nrEvaluations == test.expectedNr && (!test.sr || session.graph->srEnabled());
    if (ok)
        std::cout << "CASE_RESULT name=" << test.name << " out=" << sha256Hex(image.pixels.data(), image.pixels.size())
                  << " linear=" << sha256Hex(raw.data(), raw.size())
                  << " sourceRef=" << sha256Hex(sourceRef.data(), sourceRef.size())
                  << " baseRef=" << sha256Hex(baseRef.data(), baseRef.size())
                  << " size=" << image.width << 'x' << image.height << " nrEval=" << nrEvaluations << " srEval=" << srEvaluations
                  << " nrBeforeSr=" << desc.nrBeforeSr << " node=" << desc.runtimeNodeOrder << std::endl;
    else
        std::cout << "CASE_FAIL name=" << test.name << " process-or-readback nrEval=" << nrEvaluations << std::endl;
    out = {};
    session.ring.drainQueue();
    session.graph->shutdown();
    av_frame_free(&frame);
    session.shutdown();
    return ok ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const auto cases = allCases();
    if (argc == 2 && std::wstring_view(argv[1]) == L"--list") {
        for (const auto& c : cases) std::cout << c.name << '\n';
        return 0;
    }
    if (argc == 3 && std::wstring_view(argv[1]) == L"--case") {
        const std::wstring wanted = argv[2];
        for (const auto& c : cases) {
            if (std::wstring(c.name.begin(), c.name.end()) == wanted) return runCase(c);
        }
        std::cout << "unknown case" << std::endl;
        return 2;
    }
    return 2;
}
