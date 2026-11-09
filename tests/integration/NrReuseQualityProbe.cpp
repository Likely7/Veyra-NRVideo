// NR inference reuse: picture difference against full inference on a real video
// (docs/NR_INFERENCE_REUSE_2026-11-07.md).
//
// Two graphs built through the product path (chain -> settings -> describeStages)
// process the same decoded frames in lockstep. A infers every frame. B is a
// preview graph that reuses one frame in two (Half). After every frame both NR
// chain outputs are read back and compared in 8-bit sRGB units. Reused frames and
// inferred frames are reported separately: B's inferred frames also differ a
// little, because B's NR history skips the reused frames.
//
// Offline only: the readbacks and CPU waits exist in this executable, never in
// playback. usage: <video> <out-dir> <frames> <dump-frame>
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/Log.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/RuntimePaths.h"
#include "veyra/source/MediaFileSource.h"
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using namespace veyra;
// Full-frame RGBA16F readback; the texture is returned to NON_PIXEL_SHADER_RESOURCE,
// the state the graph's own tracker expects it in.
bool readback(gfx::D3D12DeviceContext& ctx, gfx::CommandSlotRing& ring, ID3D12Resource* texture,
              std::vector<float>& rgba, unsigned& width, unsigned& height)
{
    if (!texture) return false;
    const auto desc = texture->GetDesc();
    if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
    width = unsigned(desc.Width); height = desc.Height;
    const unsigned rowBytes = width * 8, pitch = (rowBytes + 255) & ~255u;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = UINT64(pitch) * height; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> staging;
    if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)))) return false;
    unsigned slot = 0; Status status = Status::Ok;
    auto* list = ring.acquireNext(slot, status);
    if (!list) return false;
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = staging.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;
    void* data = nullptr; D3D12_RANGE range{0, size_t(pitch) * height};
    if (FAILED(staging->Map(0, &range, &data))) return false;
    rgba.resize(size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(static_cast<const uint8_t*>(data) + size_t(y) * pitch);
        for (unsigned i = 0; i < width * 4; ++i) rgba[size_t(y) * width * 4 + i] = DirectX::PackedVector::XMConvertHalfToFloat(row[i]);
    }
    D3D12_RANGE none{0, 0}; staging->Unmap(0, &none);
    return true;
}
float srgb8(float linear)
{
    const float v = std::clamp(linear, 0.f, 1.f);
    return 255.f * (v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f);
}
// Binary PPM (P6), viewable and trivially converted; sRGB-encoded.
bool writePpm(const std::filesystem::path& path, const std::vector<float>& rgba, unsigned w, unsigned h, float gain = 1, const std::vector<float>* minus = nullptr)
{
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << ' ' << h << "\n255\n";
    for (size_t i = 0; i < size_t(w) * h; ++i)
        for (unsigned c = 0; c < 3; ++c) {
            float v = srgb8(rgba[i * 4 + c]);
            if (minus) v = std::min(255.f, std::abs(v - srgb8((*minus)[i * 4 + c])) * gain);
            out.put(char(uint8_t(std::lround(std::clamp(v, 0.f, 255.f)))));
        }
    return bool(out);
}
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) return 2;
    const std::filesystem::path directory = argv[2];
    const unsigned frames = unsigned(_wtoi(argv[3])), dumpFrame = unsigned(_wtoi(argv[4]));
    if (frames < 4 || frames > 600) return 2;
    std::filesystem::create_directories(directory);
    Logger::instance().openFile((directory / L"engine.log").wstring());
    Logger::instance().setConsoleEnabled(false);
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring; Status status;
    gfx::DeviceContextDesc device; device.enableDebugLayer = true;
    if (!ctx.initialize(device, status) || !ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 6, status)) return 2;
    source::MediaFileSource source; source::SourceOpenDesc open;
    open.path = argv[1]; open.preferHardwareDecode = false;
    if (!source.open(open)) return 2;
    const auto info = source.info();

    // Product path: one enabled NR node, default model, as the list card adds it.
    engine::EffectChain chain; chain.nodeCount = 1;
    chain.nodes[0].type = engine::EffectType::NrEnhance; chain.nodes[0].enabled = true;
    engine::EnhancementSettings settings; engine::fromChain(chain, settings);
    if (!engine::validateChain(chain).accepted || !settings.validate().empty()) return 2;
    engine::StageRequest request; request.nr = true; request.width = info.width; request.height = info.height;
    pipeline::EnhanceGraphDesc full;
    engine::describeStages(request, settings, full);
    full.enableNvofStandalone = full.enableNr;
    full.runtimeAbsPath = runtime::localRuntimeDirectory().wstring();
    auto reused = full;
    reused.nrReusePreview = true; reused.nrReuse = engine::NrReuseMode::Half;
    std::cout << "source=" << info.width << 'x' << info.height << " fps=" << info.averageFps << std::endl;

    // One NR graph per process at a time (the NR runtime adapter owns one hook
    // slot), so the passes run one after the other over the same decoded frames.
    struct Pass { std::vector<std::vector<uint8_t>> frames; std::vector<bool> reused; std::vector<float> dump; uint64_t evaluates = 0;
                  double nrChange = 0; unsigned nrChangeFrames = 0; };
    unsigned w = 0, h = 0;
    auto run = [&](const pipeline::EnhanceGraphDesc& desc, Pass& pass) -> int {
        if (!source.seek({0, 1000})) return 2;
        pipeline::EnhanceGraph graph(ctx, ring);
        if (!graph.initialize(desc) || !graph.createViews()) return 2;
        if (desc.nrReusePreview) {
            std::cout << "nr=" << graph.nrWidth() << 'x' << graph.nrHeight() << " reuseActive=" << graph.nrReuseActive()
                      << " reason=\"" << graph.nrReuseUnavailable() << "\"" << std::endl;
            if (!graph.nrReuseActive()) return 1;
        }
        std::vector<float> out;
        for (unsigned i = 0; i < frames; ++i) {
            pipeline::FramePacket packet; const AVFrame* frame = nullptr;
            if (source.read(packet, &frame) != source::SourceReadStatus::Frame) break;
            pipeline::EnhanceGraph::FrameOutputs result;
            const uint64_t before = graph.metrics().nrReuseFrames;
            if (!graph.process(frame, packet.pts.toDouble() * 1000, i == 0, result, packet.sequence, &packet.colorInfo) ||
                !ring.waitIdle() || !readback(ctx, ring, graph.diagnosticNrLayerOutput(0), out, w, h)) return 3;
            pass.reused.push_back(graph.metrics().nrReuseFrames > before);
            std::vector<uint8_t> encoded(size_t(w) * h * 3);
            for (size_t p = 0; p < size_t(w) * h; ++p)
                for (unsigned c = 0; c < 3; ++c) encoded[p * 3 + c] = uint8_t(std::lround(srgb8(out[p * 4 + c])));
            // How much NR itself changes the picture, as a scale for the reuse difference.
            std::vector<float> input; unsigned iw = 0, ih = 0;
            if (!desc.nrReusePreview && i >= 2 && readback(ctx, ring, graph.diagnosticNrLayerInput(0), input, iw, ih) && iw == w && ih == h) {
                double change = 0;
                for (size_t p = 0; p < size_t(w) * h; ++p)
                    for (unsigned c = 0; c < 3; ++c) change += std::abs(double(encoded[p * 3 + c]) - std::lround(srgb8(input[p * 4 + c])));
                pass.nrChange += change / (double(w) * h * 3); ++pass.nrChangeFrames;
            }
            pass.frames.push_back(std::move(encoded));
            if (i == dumpFrame) pass.dump = out;
        }
        pass.evaluates = graph.metrics().nrEvaluateCount;
        ring.drainQueue(); graph.shutdown();
        return 0;
    };
    Pass a, b;
    if (const int rc = run(full, a)) return rc;
    if (const int rc = run(reused, b)) return rc;
    if (a.frames.size() != b.frames.size() || a.frames.empty()) return 3;

    std::ofstream csv(directory / L"frames.csv");
    csv << "frame,reused,maeSrgb8,maxSrgb8,psnrDb,over8Percent\n";
    double reusedMae = 0, reusedPsnr = 0, inferredMae = 0, inferredPsnr = 0, reusedMax = 0, reusedOver8 = 0;
    unsigned reusedCount = 0, inferredCount = 0;
    for (size_t i = 0; i < a.frames.size(); ++i) {
        double sum = 0, sq = 0, worst = 0; size_t over8 = 0;
        for (size_t k = 0; k < a.frames[i].size(); ++k) {
            const double d = std::abs(int(a.frames[i][k]) - int(b.frames[i][k]));
            sum += d; sq += d * d; worst = std::max(worst, d); over8 += d > 8;
        }
        const double n = double(a.frames[i].size()), mae = sum / n, mse = sq / n;
        const double psnr = mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 99.0, over = 100.0 * double(over8) / n;
        csv << i << ',' << b.reused[i] << ',' << mae << ',' << worst << ',' << psnr << ',' << over << '\n';
        if (i < 2) continue;
        if (b.reused[i]) { reusedMae += mae; reusedPsnr += psnr; reusedMax = std::max(reusedMax, worst); reusedOver8 += over; ++reusedCount; }
        else { inferredMae += mae; inferredPsnr += psnr; ++inferredCount; }
    }
    if (!a.dump.empty() && !b.dump.empty()) {
        writePpm(directory / L"full-inference.ppm", a.dump, w, h);
        writePpm(directory / L"reuse.ppm", b.dump, w, h);
        writePpm(directory / L"difference-x8.ppm", b.dump, w, h, 8, &a.dump);
        std::cout << "dumped frame=" << dumpFrame << " reused=" << (dumpFrame < b.reused.size() && b.reused[dumpFrame]) << std::endl;
    }
    source.close();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> debug; unsigned errors = 0;
    if (SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&debug))))
        for (UINT64 i = 0; i < debug->GetNumStoredMessages(); ++i) {
            SIZE_T size = 0; debug->GetMessage(i, nullptr, &size);
            std::vector<unsigned char> bytes(size); auto* m = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (SUCCEEDED(debug->GetMessage(i, m, &size)) && m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                ++errors; std::cerr << m->pDescription << std::endl;
            }
        }
    std::cout << "frames reused=" << reusedCount << " inferred=" << inferredCount
              << " reusedMaeSrgb8=" << (reusedCount ? reusedMae / reusedCount : 0)
              << " reusedPsnrDb=" << (reusedCount ? reusedPsnr / reusedCount : 0)
              << " reusedMaxSrgb8=" << reusedMax
              << " inferredMaeSrgb8=" << (inferredCount ? inferredMae / inferredCount : 0)
              << " inferredPsnrDb=" << (inferredCount ? inferredPsnr / inferredCount : 0)
              << " reusedOver8Percent=" << (reusedCount ? reusedOver8 / reusedCount : 0)
              << " nrChangeMaeSrgb8=" << (a.nrChangeFrames ? a.nrChange / a.nrChangeFrames : 0)
              << " evaluatesA=" << a.evaluates << " evaluatesB=" << b.evaluates
              << " debugErrors=" << errors << std::endl;
    return errors == 0 && reusedCount > 0 ? 0 : 1;
}
