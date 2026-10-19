// GPU checks for the spatial anti-aliasing effect (#53), one case per process
// (like veyra_effect_slot_regression_tests):
//
//   veyra_anti_alias_gpu_tests.exe --list
//   veyra_anti_alias_gpu_tests.exe --case <name>
//
// sdr-*: an aliased synthetic picture (rotated rectangle, circle, thin lines on
//   a flat background) runs through the real graph with one method / tier. Flat
//   areas must stay bit-identical to the effect switched off, edges must change,
//   and the edge error against a 16x16 supersampled linear-light reference must
//   drop. hdr-*: P010 PQ input, scRGB output: no NaN/Inf, flat highlights and
//   out-of-709 (negative) colours unchanged, edges changed. temporal-*: a reset
//   frame is deterministic, later frames use the history. node-*: real NGX SR /
//   NR graphs with anti-aliasing in node slots. tiled-image: the tiled
//   full-resolution image path (no compiled plan) matches one whole-image graph.
//   timing-*: GPU milliseconds of the anti-aliasing span per method.
// VEYRA_AA_GPU_DEBUG=1 enables the D3D12 debug layer and fails on its errors.
#include "veyra/engine/PreviewGpuSession.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/engine/TiledImageProcessor.h"
#include "veyra/sink/ImageExportSink.h"
#include "veyra/RuntimePaths.h"
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
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
using engine::AntiAliasMethod;
using engine::AntiAliasSettings;
using engine::AntiAliasTemporal;
using engine::EffectType;

constexpr double kPi = 3.14159265358979323846;
int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double linearToSrgb(double l) {
    l = std::clamp(l, 0.0, 1.0);
    return l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
}

// ---- synthetic scene -----------------------------------------------------------
// Laid out on 640x360 and scaled; thin lines stay one pixel wide.
struct Scene {
    unsigned width = 640, height = 360;
    double s = 1.0;
    // Worst case for the edge searches: thin rotated stripes everywhere (timing only).
    bool dense = false;
    explicit Scene(unsigned w = 640, unsigned h = 360, bool stripes = false) : width(w), height(h), s(double(w) / 640.0), dense(stripes) {}
    bool rectangle(double x, double y) const {
        const double a = 15 * kPi / 180, c = std::cos(a), sn = std::sin(a);
        const double dx = x - 200 * s, dy = y - 180 * s, u = dx * c + dy * sn, v = -dx * sn + dy * c;
        return std::abs(u) <= 95 * s && std::abs(v) <= 62 * s;
    }
    bool circle(double x, double y) const { const double dx = x - 470 * s, dy = y - 115 * s; return dx * dx + dy * dy <= 58 * 58 * s * s; }
    static double segment(double x, double y, double ax, double ay, double bx, double by) {
        const double vx = bx - ax, vy = by - ay, t = std::clamp(((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy), 0.0, 1.0);
        const double px = ax + t * vx - x, py = ay + t * vy - y;
        return std::sqrt(px * px + py * py);
    }
    bool line(double x, double y) const {
        return segment(x, y, 380 * s, 235 * s, 620 * s, 300 * s) <= 0.5 || segment(x, y, 400 * s, 345 * s, 455 * s, 205 * s) <= 0.5;
    }
    // 0 background, 1 rectangle / circle, 2 thin line
    int label(double x, double y) const {
        if (dense) return int(std::floor((x * 0.9659 + y * 0.2588) / 3.0)) & 1; // 3 px stripes at 15 degrees
        return rectangle(x, y) || circle(x, y) ? 1 : line(x, y) ? 2 : 0;
    }
    int pixel(unsigned x, unsigned y) const { return label(x + 0.5, y + 0.5); }
};
// Fraction of 16x16 subsamples per group (rectangle+circle, lines).
struct Coverage { std::vector<float> shape, line; };
Coverage supersample(const Scene& scene) {
    Coverage c; c.shape.assign(size_t(scene.width) * scene.height, 0); c.line = c.shape;
    constexpr int n = 16;
    for (unsigned y = 0; y < scene.height; ++y)
        for (unsigned x = 0; x < scene.width; ++x) {
            int shape = 0, line = 0;
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    const int l = scene.label(x + (i + 0.5) / n, y + (j + 0.5) / n);
                    shape += l == 1; line += l == 2;
                }
            c.shape[size_t(y) * scene.width + x] = float(shape) / (n * n);
            c.line[size_t(y) * scene.width + x] = float(line) / (n * n);
        }
    return c;
}
// A pixel is flat when its 9x9 neighbourhood in the aliased input has one label.
std::vector<uint8_t> flatMask(const Scene& scene) {
    std::vector<int> labels(size_t(scene.width) * scene.height);
    for (unsigned y = 0; y < scene.height; ++y)
        for (unsigned x = 0; x < scene.width; ++x) labels[size_t(y) * scene.width + x] = scene.pixel(x, y);
    std::vector<uint8_t> flat(labels.size(), 0);
    for (int y = 4; y + 4 < int(scene.height); ++y)
        for (int x = 4; x + 4 < int(scene.width); ++x) {
            const int l = labels[size_t(y) * scene.width + x];
            bool same = true;
            for (int dy = -4; dy <= 4 && same; ++dy)
                for (int dx = -4; dx <= 4 && same; ++dx) same = labels[size_t(y + dy) * scene.width + x + dx] == l;
            flat[size_t(y) * scene.width + x] = same;
        }
    return flat;
}

constexpr uint8_t kBackground[3] = {30, 45, 70}, kShape[3] = {230, 200, 120}, kLine[3] = {240, 240, 235};
AVFrame* sdrFrame(const Scene& scene) {
    AVFrame* f = av_frame_alloc();
    if (!f) return nullptr;
    f->format = AV_PIX_FMT_BGR0; f->width = int(scene.width); f->height = int(scene.height);
    f->color_range = AVCOL_RANGE_UNSPECIFIED; f->color_trc = AVCOL_TRC_IEC61966_2_1; f->colorspace = AVCOL_SPC_RGB;
    if (av_frame_get_buffer(f, 32) < 0) { av_frame_free(&f); return nullptr; }
    for (unsigned y = 0; y < scene.height; ++y)
        for (unsigned x = 0; x < scene.width; ++x) {
            const int l = scene.pixel(x, y);
            const uint8_t* c = l == 1 ? kShape : l == 2 ? kLine : kBackground;
            auto* p = f->data[0] + size_t(y) * f->linesize[0] + size_t(x) * 4;
            p[0] = c[2]; p[1] = c[1]; p[2] = c[0]; p[3] = 255;
        }
    return f;
}
// The supersampled reference, mixed in linear light and coded like the output.
std::vector<double> reference(const Scene& scene, const Coverage& coverage) {
    std::vector<double> out(size_t(scene.width) * scene.height * 3);
    for (size_t i = 0; i < coverage.shape.size(); ++i)
        for (unsigned c = 0; c < 3; ++c) {
            const double bg = srgbToLinear(kBackground[c] / 255.0);
            const double lin = bg + coverage.shape[i] * (srgbToLinear(kShape[c] / 255.0) - bg) +
                coverage.line[i] * (srgbToLinear(kLine[c] / 255.0) - bg);
            out[i * 3 + c] = linearToSrgb(lin) * 255.0;
        }
    return out;
}

// ---- HDR (P010 PQ BT.2020) ---------------------------------------------------------
double pq(double nits) {
    const double y = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), 2610.0 / 16384.0);
    return std::pow((3424.0 / 4096.0 + 2413.0 / 128.0 * y) / (1 + 2392.0 / 128.0 * y), 2523.0 / 32.0);
}
struct Code10 { uint16_t y, cb, cr; };
Code10 encode2020(double r, double g, double b) {
    const double R = pq(r), G = pq(g), B = pq(b), Y = 0.2627 * R + 0.6780 * G + 0.0593 * B;
    const auto q = [](double v) { return uint16_t(std::clamp(std::lround(v), 0l, 1023l)); };
    return {q(64 + 876 * Y), q(512 + 896 * (B - Y) / 1.8814), q(512 + 896 * (R - Y) / 1.4746)};
}
// Dark background, a 1000-nit rectangle / circle / lines, and a flat out-of-709
// green patch aligned to the 4:2:0 chroma grid.
constexpr unsigned kPatchX0 = 24, kPatchY0 = 300, kPatchX1 = 88, kPatchY1 = 352;
bool inPatch(unsigned x, unsigned y) { return x >= kPatchX0 && x < kPatchX1 && y >= kPatchY0 && y < kPatchY1; }
AVFrame* hdrFrame(const Scene& scene) {
    AVFrame* f = av_frame_alloc();
    if (!f) return nullptr;
    f->format = AV_PIX_FMT_P010; f->width = int(scene.width); f->height = int(scene.height);
    f->color_range = AVCOL_RANGE_MPEG; f->colorspace = AVCOL_SPC_BT2020_NCL; f->color_primaries = AVCOL_PRI_BT2020;
    f->color_trc = AVCOL_TRC_SMPTE2084;
    if (av_frame_get_buffer(f, 32) < 0) { av_frame_free(&f); return nullptr; }
    const Code10 dark = encode2020(2, 2, 2), bright = encode2020(1000, 1000, 1000), green = encode2020(8, 120, 8);
    for (unsigned y = 0; y < scene.height; ++y)
        for (unsigned x = 0; x < scene.width; ++x) {
            const Code10 c = inPatch(x, y) ? green : scene.pixel(x, y) ? bright : dark;
            reinterpret_cast<uint16_t*>(f->data[0] + size_t(y) * f->linesize[0])[x] = uint16_t(c.y << 6);
        }
    for (unsigned y = 0; y < scene.height / 2; ++y)
        for (unsigned x = 0; x < scene.width / 2; ++x) {
            const Code10 c = inPatch(x * 2, y * 2) ? green : dark; // neutral elsewhere
            auto* row = reinterpret_cast<uint16_t*>(f->data[1] + size_t(y) * f->linesize[1]);
            row[x * 2] = uint16_t(c.cb << 6); row[x * 2 + 1] = uint16_t(c.cr << 6);
        }
    return f;
}

AntiAliasSettings aa(AntiAliasMethod method, uint8_t quality, AntiAliasTemporal temporal = AntiAliasTemporal::Off) {
    AntiAliasSettings a; a.enabled = true; a.method = method; a.quality = quality; a.temporal = temporal;
    return a;
}

// ---- graph without NGX ----------------------------------------------------------------
struct Gpu {
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring;
    std::unique_ptr<pipeline::EnhanceGraph> graph = std::make_unique<pipeline::EnhanceGraph>(ctx, ring);
    pipeline::EnhanceGraphDesc desc;
    bool up = false, debug = std::getenv("VEYRA_AA_GPU_DEBUG") != nullptr;
    uint64_t frameId = 0;
    bool start(const engine::EnhancementSettings& settings, unsigned w, unsigned h, bool hdr) {
        if (!up) {
            gfx::DeviceContextDesc device; Status st;
            device.enableDebugLayer = debug;
            if (!ctx.initialize(device, st) || !ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, st)) return false;
            up = true;
        } else { ring.drainQueue(); graph->shutdown(); }
        desc = {};
        desc.sourceWidth = desc.workWidth = w; desc.sourceHeight = desc.workHeight = h;
        desc.rgbInput = !hdr; desc.hdrInput = hdr; desc.hdrOutput = hdr;
        desc.stillImage = true; desc.noFeatures = true; desc.noNgx = true;
        desc.enableNr = desc.enableSr = desc.enableFg = false; desc.outputDitherStep = 0;
        engine::StageRequest request; request.width = w; request.height = h; request.stillImage = true;
        engine::describeStages(request, settings, desc);
        if (!desc.fixedExecutionPlanError.empty()) { std::cout << "plan error: " << desc.fixedExecutionPlanError << std::endl; return false; }
        return graph->initialize(desc) && graph->createViews();
    }
    bool render(AVFrame* frame, bool reset, sink::RgbaImage& out) {
        pipeline::EnhanceGraph::FrameOutputs outputs;
        if (!graph->process(frame, double(frameId) * 1000.0 / 60.0, reset, outputs, ++frameId)) return false;
        auto* texture = graph->videoFrameResource(outputs.videoSlot);
        if (!texture) return false;
        if (texture->GetDesc().Format == DXGI_FORMAT_R8G8B8A8_UNORM) return sink::readRgba8(ctx, ring, texture, out);
        return readHalf(texture, out);
    }
    // FP16 readback of the scRGB output (8 bytes per pixel in `out.pixels`).
    bool readHalf(ID3D12Resource* texture, sink::RgbaImage& out) {
        const auto d = texture->GetDesc();
        if (d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes = 0;
        ctx.device()->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = bytes;
        buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
        Status st = Status::Ok; const auto slot = ring.slotCount() - 1; auto* list = ring.acquire(slot, st);
        if (!list) return false;
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter); list->ResourceBarrier(1, &b);
        if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;
        uint8_t* data = nullptr; D3D12_RANGE range{0, SIZE_T(bytes)};
        if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&data)))) return false;
        out.width = uint32_t(d.Width); out.height = d.Height; out.pixels.resize(size_t(out.width) * out.height * 8);
        for (UINT y = 0; y < out.height; ++y)
            std::memcpy(out.pixels.data() + size_t(y) * out.width * 8, data + fp.Offset + size_t(y) * fp.Footprint.RowPitch, size_t(out.width) * 8);
        D3D12_RANGE empty{}; readback->Unmap(0, &empty);
        return true;
    }
    std::optional<double> aaMs(unsigned parameter) {
        const auto m = graph->gpuMetrics();
        const auto& s = m.gpu[size_t(diagnostics::aaNodeStage(parameter))];
        if (s.state != diagnostics::SampleState::Measured) return std::nullopt;
        return s.milliseconds;
    }
    unsigned debugErrors() {
        if (!debug || !up) return 0;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
        if (FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)))) { std::printf("D3D12 debug info queue unavailable\n"); return 1; }
        unsigned errors = 0;
        for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
            SIZE_T size = 0; info->GetMessage(i, nullptr, &size); std::vector<uint8_t> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(info->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                std::printf("D3D12_ERROR id=%u %s\n", unsigned(message->ID), message->pDescription); ++errors;
            }
        }
        return errors;
    }
    ~Gpu() { if (up) { ring.drainQueue(); graph->shutdown(); ring.shutdown(); } }
};

engine::EnhancementSettings withAa(const AntiAliasSettings& a) { engine::EnhancementSettings s; s.antiAlias = a; return s; }
// scRGB FP16 output (the HDR10 default would write PQ R10G10B10A2).
engine::EnhancementSettings withAaHdr(const AntiAliasSettings& a) { auto s = withAa(a); s.hdrOutputMode = engine::HdrOutputMode::ScRgb; return s; }

struct Quality { double shapeMae = 0, lineMae = 0; size_t shapeEdges = 0, lineEdges = 0, flat = 0, flatChanged = 0, changed = 0; };
Quality measure(const Scene& scene, const sink::RgbaImage& out, const sink::RgbaImage& off, const Coverage& coverage,
                const std::vector<double>& truth, const std::vector<uint8_t>& flat) {
    Quality q;
    for (size_t i = 0; i < coverage.shape.size(); ++i) {
        bool differs = false;
        for (unsigned c = 0; c < 3; ++c) differs |= out.pixels[i * 4 + c] != off.pixels[i * 4 + c];
        q.changed += differs;
        if (flat[i]) { ++q.flat; q.flatChanged += differs; }
        double error = 0;
        for (unsigned c = 0; c < 3; ++c) error += std::abs(double(out.pixels[i * 4 + c]) - truth[i * 3 + c]) / 3.0;
        if (coverage.shape[i] > 0 && coverage.shape[i] < 1) { q.shapeMae += error; ++q.shapeEdges; }
        if (coverage.line[i] > 0 && coverage.line[i] < 1) { q.lineMae += error; ++q.lineEdges; }
    }
    if (q.shapeEdges) q.shapeMae /= double(q.shapeEdges);
    if (q.lineEdges) q.lineMae /= double(q.lineEdges);
    (void)scene;
    return q;
}

int sdrCase(const std::string& name, const AntiAliasSettings& setting) {
    const Scene scene;
    const auto coverage = supersample(scene);
    const auto truth = reference(scene, coverage);
    const auto flat = flatMask(scene);
    AVFrame* frame = sdrFrame(scene);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage off, on;
    AntiAliasSettings disabled = setting; disabled.enabled = false;
    bool ok = frame && gpu->start(withAa(disabled), scene.width, scene.height, false) && gpu->render(frame, true, off) &&
              gpu->start(withAa(setting), scene.width, scene.height, false) && gpu->render(frame, true, on) &&
              off.pixels.size() == on.pixels.size() && on.pixels.size() == size_t(scene.width) * scene.height * 4;
    check(ok, name + ": graph runs with the effect off and on");
    if (ok) {
        const auto base = measure(scene, off, off, coverage, truth, flat);
        const auto q = measure(scene, on, off, coverage, truth, flat);
        std::printf("METRIC case=%s changed=%zu flat=%zu flatChanged=%zu shapeEdges=%zu shapeMae=%.3f (off %.3f) lineEdges=%zu lineMae=%.3f (off %.3f)\n",
                    name.c_str(), q.changed, q.flat, q.flatChanged, q.shapeEdges, q.shapeMae, base.shapeMae, q.lineEdges, q.lineMae, base.lineMae);
        check(q.flat > 100000 && q.flatChanged == 0, name + ": flat areas are bit-identical to the effect switched off");
        check(q.changed > 500, name + ": edges are processed");
        check(q.shapeMae < base.shapeMae * 0.85, name + ": edge error against the supersampled reference drops by more than 15%");
    }
    av_frame_free(&frame);
    const auto errors = gpu->debugErrors();
    check(errors == 0, name + ": no D3D12 debug-layer errors");
    return failures ? 1 : 0;
}

bool finiteHalf(const sink::RgbaImage& image, float& maxValue, float& minValue) {
    maxValue = -1e30f; minValue = 1e30f;
    const auto* h = reinterpret_cast<const uint16_t*>(image.pixels.data());
    for (size_t i = 0; i < image.pixels.size() / 2; ++i) {
        if (i % 4 == 3) continue;
        const float v = DirectX::PackedVector::XMConvertHalfToFloat(h[i]);
        if (!std::isfinite(v)) return false;
        maxValue = std::max(maxValue, v); minValue = std::min(minValue, v);
    }
    return true;
}

int hdrCase(const std::string& name, const AntiAliasSettings& setting) {
    const Scene scene;
    const auto flat = flatMask(scene);
    AVFrame* frame = hdrFrame(scene);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage off, on;
    AntiAliasSettings disabled = setting; disabled.enabled = false;
    bool ok = frame && gpu->start(withAaHdr(disabled), scene.width, scene.height, true) && gpu->render(frame, true, off) &&
              gpu->start(withAaHdr(setting), scene.width, scene.height, true) && gpu->render(frame, true, on) &&
              on.pixels.size() == size_t(scene.width) * scene.height * 8 && off.pixels.size() == on.pixels.size();
    check(ok, name + ": HDR graph runs with the effect off and on (scRGB FP16 output)");
    if (ok) {
        float maxOn = 0, minOn = 0, maxOff = 0, minOff = 0;
        const bool finite = finiteHalf(on, maxOn, minOn) && finiteHalf(off, maxOff, minOff);
        size_t flatPixels = 0, flatChanged = 0, changed = 0, patchNegative = 0, patchPixels = 0;
        const auto* a = reinterpret_cast<const uint16_t*>(on.pixels.data());
        const auto* b = reinterpret_cast<const uint16_t*>(off.pixels.data());
        for (unsigned y = 0; y < scene.height; ++y)
            for (unsigned x = 0; x < scene.width; ++x) {
                const size_t i = size_t(y) * scene.width + x;
                const bool differs = a[i * 4] != b[i * 4] || a[i * 4 + 1] != b[i * 4 + 1] || a[i * 4 + 2] != b[i * 4 + 2];
                changed += differs;
                // The patch is flat away from its own border; the scene there is background.
                const bool patchCore = x >= kPatchX0 + 6 && x + 6 < kPatchX1 && y >= kPatchY0 + 6 && y + 6 < kPatchY1;
                const bool nearPatch = x + 8 >= kPatchX0 && x < kPatchX1 + 8 && y + 8 >= kPatchY0 && y < kPatchY1 + 8;
                if ((flat[i] && !nearPatch) || patchCore) { ++flatPixels; flatChanged += differs; }
                if (patchCore) {
                    ++patchPixels;
                    const float r = DirectX::PackedVector::XMConvertHalfToFloat(a[i * 4]);
                    const float bl = DirectX::PackedVector::XMConvertHalfToFloat(a[i * 4 + 2]);
                    patchNegative += r < 0 || bl < 0;
                }
            }
        std::printf("METRIC case=%s changed=%zu flat=%zu flatChanged=%zu max=%.3f (off %.3f) min=%.4f (off %.4f) patchNegative=%zu/%zu\n",
                    name.c_str(), changed, flatPixels, flatChanged, maxOn, maxOff, minOn, minOff, patchNegative, patchPixels);
        check(finite, name + ": no NaN or infinity in the HDR output");
        check(flatPixels > 100000 && flatChanged == 0, name + ": flat highlights, background and out-of-709 patch are bit-identical");
        check(changed > 500, name + ": HDR edges are processed");
        check(maxOn > 10.0f && std::abs(maxOn - maxOff) < 0.05f * maxOff, name + ": 1000-nit highlights keep their level");
        check(patchPixels && patchNegative == patchPixels, name + ": out-of-709 colours keep their negative components");
    }
    av_frame_free(&frame);
    const auto errors = gpu->debugErrors();
    check(errors == 0, name + ": no D3D12 debug-layer errors");
    return failures ? 1 : 0;
}

int temporalCase(const std::string& name, AntiAliasTemporal temporal) {
    const Scene scene;
    AVFrame* frame = sdrFrame(scene);
    auto gpu = std::make_unique<Gpu>();
    auto setting = aa(AntiAliasMethod::Smaa, 2, temporal);
    sink::RgbaImage first, second, third, again, plain;
    bool ok = frame && gpu->start(withAa(setting), scene.width, scene.height, false) && gpu->render(frame, true, first) &&
              gpu->render(frame, false, second) && gpu->render(frame, false, third) && gpu->render(frame, true, again);
    check(ok, name + ": temporal SMAA runs four frames");
    if (ok) {
        size_t historyChanged = 0;
        for (size_t i = 0; i < first.pixels.size(); ++i) historyChanged += first.pixels[i] != third.pixels[i];
        std::printf("METRIC case=%s historyChangedBytes=%zu\n", name.c_str(), historyChanged);
        check(first.pixels == again.pixels, name + ": a reset frame ignores the history (deterministic)");
        check(historyChanged > 0, name + ": later frames accumulate the history");
        // A live history-weight edit is a uniform update, not a rebuild.
        auto edited = withAa(setting); edited.antiAlias.historyWeight = 0.0f;
        sink::RgbaImage noHistory;
        ok = gpu->graph->applySettings(edited) && gpu->render(frame, false, noHistory);
        // `again` reset the history to phase 0, so this frame is phase 1 like `second`,
        // which carried the first frame's history.
        size_t differsFromAccumulated = 0;
        for (size_t i = 0; ok && i < second.pixels.size(); ++i) differsFromAccumulated += noHistory.pixels[i] != second.pixels[i];
        check(ok && differsFromAccumulated > 0, name + ": history weight 0 is applied live and drops the accumulation");
    }
    av_frame_free(&frame);
    const auto errors = gpu->debugErrors();
    check(errors == 0, name + ": no D3D12 debug-layer errors");
    return failures ? 1 : 0;
}

int liveMlaaCase() {
    const Scene scene;
    AVFrame* frame = sdrFrame(scene);
    auto gpu = std::make_unique<Gpu>();
    auto setting = aa(AntiAliasMethod::Mlaa, 0);
    sink::RgbaImage strong, weak, rebuilt;
    auto weakSettings = withAa(setting); weakSettings.antiAlias.mlaaStrength = 0.3f;
    bool ok = frame && gpu->start(withAa(setting), scene.width, scene.height, false) && gpu->render(frame, true, strong) &&
              gpu->graph->applySettings(weakSettings) && gpu->render(frame, true, weak) &&
              gpu->start(weakSettings, scene.width, scene.height, false) && gpu->render(frame, true, rebuilt);
    check(ok, "mlaa-live: strength edit applies without a rebuild");
    check(ok && weak.pixels != strong.pixels && weak.pixels == rebuilt.pixels, "mlaa-live: the live value equals a graph built with it");
    av_frame_free(&frame);
    return failures ? 1 : 0;
}

// ---- real NGX graphs: node slots ------------------------------------------------------
int nodeCase(const std::string& name, std::initializer_list<engine::ChainNode> nodes, unsigned expectedAa) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); (void)com;
    auto chain = std::make_unique<engine::EffectChain>(); chain->mode = engine::ChainMode::Node;
    bool nr = false, sr = false;
    for (const auto& n : nodes) {
        chain->nodes[chain->nodeCount++] = n;
        nr |= n.type == EffectType::NrEnhance; sr |= n.type == EffectType::SuperResolution;
    }
    engine::EnhancementSettings settings; engine::fromChain(*chain, settings);
    settings.multiplier = 1; settings.nrMotion = engine::MotionSource::Zero; settings.srTarget = pipeline::SrTarget::Qhd;
    const auto order = engine::runtimeOrder(*chain);
    engine::StageRequest request; request.width = 640; request.height = 360; request.nr = nr; request.sr = sr; request.nodeOrder = &*order;
    pipeline::EnhanceGraphDesc desc; engine::describeStages(request, settings, desc);
    check(desc.fixedExecutionPlanError.empty() && desc.validateFixedExecutionPlan().empty(), name + ": plan accepted");
    if (!failures) {
        desc.rgbInput = true; desc.runtimeAbsPath = runtime::localRuntimeDirectory().wstring();
        engine::PreviewGpuSession session;
        const Scene scene;
        AVFrame* frame = sdrFrame(scene);
        bool ok = frame && session.initialize() && session.graph->initialize(desc) && session.graph->createViews();
        pipeline::EnhanceGraph::FrameOutputs out;
        sink::RgbaImage image;
        ok = ok && session.graph->process(frame, 0.0, true, out, 1) && session.ring.waitIdle() &&
             sink::readRgba8(session.context, session.ring, session.graph->videoFrameResource(out.videoSlot), image);
        unsigned measured = 0;
        const auto metrics = session.graph->gpuMetrics();
        for (unsigned i = 0; i < engine::kMaxAntiAliasInstances; ++i)
            if (metrics.gpu[size_t(diagnostics::aaNodeStage(i))].state == diagnostics::SampleState::Measured) ++measured;
        std::string extents;
        for (uint32_t i = 0; desc.fixedExecutionPlan && i < desc.fixedExecutionPlan->stepCount; ++i) {
            const auto& step = desc.fixedExecutionPlan->steps[i];
            if (step.type == EffectType::AntiAliasing)
                extents += std::format(" aa{}={}x{}@slot{}", step.parameterIndex, step.output.width, step.output.height, int(desc.slotOfStep(i)));
        }
        std::printf("METRIC case=%s out=%ux%u nrEval=%llu srEval=%llu aaMeasured=%u%s\n", name.c_str(), image.width, image.height,
                    (unsigned long long)session.graph->metrics().nrEvaluateCount, (unsigned long long)session.graph->metrics().srEvaluateCount,
                    measured, extents.c_str());
        check(ok && measured == expectedAa, name + ": every anti-aliasing node executed on the GPU");
        check(ok && (!nr || session.graph->metrics().nrEvaluateCount > 0) && (!sr || session.graph->srEnabled()), name + ": SR / NR ran alongside");
        out = {};
        session.ring.drainQueue(); session.graph->shutdown(); av_frame_free(&frame); session.shutdown();
    }
    return failures ? 1 : 0;
}
engine::ChainNode aaNode(const AntiAliasSettings& a) { engine::ChainNode n; n.type = EffectType::AntiAliasing; n.enabled = true; n.aa = a; return n; }
engine::ChainNode srNode() { engine::ChainNode n; n.type = EffectType::SuperResolution; n.enabled = true; return n; }
engine::ChainNode nrNode() {
    engine::ChainNode n; n.type = EffectType::NrEnhance; n.enabled = true;
    n.nr.sizePolicy = pipeline::NrSizePolicy::Realtime; n.nr.temporal = false; return n;
}

// ---- tiled full-resolution image path -------------------------------------------------
// MLAA only uses integer loads, so tiles with a halo past its reach must match a
// whole-image graph exactly. SMAA / FXAA search with normalised coordinates;
// a different texture size changes their bilinear sample positions by rounding,
// so they are held to a small tolerance that must not cluster at the tile seams.
bool tiledMatches(const char* label, const AntiAliasSettings& setting, bool exact) {
    const Scene scene(2900, 1700);
    AVFrame* frame = sdrFrame(scene);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage whole, input, tiled, tiledOff;
    bool ok = frame && gpu->start(withAa(setting), scene.width, scene.height, false) && gpu->render(frame, true, whole);
    input.width = scene.width; input.height = scene.height; input.pixels.resize(size_t(scene.width) * scene.height * 4);
    for (unsigned y = 0; frame && y < scene.height; ++y)
        for (unsigned x = 0; x < scene.width; ++x) {
            const auto* p = frame->data[0] + size_t(y) * frame->linesize[0] + size_t(x) * 4;
            auto* q = input.pixels.data() + (size_t(y) * scene.width + x) * 4;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = 255;
        }
    std::atomic<bool> cancel{false};
    engine::TiledImageProcessor::Stats stats, statsOff;
    pipeline::EnhanceGraphDesc desc; desc.enableNr = false; desc.noFeatures = true; desc.settingsRevision = 1; desc.outputDitherStep = 0;
    desc.runtimeAbsPath = runtime::localRuntimeDirectory().wstring();
    engine::describeAntiAlias(withAa(setting), desc);
    pipeline::EnhanceGraphDesc descOff = desc; descOff.antiAlias = {};
    ok = ok && engine::TiledImageProcessor::process(gpu->ctx, gpu->ring, input, tiled, desc, cancel, stats, {}) &&
         engine::TiledImageProcessor::process(gpu->ctx, gpu->ring, input, tiledOff, descOff, cancel, statsOff, {});
    check(ok && stats.tiles > 1 && tiled.pixels.size() == whole.pixels.size(), std::string(label) + ": the tiled path runs anti-aliasing per tile");
    bool matches = false;
    if (ok && tiled.pixels.size() == whole.pixels.size()) {
        int maxDifference = 0; size_t differing = 0, changed = 0, nearSeam = 0;
        for (size_t i = 0; i < whole.pixels.size(); ++i) {
            if (i % 4 == 3) continue;
            const int d = std::abs(int(tiled.pixels[i]) - int(whole.pixels[i]));
            maxDifference = std::max(maxDifference, d); differing += d != 0; changed += tiled.pixels[i] != tiledOff.pixels[i];
            if (!d) continue;
            // Within 64 px of an internal tile core boundary (cores are 1280 px).
            const unsigned x = unsigned(i / 4 % scene.width), y = unsigned(i / 4 / scene.width);
            const unsigned dx = std::min(x % 1280, 1280 - x % 1280), dy = std::min(y % 1280, 1280 - y % 1280);
            nearSeam += (x >= 640 && dx <= 64) || (y >= 640 && dy <= 64);
        }
        const size_t channels = whole.pixels.size() / 4 * 3;
        std::printf("METRIC case=tiled-%s tiles=%u maxDifference=%d differingBytes=%zu of %zu nearSeam=%zu changedBytes=%zu\n",
                    label, stats.tiles, maxDifference, differing, channels, nearSeam, changed);
        // About a sixth of the picture lies within 64 px of a seam.
        matches = exact ? differing == 0 : maxDifference <= 4 && differing * 10000 <= channels && nearSeam * 3 <= differing + 3;
        check(matches, std::string(label) + (exact ? ": tiles match one whole-image graph exactly" :
              ": tiles match one whole-image graph within the bilinear rounding tolerance, not clustered at seams"));
        check(changed > 1000, std::string(label) + ": anti-aliasing changed the tiled result");
    }
    av_frame_free(&frame);
    return matches;
}
int tiledCase() {
    tiledMatches("mlaa", aa(AntiAliasMethod::Mlaa, 0), true);
    tiledMatches("smaa-high", aa(AntiAliasMethod::Smaa, 2), false);
    return failures ? 1 : 0;
}

// ---- timing -----------------------------------------------------------------------------
int timingCase(unsigned width, unsigned height, bool dense = false) {
    const Scene scene(width, height, dense);
    AVFrame* frame = sdrFrame(scene);
    auto gpu = std::make_unique<Gpu>();
    const struct { const char* label; AntiAliasSettings a; } methods[] = {
        {"FXAA-medium", aa(AntiAliasMethod::Fxaa, 0)}, {"FXAA-high", aa(AntiAliasMethod::Fxaa, 1)}, {"FXAA-ultra", aa(AntiAliasMethod::Fxaa, 2)},
        {"SMAA-low", aa(AntiAliasMethod::Smaa, 0)}, {"SMAA-medium", aa(AntiAliasMethod::Smaa, 1)}, {"SMAA-high", aa(AntiAliasMethod::Smaa, 2)},
        {"SMAA-ultra", aa(AntiAliasMethod::Smaa, 3)}, {"SMAA-high-T2x", aa(AntiAliasMethod::Smaa, 2, AntiAliasTemporal::T2x)},
        {"SMAA-high-4x", aa(AntiAliasMethod::Smaa, 2, AntiAliasTemporal::X4)}, {"MLAA", aa(AntiAliasMethod::Mlaa, 0)},
    };
    bool ok = frame != nullptr;
    for (const auto& m : methods) {
        ok = ok && gpu->start(withAa(m.a), width, height, false);
        std::vector<double> samples;
        sink::RgbaImage image;
        for (int i = 0; ok && i < 40; ++i) {
            pipeline::EnhanceGraph::FrameOutputs outputs;
            ok = gpu->graph->process(frame, double(i) * 1000.0 / 60.0, i == 0, outputs, ++gpu->frameId) && gpu->ring.waitIdle();
            if (const auto ms = gpu->aaMs(0); ok && i >= 8 && ms) samples.push_back(*ms);
        }
        std::sort(samples.begin(), samples.end());
        const double median = samples.empty() ? -1 : samples[samples.size() / 2];
        const double p95 = samples.empty() ? -1 : samples[std::min(samples.size() - 1, size_t(double(samples.size()) * 0.95))];
        std::printf("TIMING extent=%ux%u content=%s method=%s samples=%zu medianMs=%.3f p95Ms=%.3f\n", width, height,
                    dense ? "dense-stripes" : "sparse-shapes", m.label, samples.size(), median, p95);
        ok = ok && samples.size() >= 20;
    }
    check(ok, std::format("timing-{}x{}: every method measured", width, height));
    av_frame_free(&frame);
    return failures ? 1 : 0;
}

struct Case { std::string name; std::function<int()> run; };
std::vector<Case> cases() {
    std::vector<Case> c;
    const struct { const char* name; AntiAliasSettings a; } sdr[] = {
        {"sdr-fxaa-medium", aa(AntiAliasMethod::Fxaa, 0)}, {"sdr-fxaa-high", aa(AntiAliasMethod::Fxaa, 1)}, {"sdr-fxaa-ultra", aa(AntiAliasMethod::Fxaa, 2)},
        {"sdr-smaa-low", aa(AntiAliasMethod::Smaa, 0)}, {"sdr-smaa-medium", aa(AntiAliasMethod::Smaa, 1)}, {"sdr-smaa-high", aa(AntiAliasMethod::Smaa, 2)},
        {"sdr-smaa-ultra", aa(AntiAliasMethod::Smaa, 3)}, {"sdr-mlaa", aa(AntiAliasMethod::Mlaa, 0)},
    };
    for (const auto& s : sdr) c.push_back({s.name, [s] { return sdrCase(s.name, s.a); }});
    c.push_back({"hdr-fxaa", [] { return hdrCase("hdr-fxaa", aa(AntiAliasMethod::Fxaa, 1)); }});
    c.push_back({"hdr-smaa", [] { return hdrCase("hdr-smaa", aa(AntiAliasMethod::Smaa, 2)); }});
    c.push_back({"hdr-mlaa", [] { return hdrCase("hdr-mlaa", aa(AntiAliasMethod::Mlaa, 0)); }});
    c.push_back({"temporal-t2x", [] { return temporalCase("temporal-t2x", AntiAliasTemporal::T2x); }});
    c.push_back({"temporal-4x", [] { return temporalCase("temporal-4x", AntiAliasTemporal::X4); }});
    c.push_back({"mlaa-live", [] { return liveMlaaCase(); }});
    c.push_back({"node-prenr", [] { return nodeCase("node-prenr", {srNode(), aaNode(aa(AntiAliasMethod::Smaa, 2)), nrNode()}, 1); }});
    c.push_back({"node-source-and-tail", [] {
        return nodeCase("node-source-and-tail", {aaNode(aa(AntiAliasMethod::Smaa, 0)), srNode(), nrNode(), aaNode(aa(AntiAliasMethod::Fxaa, 1))}, 2); }});
    c.push_back({"tiled-image", [] { return tiledCase(); }});
    c.push_back({"timing-1080p", [] { return timingCase(1920, 1080); }});
    c.push_back({"timing-1440p", [] { return timingCase(2560, 1440); }});
    c.push_back({"timing-2160p", [] { return timingCase(3840, 2160); }});
    c.push_back({"timing-dense-2160p", [] { return timingCase(3840, 2160, true); }});
    return c;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto all = cases();
    if (argc == 2 && std::wstring_view(argv[1]) == L"--list") {
        for (const auto& c : all) std::cout << c.name << '\n';
        return 0;
    }
    if (argc == 3 && std::wstring_view(argv[1]) == L"--case") {
        const std::wstring wanted = argv[2];
        for (const auto& c : all)
            if (std::wstring(c.name.begin(), c.name.end()) == wanted) {
                const int result = c.run();
                std::printf(result ? "anti-alias gpu: %s FAILED\n" : "anti-alias gpu: %s passed\n", c.name.c_str());
                return result;
            }
        std::cout << "unknown case" << std::endl;
        return 2;
    }
    return 2;
}
