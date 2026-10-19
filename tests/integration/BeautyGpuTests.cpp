// GPU checks for the beauty node (face restore + skin smoothing), one case per process
// (like veyra_anti_alias_gpu_tests):
//
//   veyra_beauty_gpu_tests.exe --list
//   veyra_beauty_gpu_tests.exe --case <name> <reference-dir> <output-dir>
//
// <reference-dir> is the YuNet reference folder (scripts/beauty/yunet-reference.py):
// public test pictures in originals/ and OpenCV's faces per detector size, used here
// to know where the faces are. The real graph runs on those pictures; masks are
// judged in the face core (well inside every oval) and far outside every face.
//   off-identity      enabled node with both sub-functions off == no node, bit for bit
//   restore-grade     a fused colour grade, face restore 100%: core == ungraded picture,
//                     far outside == graded picture
//   restore-half      50%: core == the midpoint of graded and ungraded
//   smooth-*          skin texture drops in the face, eyes keep theirs, outside unchanged;
//                     before / after / mask pictures are written for a look
//   two-nodes         smoothing node then a restore node: the second returns to the original
//   hdr               P010 PQ input, scRGB output: finite, a 1000-nit patch and an out-of-709
//                     patch outside the faces bit-identical, the face changed
//   tracking          a moving face (video timing): faces follow it, detections at the rate
//   no-model          a missing model: preview runs (notice, no faces), export refuses
//   timing-*          GPU milliseconds of the node and of the shared detection
// VEYRA_BEAUTY_GPU_DEBUG=1 enables the D3D12 debug layer and fails on its errors.
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/engine/GraphDescription.h"
#include "veyra/engine/EffectChain.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/sink/ImageExportSink.h"
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
using namespace veyra;
using engine::BeautySettings;
using engine::EffectType;
namespace fs = std::filesystem;

fs::path gRefs, gOut;
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

// ---- pictures and faces -------------------------------------------------------------
struct Face { double x = 0, y = 0, w = 0, h = 0; double lm[10]{}; double score = 0; };
struct Picture { sink::RgbaImage image; std::vector<Face> faces; };

bool loadPicture(const std::string& stem, Picture& p) {
    fs::path file;
    for (const auto& f : fs::directory_iterator(gRefs / "originals"))
        if (f.path().stem().string() == stem) file = f.path();
    if (file.empty() || !sink::loadImage(file.wstring(), p.image)) return false;
    // OpenCV's faces at the balanced detector size (512 on the long side), scaled to the picture.
    for (const auto& f : fs::directory_iterator(gRefs)) {
        const auto name = f.path().filename().string();
        if (!f.is_directory() || name.rfind(stem + "-", 0) != 0) continue;
        uint32_t w = 0, h = 0, pw = 0, ph = 0;
        std::ifstream meta(f.path() / "meta.txt");
        if (!(meta >> w >> h >> pw >> ph) || std::max(w, h) != 512) continue;
        const double sx = double(p.image.width) / w, sy = double(p.image.height) / h;
        std::ifstream faces(f.path() / "faces.txt");
        std::string line;
        while (std::getline(faces, line)) {
            std::istringstream row(line);
            std::vector<double> v; double x;
            while (row >> x) v.push_back(x);
            if (v.size() != 15) continue;
            Face face;
            face.x = v[0] * sx; face.y = v[1] * sy; face.w = v[2] * sx; face.h = v[3] * sy;
            for (int k = 0; k < 10; ++k) face.lm[k] = v[4 + size_t(k)] * (k % 2 ? sy : sx);
            face.score = v[14];
            p.faces.push_back(face);
        }
    }
    return !p.faces.empty();
}
sink::RgbaImage resized(const sink::RgbaImage& in, unsigned w, unsigned h);
// The picture (and its faces) enlarged so the frame is `rows` tall: the sizes video has.
Picture enlarged(const Picture& p, unsigned rows) {
    Picture out;
    const double k = double(rows) / p.image.height;
    out.image = resized(p.image, unsigned(std::lround(p.image.width * k)), rows);
    out.faces = p.faces;
    for (auto& f : out.faces) {
        f.x *= k; f.y *= k; f.w *= k; f.h *= k;
        for (auto& v : f.lm) v *= k;
    }
    return out;
}
// Normalised distance from a face centre (1 = the box half size).
double faceDistance(const Face& f, double x, double y) {
    const double cx = f.x + f.w / 2, cy = f.y + f.h / 2;
    return std::hypot((x + 0.5 - cx) / (f.w / 2), (y + 0.5 - cy) / (f.h / 2));
}
bool inCore(const Picture& p, unsigned x, unsigned y) {
    for (const auto& f : p.faces) if (faceDistance(f, x, y) < 0.55) return true;
    return false;
}
bool farOutside(const Picture& p, unsigned x, unsigned y) {
    for (const auto& f : p.faces) if (faceDistance(f, x, y) < 2.6) return false;
    return true;
}
sink::RgbaImage resized(const sink::RgbaImage& in, unsigned w, unsigned h) {
    sink::RgbaImage out; out.width = w; out.height = h; out.pixels.resize(size_t(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const double sx = (x + 0.5) * in.width / w - 0.5, sy = (y + 0.5) * in.height / h - 0.5;
            const int x0 = std::clamp(int(std::floor(sx)), 0, int(in.width) - 1), y0 = std::clamp(int(std::floor(sy)), 0, int(in.height) - 1);
            const int x1 = std::min(x0 + 1, int(in.width) - 1), y1 = std::min(y0 + 1, int(in.height) - 1);
            const double fx = std::clamp(sx - x0, 0.0, 1.0), fy = std::clamp(sy - y0, 0.0, 1.0);
            for (int c = 0; c < 4; ++c) {
                const auto at = [&](int xx, int yy) { return double(in.pixels[(size_t(yy) * in.width + xx) * 4 + c]); };
                const double v = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
                out.pixels[(size_t(y) * w + x) * 4 + c] = uint8_t(std::clamp(std::lround(v), 0l, 255l));
            }
        }
    return out;
}
// The picture shifted by (dx, dy) inside a canvas of its own size, grey around it.
sink::RgbaImage shifted(const sink::RgbaImage& in, int dx, int dy) {
    sink::RgbaImage out = in;
    for (unsigned y = 0; y < in.height; ++y)
        for (unsigned x = 0; x < in.width; ++x) {
            const int sx = int(x) - dx, sy = int(y) - dy;
            uint8_t* d = out.pixels.data() + (size_t(y) * in.width + x) * 4;
            if (sx < 0 || sy < 0 || sx >= int(in.width) || sy >= int(in.height)) { d[0] = d[1] = d[2] = 96; d[3] = 255; continue; }
            std::memcpy(d, in.pixels.data() + (size_t(sy) * in.width + sx) * 4, 4);
        }
    return out;
}

AVFrame* rgbaFrame(const sink::RgbaImage& image) {
    AVFrame* f = av_frame_alloc();
    if (!f) return nullptr;
    f->format = AV_PIX_FMT_RGBA; f->width = int(image.width); f->height = int(image.height);
    f->color_range = AVCOL_RANGE_JPEG; f->color_trc = AVCOL_TRC_IEC61966_2_1; f->colorspace = AVCOL_SPC_RGB;
    if (av_frame_get_buffer(f, 32) < 0) { av_frame_free(&f); return nullptr; }
    for (unsigned y = 0; y < image.height; ++y)
        std::memcpy(f->data[0] + size_t(y) * f->linesize[0], image.pixels.data() + size_t(y) * image.width * 4, size_t(image.width) * 4);
    return f;
}

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
// The picture as P010 PQ BT.2020 (SDR white at 203 nits), plus two 32x32 patches in
// the top-left corner: 1000-nit white and an out-of-709 green.
constexpr unsigned kPatch = 32;
bool hdrPatch(unsigned x, unsigned y, int& which) {
    which = -1;
    if (y >= 8 && y < 8 + kPatch && x >= 8 && x < 8 + kPatch) which = 0;
    if (y >= 8 && y < 8 + kPatch && x >= 48 && x < 48 + kPatch) which = 1;
    return which >= 0;
}
AVFrame* hdrFrame(const sink::RgbaImage& image) {
    AVFrame* f = av_frame_alloc();
    if (!f) return nullptr;
    f->format = AV_PIX_FMT_P010; f->width = int(image.width); f->height = int(image.height);
    f->color_range = AVCOL_RANGE_MPEG; f->colorspace = AVCOL_SPC_BT2020_NCL; f->color_primaries = AVCOL_PRI_BT2020;
    f->color_trc = AVCOL_TRC_SMPTE2084;
    if (av_frame_get_buffer(f, 32) < 0) { av_frame_free(&f); return nullptr; }
    std::vector<Code10> codes(size_t(image.width) * image.height);
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x) {
            int which = -1;
            Code10 c;
            if (hdrPatch(x, y, which)) c = which == 0 ? encode2020(1000, 1000, 1000) : encode2020(8, 120, 8);
            else {
                const uint8_t* p = image.pixels.data() + (size_t(y) * image.width + x) * 4;
                const double r = srgbToLinear(p[0] / 255.0), g = srgbToLinear(p[1] / 255.0), b = srgbToLinear(p[2] / 255.0);
                const double r2 = 0.6274 * r + 0.3293 * g + 0.0433 * b, g2 = 0.0691 * r + 0.9195 * g + 0.0114 * b, b2 = 0.0164 * r + 0.0880 * g + 0.8956 * b;
                c = encode2020(r2 * 203.0, g2 * 203.0, b2 * 203.0);
            }
            codes[size_t(y) * image.width + x] = c;
            reinterpret_cast<uint16_t*>(f->data[0] + size_t(y) * f->linesize[0])[x] = uint16_t(c.y << 6);
        }
    for (unsigned y = 0; y < image.height / 2; ++y)
        for (unsigned x = 0; x < image.width / 2; ++x) {
            uint32_t cb = 0, cr = 0;
            for (unsigned j = 0; j < 2; ++j) for (unsigned i = 0; i < 2; ++i) {
                const auto& c = codes[size_t(y * 2 + j) * image.width + x * 2 + i]; cb += c.cb; cr += c.cr;
            }
            auto* row = reinterpret_cast<uint16_t*>(f->data[1] + size_t(y) * f->linesize[1]);
            row[x * 2] = uint16_t(((cb + 2) / 4) << 6); row[x * 2 + 1] = uint16_t(((cr + 2) / 4) << 6);
        }
    return f;
}

// ---- graph without NGX ----------------------------------------------------------------
struct Gpu {
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring;
    std::unique_ptr<pipeline::EnhanceGraph> graph = std::make_unique<pipeline::EnhanceGraph>(ctx, ring);
    pipeline::EnhanceGraphDesc desc;
    bool up = false, debug = std::getenv("VEYRA_BEAUTY_GPU_DEBUG") != nullptr;
    uint64_t frameId = 0;
    std::string error;
    bool start(const engine::EnhancementSettings& settings, unsigned w, unsigned h, bool hdr, bool still = true,
               const engine::ChainRuntimeOrder* order = nullptr, const std::wstring& model = {}, bool exportJob = false) {
        if (!up) {
            gfx::DeviceContextDesc device; Status st;
            device.enableDebugLayer = debug;
            if (!ctx.initialize(device, st) || !ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, st)) return false;
            up = true;
        } else { ring.drainQueue(); graph->shutdown(); }
        desc = {};
        desc.sourceWidth = desc.workWidth = w; desc.sourceHeight = desc.workHeight = h;
        desc.rgbInput = !hdr; desc.hdrInput = hdr; desc.hdrOutput = hdr;
        desc.stillImage = still; desc.noFeatures = true; desc.noNgx = true;
        desc.enableNr = desc.enableSr = desc.enableFg = false; desc.outputDitherStep = 0;
        engine::StageRequest request; request.width = w; request.height = h; request.stillImage = still;
        request.nodeOrder = order; request.exportJob = exportJob;
        engine::describeStages(request, settings, desc);
        desc.stillImage = still;
        if (!model.empty()) desc.beautyModelPath = model;
        if (!desc.fixedExecutionPlanError.empty()) { error = desc.fixedExecutionPlanError; std::cout << "plan error: " << error << std::endl; return false; }
        frameId = 0;
        return graph->initialize(desc) && graph->createViews();
    }
    bool render(AVFrame* frame, bool reset, sink::RgbaImage& out) {
        pipeline::EnhanceGraph::FrameOutputs outputs;
        if (!graph->process(frame, double(frameId) * 1000.0 / 60.0, reset, outputs, frameId + 1)) return false;
        ++frameId;
        auto* texture = graph->videoFrameResource(outputs.videoSlot);
        if (!texture) return false;
        if (texture->GetDesc().Format == DXGI_FORMAT_R8G8B8A8_UNORM) return sink::readRgba8(ctx, ring, texture, out);
        return readHalf(texture, out);
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer(UINT64 bytes) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = bytes;
        buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return nullptr;
        return readback;
    }
    // FP16 readback of the scRGB output (8 bytes per pixel in `out.pixels`).
    bool readHalf(ID3D12Resource* texture, sink::RgbaImage& out) {
        const auto d = texture->GetDesc();
        if (d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes = 0;
        ctx.device()->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        auto readback = readbackBuffer(bytes);
        Status st = Status::Ok; const auto slot = ring.slotCount() - 1; auto* list = readback ? ring.acquire(slot, st) : nullptr;
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
    // The tracked faces after the last frame: header + eight faces, 16 floats each.
    bool readFaces(std::vector<float>& faces) {
        auto* buffer = graph->beautyFacesBuffer();
        auto readback = buffer ? readbackBuffer(9 * 64) : nullptr;
        Status st = Status::Ok; const auto slot = ring.slotCount() - 1; auto* list = readback ? ring.acquire(slot, st) : nullptr;
        if (!list) return false;
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &b);
        list->CopyBufferRegion(readback.Get(), 0, buffer, 0, 9 * 64);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter); list->ResourceBarrier(1, &b);
        if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;
        void* data = nullptr; D3D12_RANGE range{0, 9 * 64};
        if (FAILED(readback->Map(0, &range, &data))) return false;
        faces.resize(9 * 16);
        std::memcpy(faces.data(), data, 9 * 64);
        D3D12_RANGE empty{}; readback->Unmap(0, &empty);
        return true;
    }
    // The face outlines after the last frame: 8 slots x 128 float2 (BeautyFaces.hlsli kShape*).
    bool readShapes(std::vector<float>& shapes) {
        auto* buffer = graph->beautyShapesBuffer();
        constexpr UINT64 bytes = 8 * 128 * 8;
        auto readback = buffer ? readbackBuffer(bytes) : nullptr;
        Status st = Status::Ok; const auto slot = ring.slotCount() - 1; auto* list = readback ? ring.acquire(slot, st) : nullptr;
        if (!list) return false;
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &b);
        list->CopyBufferRegion(readback.Get(), 0, buffer, 0, bytes);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter); list->ResourceBarrier(1, &b);
        if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;
        void* data = nullptr; D3D12_RANGE range{0, SIZE_T(bytes)};
        if (FAILED(readback->Map(0, &range, &data))) return false;
        shapes.resize(bytes / 4);
        std::memcpy(shapes.data(), data, size_t(bytes));
        D3D12_RANGE empty{}; readback->Unmap(0, &empty);
        return true;
    }
    std::optional<double> stageMs(diagnostics::GpuStage stage) {
        const auto m = graph->gpuMetrics();
        const auto& s = m.gpu[size_t(stage)];
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

// Header entry: meta.x (float index 12) is the face count as an integer.
uint32_t trackedCount(const std::vector<float>& faces) {
    uint32_t n = 0;
    if (faces.size() > 12) std::memcpy(&n, &faces[12], 4);
    return n;
}
BeautySettings restoreNode(float strength = 1.0f) { BeautySettings b; b.enabled = true; b.restore = true; b.restoreStrength = strength; return b; }
BeautySettings smoothNode(float amount = 0.6f, float radius = 4.0f) {
    BeautySettings b; b.enabled = true; b.smooth = true; b.smoothAmount = amount; b.radius = radius; b.detail = 0.5f; b.texture = 0.2f;
    return b;
}
// The strong end of the tuned range (amount 0.85, sigma 7, detail 0.4, texture 0.15).
BeautySettings strongNode() { auto b = smoothNode(0.85f, 7.0f); b.detail = 0.4f; b.texture = 0.15f; return b; }
engine::EnhancementSettings withBeauty(const BeautySettings& b) { engine::EnhancementSettings s; s.beauty = b; return s; }
// Mild enough that the face stays below white: the midpoint check needs unclipped values.
engine::EnhancementSettings graded(engine::EnhancementSettings s) {
    s.color.enabled = true; s.color.exposure = 0.3f; s.color.saturation = 25.0f; s.color.temperature = 25.0f;
    return s;
}
void save(const std::string& name, const sink::RgbaImage& image) {
    if (image.pixels.size() == size_t(image.width) * image.height * 4) (void)sink::saveImage((gOut / (name + ".png")).wstring(), image);
}

// 8-bit picture statistics.
struct Diff { size_t pixels = 0, differing = 0; int maxDiff = 0; };
template <typename Pred> Diff compare(const sink::RgbaImage& a, const sink::RgbaImage& b, Pred where) {
    Diff d;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            if (!where(x, y)) continue;
            ++d.pixels;
            int m = 0;
            for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(a.pixels[(size_t(y) * a.width + x) * 4 + c]) - int(b.pixels[(size_t(y) * a.width + x) * 4 + c])));
            d.differing += m > 0; d.maxDiff = std::max(d.maxDiff, m);
        }
    return d;
}
// Mean absolute difference of the luma from its 3x3 mean: the fine texture.
template <typename Pred> double texture(const sink::RgbaImage& a, Pred where) {
    const auto luma = [&](int x, int y) {
        x = std::clamp(x, 0, int(a.width) - 1); y = std::clamp(y, 0, int(a.height) - 1);
        const uint8_t* p = a.pixels.data() + (size_t(y) * a.width + x) * 4;
        return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    };
    double sum = 0; size_t n = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            if (!where(x, y)) continue;
            double mean = 0;
            for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i) mean += luma(int(x) + i, int(y) + j);
            sum += std::abs(luma(int(x), int(y)) - mean / 9.0); ++n;
        }
    return n ? sum / double(n) : 0.0;
}

int finish(Gpu& gpu, const std::string& name) {
    const auto errors = gpu.debugErrors();
    check(errors == 0, name + ": no D3D12 debug-layer errors");
    return failures ? 1 : 0;
}

int offIdentityCase() {
    Picture p;
    check(loadPicture("lena", p), "off-identity: picture and reference faces");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage plain, idle;
    BeautySettings idleNode; idleNode.enabled = true;
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, plain) &&
              gpu->start(withBeauty(idleNode), p.image.width, p.image.height, false) && gpu->render(frame, true, idle);
    check(ok && gpu->graph->beautyNodesActive(), "off-identity: a graph with an idle beauty node runs");
    const auto d = compare(plain, idle, [](unsigned, unsigned) { return true; });
    std::printf("METRIC case=off-identity pixels=%zu differing=%zu\n", d.pixels, d.differing);
    check(ok && d.pixels > 0 && d.differing == 0, "off-identity: both sub-functions off is bit-identical to no node");
    av_frame_free(&frame);
    return finish(*gpu, "off-identity");
}

int restoreCase(const std::string& name, float strength) {
    Picture p;
    check(loadPicture("lena", p), name + ": picture and reference faces");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage original, grade, restored;
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, original) &&
              gpu->start(graded({}), p.image.width, p.image.height, false) && gpu->render(frame, true, grade) &&
              gpu->start(graded(withBeauty(restoreNode(strength))), p.image.width, p.image.height, false) && gpu->render(frame, true, restored);
    check(ok, name + ": ungraded, graded and graded + restore graphs run");
    if (ok) {
        std::vector<float> faces;
        ok = gpu->readFaces(faces);
        std::printf("METRIC case=%s trackedFaces=%u score=%.3f\n", name.c_str(), ok ? trackedCount(faces) : 0u, ok ? faces[16 + 6] : 0.0f);
        check(ok && trackedCount(faces) >= 1, name + ": the tracker found the face on the picture's own frame");
        const auto outside = compare(restored, grade, [&](unsigned x, unsigned y) { return farOutside(p, x, y); });
        std::printf("METRIC case=%s outsidePixels=%zu outsideDiffering=%zu\n", name.c_str(), outside.pixels, outside.differing);
        check(outside.pixels > 1000 && outside.differing == 0, name + ": far from the face the graded picture is bit-identical");
        if (strength >= 1.0f) {
            const auto core = compare(restored, original, [&](unsigned x, unsigned y) { return inCore(p, x, y); });
            std::printf("METRIC case=%s corePixels=%zu coreDiffering=%zu coreMax=%d\n", name.c_str(), core.pixels, core.differing, core.maxDiff);
            check(core.pixels > 2000 && core.differing == 0, name + ": the face core is the ungraded picture, bit for bit");
        } else {
            // Expected: the linear midpoint of the graded and ungraded pictures (8-bit inputs, +-2 levels).
            size_t n = 0, bad = 0, clipped = 0; int worst = 0;
            for (unsigned y = 0; y < p.image.height; ++y)
                for (unsigned x = 0; x < p.image.width; ++x) {
                    if (!inCore(p, x, y)) continue;
                    bool edge = false;
                    for (int c = 0; c < 3; ++c) {
                        const size_t i = (size_t(y) * p.image.width + x) * 4 + c;
                        edge |= grade.pixels[i] >= 254 || grade.pixels[i] <= 1 || original.pixels[i] >= 254 || original.pixels[i] <= 1;
                    }
                    if (edge) { ++clipped; continue; }
                    ++n;
                    for (int c = 0; c < 3; ++c) {
                        const size_t i = (size_t(y) * p.image.width + x) * 4 + c;
                        const double mid = linearToSrgb((srgbToLinear(original.pixels[i] / 255.0) + srgbToLinear(grade.pixels[i] / 255.0)) * 0.5) * 255.0;
                        const int e = int(std::abs(std::lround(mid) - long(restored.pixels[i])));
                        worst = std::max(worst, e); bad += e > 2;
                    }
                }
            std::printf("METRIC case=%s corePixels=%zu clippedSkipped=%zu coreWorst=%d coreOver2=%zu\n", name.c_str(), n, clipped, worst, bad);
            check(n > 2000 && bad == 0, name + ": the face core is the linear midpoint (within 2 levels)");
        }
        save(name + "-graded", grade); save(name + "-restored", restored);
    }
    av_frame_free(&frame);
    return finish(*gpu, name);
}

int smoothCase(const std::string& name, const std::string& stem, BeautySettings setting) {
    // Native size: an enlarged picture has lost the skin texture the check measures.
    Picture p;
    check(loadPicture(stem, p), name + ": picture and reference faces");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage off, on, mask;
    BeautySettings masked = setting; masked.showMask = true;
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, off) &&
              gpu->start(withBeauty(setting), p.image.width, p.image.height, false) && gpu->render(frame, true, on) &&
              gpu->start(withBeauty(masked), p.image.width, p.image.height, false) && gpu->render(frame, true, mask);
    check(ok, name + ": graphs without and with smoothing run");
    if (ok) {
        save(name + "-off", off); save(name + "-on", on); save(name + "-mask", mask);
        const auto outside = compare(on, off, [&](unsigned x, unsigned y) { return farOutside(p, x, y); });
        // Skin texture: the face core minus the eyes; the eyes on their own.
        const auto nearEye = [&](unsigned x, unsigned y) {
            for (const auto& f : p.faces)
                for (int e = 0; e < 2; ++e)
                    if (std::hypot(x + 0.5 - f.lm[e * 2], y + 0.5 - f.lm[e * 2 + 1]) < f.w * 0.09) return true;
            return false;
        };
        const auto skin = [&](unsigned x, unsigned y) { return inCore(p, x, y) && !nearEye(x, y); };
        const double skinBefore = texture(off, skin), skinAfter = texture(on, skin);
        const double eyeBefore = texture(off, nearEye), eyeAfter = texture(on, nearEye);
        const auto changed = compare(on, off, [&](unsigned x, unsigned y) { return inCore(p, x, y); });
        std::printf("METRIC case=%s faces=%zu outsidePixels=%zu outsideDiffering=%zu coreChanged=%zu/%zu skinTexture=%.3f->%.3f (%.1f%%) eyeTexture=%.3f->%.3f (%.1f%%)\n",
                    name.c_str(), p.faces.size(), outside.pixels, outside.differing, changed.differing, changed.pixels, skinBefore, skinAfter,
                    100.0 * (skinAfter / skinBefore - 1.0), eyeBefore, eyeAfter, 100.0 * (eyeAfter / std::max(eyeBefore, 1e-9) - 1.0));
        check(outside.pixels > 1000 && outside.differing == 0, name + ": far from every face nothing changes");
        check(changed.differing * 4 > changed.pixels, name + ": the face is processed");
        const double skinDrop = 1.0 - skinAfter / skinBefore, eyeDrop = 1.0 - eyeAfter / std::max(eyeBefore, 1e-9);
        check(skinDrop > 0.08, name + ": fine skin texture in the face drops by more than 8%");
        check(eyeDrop < skinDrop, name + ": the eyes lose less of their detail than the skin (detail protection)");
    }
    av_frame_free(&frame);
    return finish(*gpu, name);
}

int twoNodeCase() {
    Picture p;
    check(loadPicture("lena", p), "two-nodes: picture and reference faces");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    // Node mode: smoothing, then a full restore.
    engine::EffectChain chain; chain.mode = engine::ChainMode::Node;
    auto& a = chain.nodes[chain.nodeCount++]; a.type = EffectType::Beauty; a.enabled = true; a.beauty = smoothNode(1.0f, 6.0f);
    auto& b = chain.nodes[chain.nodeCount++]; b.type = EffectType::Beauty; b.enabled = true; b.beauty = restoreNode(1.0f);
    engine::EnhancementSettings settings; engine::fromChain(chain, settings);
    const auto order = engine::runtimeOrder(chain);
    engine::EffectChain smoothOnly = chain; smoothOnly.nodeCount = 1;
    engine::EnhancementSettings first; engine::fromChain(smoothOnly, first);
    const auto firstOrder = engine::runtimeOrder(smoothOnly);
    sink::RgbaImage original, smoothed, both;
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, original) &&
              gpu->start(first, p.image.width, p.image.height, false, true, &*firstOrder) && gpu->render(frame, true, smoothed) &&
              gpu->start(settings, p.image.width, p.image.height, false, true, &*order) && gpu->render(frame, true, both);
    check(ok, "two-nodes: node chains run");
    if (ok) {
        const auto changed = compare(smoothed, original, [&](unsigned x, unsigned y) { return inCore(p, x, y); });
        const auto core = compare(both, original, [&](unsigned x, unsigned y) { return inCore(p, x, y); });
        std::printf("METRIC case=two-nodes smoothedCoreChanged=%zu/%zu restoredCoreDiffering=%zu\n", changed.differing, changed.pixels, core.differing);
        check(changed.differing * 4 > changed.pixels, "two-nodes: the first node changes the face");
        check(core.pixels > 2000 && core.differing == 0, "two-nodes: the second node's restore undoes the first in the face core");
    }
    av_frame_free(&frame);
    return finish(*gpu, "two-nodes");
}

int hdrCase() {
    Picture p;
    check(loadPicture("lena", p), "hdr: picture and reference faces");
    AVFrame* frame = hdrFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    auto on = withBeauty(smoothNode(0.8f, 4.0f)); on.beauty.restore = true; on.beauty.restoreStrength = 0.5f;
    on.hdrOutputMode = engine::HdrOutputMode::ScRgb;
    engine::EnhancementSettings offSettings; offSettings.hdrOutputMode = engine::HdrOutputMode::ScRgb;
    sink::RgbaImage off, out;
    bool ok = frame && gpu->start(offSettings, p.image.width, p.image.height, true) && gpu->render(frame, true, off) &&
              gpu->start(on, p.image.width, p.image.height, true) && gpu->render(frame, true, out) &&
              out.pixels.size() == size_t(p.image.width) * p.image.height * 8 && off.pixels.size() == out.pixels.size();
    check(ok, "hdr: HDR graphs run (scRGB FP16 output)");
    if (ok) {
        const auto* a = reinterpret_cast<const uint16_t*>(out.pixels.data());
        const auto* b = reinterpret_cast<const uint16_t*>(off.pixels.data());
        bool finite = true; size_t patch = 0, patchDiffering = 0, negative = 0, core = 0, coreChanged = 0; float peak = 0;
        for (unsigned y = 0; y < p.image.height; ++y)
            for (unsigned x = 0; x < p.image.width; ++x) {
                const size_t i = size_t(y) * p.image.width + x;
                bool differs = false;
                for (int c = 0; c < 3; ++c) {
                    const float v = DirectX::PackedVector::XMConvertHalfToFloat(a[i * 4 + c]);
                    finite = finite && std::isfinite(v);
                    differs |= a[i * 4 + c] != b[i * 4 + c];
                }
                int which = -1;
                if (hdrPatch(x, y, which) && x % kPatch > 1 && y % kPatch > 1) {
                    ++patch; patchDiffering += differs;
                    if (which == 0) peak = std::max(peak, DirectX::PackedVector::XMConvertHalfToFloat(a[i * 4]));
                    if (which == 1) negative += DirectX::PackedVector::XMConvertHalfToFloat(a[i * 4]) < 0.0f;
                }
                if (inCore(p, x, y)) { ++core; coreChanged += differs; }
            }
        std::printf("METRIC case=hdr finite=%d patchPixels=%zu patchDiffering=%zu peak=%.3f negativeGreen=%zu coreChanged=%zu/%zu\n",
                    finite, patch, patchDiffering, peak, negative, coreChanged, core);
        check(finite, "hdr: no NaN or infinity");
        check(patch > 1000 && patchDiffering == 0, "hdr: the 1000-nit and out-of-709 patches outside the face are bit-identical");
        check(peak > 10.0f && negative > 400, "hdr: the highlight keeps its level and the green keeps its negative components");
        check(coreChanged * 4 > core, "hdr: the face is processed");
    }
    av_frame_free(&frame);
    return finish(*gpu, "hdr");
}

int trackingCase() {
    Picture p;
    check(loadPicture("lena", p), "tracking: picture and reference faces");
    auto gpu = std::make_unique<Gpu>();
    auto settings = withBeauty(restoreNode(1.0f));
    settings.beautyDetection.stability = 0.5f;
    bool ok = gpu->start(settings, p.image.width, p.image.height, false, false);
    check(ok, "tracking: video-timed graph runs");
    const int frames = 48, step = 3;   // 3 px per frame at 60 fps
    std::vector<float> faces;
    double worst = 0;
    for (int i = 0; ok && i < frames; ++i) {
        const auto moved = shifted(p.image, i * step - frames * step / 2, 0);
        AVFrame* frame = rgbaFrame(moved);
        sink::RgbaImage out;
        ok = frame && gpu->render(frame, i == 0, out) && gpu->readFaces(faces);
        av_frame_free(&frame);
        if (!ok || trackedCount(faces) < 1) { ok = false; break; }
        const double expected = (p.faces[0].x + p.faces[0].w / 2 + (i * step - frames * step / 2)) / p.image.width;
        if (i >= 8) worst = std::max(worst, std::abs(double(faces[16]) - expected) * p.image.width);
    }
    const uint64_t detections = gpu->graph->beautyDetections();
    std::printf("METRIC case=tracking frames=%d detections=%llu worstCentreErrorPx=%.2f faceWidthPx=%.1f\n", frames,
                (unsigned long long)detections, worst, p.faces.empty() ? 0.0 : p.faces[0].w);
    check(ok, "tracking: the face is tracked on every frame");
    check(detections == uint64_t(frames), "tracking: every frame of the video is detected");
    check(ok && worst < 0.03 * p.faces[0].w, "tracking: the tracked centre stays within 3% of the face width of the moving face");
    // A reset (seek) detects on the very next frame.
    AVFrame* frame = rgbaFrame(p.image);
    sink::RgbaImage out;
    const auto before = gpu->graph->beautyDetections();
    ok = frame && gpu->render(frame, true, out);
    check(ok && gpu->graph->beautyDetections() == before + 1, "tracking: a reset frame detects immediately");
    av_frame_free(&frame);
    return finish(*gpu, "tracking");
}

// A face that rests, crosses the picture fast (16 px a frame at 60 fps, about six face
// widths a second) and stops. The tracked centre has to follow within a few percent of
// the face width and must not overshoot where the face stops: the 15 Hz predicting
// filter this replaces trailed by more than half a face and overshot by as much.
int trackingMotionCase() {
    Picture p;
    check(loadPicture("lena", p), "tracking-motion: picture and reference faces");
    const unsigned width = 1280, height = 640, x0 = 64, y0 = 64;
    sink::RgbaImage canvas; canvas.width = width; canvas.height = height; canvas.pixels.assign(size_t(width) * height * 4, 96);
    for (size_t i = 3; i < canvas.pixels.size(); i += 4) canvas.pixels[i] = 255;
    for (unsigned y = 0; y < p.image.height && y0 + y < height; ++y)
        std::memcpy(canvas.pixels.data() + (size_t(y0 + y) * width + x0) * 4, p.image.pixels.data() + size_t(y) * p.image.width * 4,
                    size_t(p.image.width) * 4);
    auto gpu = std::make_unique<Gpu>();
    auto settings = withBeauty(restoreNode(1.0f));
    settings.beautyDetection.quality = 2;
    settings.beautyDetection.stability = 0.5f;
    bool ok = gpu->start(settings, width, height, false, false);
    check(ok, "tracking-motion: video-timed graph runs");
    const int rest = 12, move = 30, after = 30, speed = 16;
    std::vector<double> error;   // tracked centre minus the true one, pixels
    std::vector<float> faces;
    const double reference = p.faces[0].x + p.faces[0].w / 2 + x0;
    for (int i = 0; ok && i < rest + move + after; ++i) {
        const int dx = std::clamp(i - rest, 0, move) * speed;
        AVFrame* frame = rgbaFrame(shifted(canvas, dx, 0));
        sink::RgbaImage out;
        ok = frame && gpu->render(frame, i == 0, out) && gpu->readFaces(faces) && trackedCount(faces) >= 1;
        av_frame_free(&frame);
        if (ok) error.push_back(double(faces[16]) * width - (reference + dx));
    }
    check(ok, "tracking-motion: the face is tracked on every frame");
    if (!ok) return finish(*gpu, "tracking-motion");
    // The detector's own offset at this size, measured at rest, is not tracking error.
    double bias = 0;
    for (int i = 4; i < rest; ++i) bias += error[i];
    bias /= double(rest - 4);
    double lag = 0, overshoot = 0, settled = -1;
    for (int i = rest + 4; i < rest + move; ++i) lag = std::max(lag, -(error[i] - bias));
    for (int i = rest + move; i < rest + move + after; ++i) overshoot = std::max(overshoot, error[i] - bias);
    for (int i = rest + move; i < rest + move + after; ++i) {
        bool calm = true;
        for (int k = i; k < rest + move + after; ++k) calm = calm && std::abs(error[k] - bias) < 0.015 * p.faces[0].w;
        if (calm) { settled = i - (rest + move); break; }
    }
    const double w = p.faces[0].w;
    std::printf("METRIC case=tracking-motion faceWidthPx=%.1f speedPxPerFrame=%d detections=%llu lagFaceWidths=%.4f overshootFaceWidths=%.4f "
                "settleFrames=%.0f\n", w, speed, (unsigned long long)gpu->graph->beautyDetections(), lag / w, overshoot / w, settled);
    check(gpu->graph->beautyDetections() == uint64_t(rest + move + after), "tracking-motion: every frame is detected");
    check(lag < 0.06 * w, "tracking-motion: a fast face is followed within 6% of its width");
    check(overshoot < 0.025 * w, "tracking-motion: the region does not overshoot where the face stops");
    check(settled >= 0 && settled <= 8, "tracking-motion: the region settles within 8 frames of the stop");
    return finish(*gpu, "tracking-motion");
}

// --- face mesh -------------------------------------------------------------------
// MediaPipe face_mesh_connections (v0.8.9) as closed outlines, as BeautyMeshDecode.hlsl:
// contour 36, lips 20, left eye 16, right eye 16, left brow 10, right brow 10.
constexpr int kOutline[108] = {
    10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148, 176, 149,
    150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109,
    61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291, 409, 270, 269, 267, 0, 37, 39, 40, 185,
    263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466,
    33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246,
    276, 283, 282, 295, 285, 336, 296, 334, 293, 300,
    46, 53, 52, 65, 55, 107, 66, 105, 63, 70};
constexpr int kOutlineFirst[6] = {0, 36, 56, 72, 88, 98}, kOutlineCount[6] = {36, 20, 16, 16, 10, 10};
using Point = std::array<double, 2>;
// The reference points of one face (scripts/beauty/facemesh-reference.py), picture pixels.
bool loadMesh(const std::string& stem, int index, std::vector<Point>& points) {
    std::ifstream in(gRefs / "mesh" / (stem + "-" + std::to_string(index)) / "landmarks.txt");
    points.clear();
    Point p;
    while (in >> p[0] >> p[1]) points.push_back(p);
    return points.size() == 468;
}
std::vector<Point> outline(const std::vector<Point>& points, int part) {
    std::vector<Point> out;
    for (int k = 0; k < kOutlineCount[part]; ++k) out.push_back(points[size_t(kOutline[kOutlineFirst[part] + k])]);
    return out;
}
// Signed distance (negative inside) from a pixel centre to a closed outline.
double outlineDistance(const std::vector<Point>& poly, double px, double py) {
    double best = 1e30; bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[i]; const auto& b = poly[j];
        const double ex = b[0] - a[0], ey = b[1] - a[1], wx = px - a[0], wy = py - a[1];
        const double s = std::clamp((wx * ex + wy * ey) / std::max(ex * ex + ey * ey, 1e-12), 0.0, 1.0);
        best = std::min(best, std::hypot(wx - ex * s, wy - ey * s));
        if ((a[1] > py) != (b[1] > py) && px < ex * (py - a[1]) / (b[1] - a[1]) + a[0]) inside = !inside;
    }
    return inside ? -best : best;
}
double outlineWidth(const std::vector<Point>& poly) {
    double lo = 1e30, hi = -1e30;
    for (const auto& p : poly) { lo = std::min(lo, p[0]); hi = std::max(hi, p[0]); }
    return hi - lo;
}

// The graph's outlines against the reference points: the network in the real
// pipeline (crop from the tracked box, decode, outline gathering).
int meshLandmarksCase() {
    double worstMean = 0, worstMax = 0;
    int pictures = 0;
    for (const char* stem : {"lena", "astronaut", "gta-f040", "gta-f052"}) {
        Picture p;
        std::vector<Point> reference;
        if (!loadPicture(stem, p) || !loadMesh(stem, 0, reference)) { check(false, std::string("mesh-landmarks: references for ") + stem); continue; }
        AVFrame* frame = rgbaFrame(p.image);
        auto gpu = std::make_unique<Gpu>();
        sink::RgbaImage out;
        std::vector<float> shapes;
        const bool ok = frame && gpu->start(withBeauty(restoreNode()), p.image.width, p.image.height, false) && gpu->render(frame, true, out) &&
                        gpu->readShapes(shapes);
        av_frame_free(&frame);
        check(ok && shapes[0] > 0.5f, std::string("mesh-landmarks: ") + stem + " has a usable mesh");
        if (!ok || shapes[0] <= 0.5f) continue;
        const double width = outlineWidth(outline(reference, 0));
        double sum = 0, worst = 0;
        for (int k = 0; k < 108; ++k) {
            const double x = shapes[size_t(8 + k) * 2] * p.image.width, y = shapes[size_t(8 + k) * 2 + 1] * p.image.height;
            const auto& r = reference[size_t(kOutline[k])];
            const double e = std::hypot(x - r[0], y - r[1]) / width;
            sum += e; worst = std::max(worst, e);
        }
        std::printf("METRIC case=mesh-landmarks picture=%s faceWidthPx=%.1f meshScore=%.3f meanError=%.4f maxError=%.4f (face widths)\n", stem,
                    width, shapes[1], sum / 108, worst);
        worstMean = std::max(worstMean, sum / 108); worstMax = std::max(worstMax, worst);
        ++pictures;
        if (!gpu->debug) continue;
        check(gpu->debugErrors() == 0, std::string("mesh-landmarks: D3D12 debug layer for ") + stem);
    }
    check(pictures == 4, "mesh-landmarks: every picture ran");
    check(worstMean < 0.025, "mesh-landmarks: outline points within 2.5% of the face width on average");
    check(worstMax < 0.08, "mesh-landmarks: every outline point within 8% of the face width");
    return failures ? 1 : 0;
}

// Full face restore under a grade follows the face contour: the original inside it,
// the graded picture outside it - also where the old oval reached (hair, hat brim).
int meshContourCase() {
    Picture p;
    std::vector<Point> reference;
    check(loadPicture("lena", p) && loadMesh("lena", 0, reference), "mesh-contour: picture and reference points");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage original, grade, restored;
    const BeautySettings node = restoreNode(1.0f);
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, original) &&
              gpu->start(graded({}), p.image.width, p.image.height, false) && gpu->render(frame, true, grade) &&
              gpu->start(graded(withBeauty(node)), p.image.width, p.image.height, false) && gpu->render(frame, true, restored);
    av_frame_free(&frame);
    check(ok, "mesh-contour: ungraded, graded and graded + restore graphs run");
    if (!ok) return finish(*gpu, "mesh-contour");
    const auto contour = outline(reference, 0);
    const Face& f = p.faces[0];
    const double width = f.w;      // the node's unit: the detector's box width
    const double grow = node.regionExpand * width, soft = std::max(node.feather * width, 1.5);
    const double tolerance = 0.03 * width + 2.0;   // the graph's points against the reference ones
    const auto inside = compare(restored, original, [&](unsigned x, unsigned y) {
        return outlineDistance(contour, x + 0.5, y + 0.5) < grow - soft / 2 - tolerance;
    });
    const auto outside = compare(restored, grade, [&](unsigned x, unsigned y) {
        return outlineDistance(contour, x + 0.5, y + 0.5) > grow + soft / 2 + tolerance;
    });
    // Where the oval from the box (Beauty.hlsli BeautyFaceOval, the mask before the mesh) restored
    // at least half way, but the contour does not reach.
    const double ex = f.lm[2] - f.lm[0], ey = f.lm[3] - f.lm[1], el = std::max(std::hypot(ex, ey), 1e-9);
    const double rc = (ex < 0 ? -ex : ex) / el, rs = (ex < 0 ? -ey : ey) / el;
    const double ocx = f.x + f.w / 2 + rs * 0.06 * f.h, ocy = f.y + f.h / 2 - rc * 0.06 * f.h;
    const double ax = f.w / 2 * (1.0 + node.regionExpand), ay = f.h / 2 * 1.12 * (1.0 + node.regionExpand);
    const auto oldOval = [&](unsigned x, unsigned y) {
        const double dx = x + 0.5 - ocx, dy = y + 0.5 - ocy;
        const double u = dx * rc + dy * rs, v = -dx * rs + dy * rc;
        return std::hypot(u / ax, v / ay) < 1.0;
    };
    const auto between = compare(restored, grade, [&](unsigned x, unsigned y) {
        return oldOval(x, y) && outlineDistance(contour, x + 0.5, y + 0.5) > grow + soft / 2 + tolerance;
    });
    std::printf("METRIC case=mesh-contour insidePixels=%zu insideDiffering=%zu outsidePixels=%zu outsideDiffering=%zu ovalOnlyPixels=%zu ovalOnlyDiffering=%zu\n",
                inside.pixels, inside.differing, outside.pixels, outside.differing, between.pixels, between.differing);
    check(inside.pixels > 5000 && inside.differing == 0, "mesh-contour: inside the face contour the picture is the ungraded original");
    check(outside.pixels > 5000 && outside.differing == 0, "mesh-contour: outside the contour the graded picture is untouched");
    check(between.pixels > 500 && between.differing == 0, "mesh-contour: hair and background the old oval covered keep the grade");
    save("mesh-contour-restored", restored);
    return finish(*gpu, "mesh-contour");
}

// Smoothing leaves the eyes, brows and lips exactly as they were and still smooths the skin.
int meshFeaturesCase() {
    Picture p;
    std::vector<Point> reference;
    check(loadPicture("lena", p) && loadMesh("lena", 0, reference), "mesh-features: picture and reference points");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage plain, smoothed;
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, plain) &&
              gpu->start(withBeauty(strongNode()), p.image.width, p.image.height, false) && gpu->render(frame, true, smoothed);
    av_frame_free(&frame);
    check(ok, "mesh-features: plain and smoothing graphs run");
    if (!ok) return finish(*gpu, "mesh-features");
    // The graph's points are within 1% of the face width of the reference ones (mesh-landmarks).
    const double width = p.faces[0].w, tolerance = 0.012 * width + 1.0;
    std::vector<std::vector<Point>> features;
    for (int part = 1; part < 6; ++part) features.push_back(outline(reference, part));
    const auto inFeature = [&](unsigned x, unsigned y) {
        for (const auto& poly : features) if (outlineDistance(poly, x + 0.5, y + 0.5) < -tolerance) return true;
        return false;
    };
    const auto kept = compare(smoothed, plain, inFeature);
    const auto contour = outline(reference, 0);
    const auto skin = [&](unsigned x, unsigned y) {
        if (outlineDistance(contour, x + 0.5, y + 0.5) > -0.08 * width) return false;
        for (const auto& poly : features) if (outlineDistance(poly, x + 0.5, y + 0.5) < 0.06 * width) return false;
        return true;
    };
    const double before = texture(plain, skin), after = texture(smoothed, skin);
    std::printf("METRIC case=mesh-features featurePixels=%zu featureDiffering=%zu skinTexture=%.3f->%.3f (%.1f%%)\n", kept.pixels, kept.differing,
                before, after, 100.0 * (after / std::max(before, 1e-9) - 1.0));
    check(kept.pixels > 400 && kept.differing == 0, "mesh-features: eyes, brows and lips are not smoothed at all");
    check(after < before * 0.92, "mesh-features: the skin between them is still smoothed");
    save("mesh-features-smoothed", smoothed);
    return finish(*gpu, "mesh-features");
}

int noModelCase() {
    Picture p;
    check(loadPicture("lena", p), "no-model: picture");
    AVFrame* frame = rgbaFrame(p.image);
    auto gpu = std::make_unique<Gpu>();
    sink::RgbaImage plain, out;
    const std::wstring missing = (gOut / L"no-such-model.onnx").wstring();
    bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, plain) &&
              gpu->start(withBeauty(restoreNode()), p.image.width, p.image.height, false, true, nullptr, missing) && gpu->render(frame, true, out);
    check(ok && !gpu->graph->beautyDetectorReady() && !gpu->graph->beautyNotice().empty(), "no-model: preview runs and says why beauty is off");
    const auto d = compare(plain, out, [](unsigned, unsigned) { return true; });
    check(ok && d.differing == 0, "no-model: without faces the picture is unchanged");
    const bool exported = gpu->start(withBeauty(restoreNode()), p.image.width, p.image.height, false, false, nullptr, missing, true);
    check(!exported, "no-model: an export with beauty refuses to start without the detector");
    av_frame_free(&frame);
    return finish(*gpu, "no-model");
}

int timingCase(const std::string& name, unsigned width, unsigned height) {
    Picture crowd, portrait;
    check(loadPicture("largest_selfie", crowd) && loadPicture("lena", portrait), name + ": pictures");
    // A crowd (many small faces) and a close-up (one face across half the frame height).
    const auto crowdImage = resized(crowd.image, width, height);
    sink::RgbaImage close; close.width = width; close.height = height; close.pixels.assign(size_t(width) * height * 4, 96);
    const unsigned side = height;   // lena 512 -> the full height
    const auto big = resized(portrait.image, side, side);
    for (unsigned y = 0; y < side; ++y)
        std::memcpy(close.pixels.data() + (size_t(y) * width + (width - side) / 2) * 4, big.pixels.data() + size_t(y) * side * 4, size_t(side) * 4);
    sink::RgbaImage empty; empty.width = width; empty.height = height; empty.pixels.assign(size_t(width) * height * 4, 96);
    for (size_t i = 3; i < empty.pixels.size(); i += 4) { empty.pixels[i] = 255; close.pixels[i] = 255; }
    struct Run { const char* label; const sink::RgbaImage* image; BeautySettings node; };
    BeautySettings both = smoothNode(0.6f, 4.0f); both.restore = true;
    const Run runs[] = {{"close-restore", &close, restoreNode()}, {"close-smooth", &close, smoothNode()}, {"close-both", &close, both},
                        {"crowd-both", &crowdImage, both}, {"no-face-both", &empty, both}};
    for (const auto& run : runs) {
        auto gpu = std::make_unique<Gpu>();
        AVFrame* frame = rgbaFrame(*run.image);
        bool ok = frame && gpu->start(withBeauty(run.node), width, height, false, false);
        std::vector<double> node, detect, detectAll;
        sink::RgbaImage out;
        for (int i = 0; ok && i < 64; ++i) {
            ok = gpu->render(frame, i == 0, out);
            if (i < 8) continue;
            if (const auto ms = gpu->stageMs(diagnostics::beautyNodeStage(0))) node.push_back(*ms);
            if (const auto ms = gpu->stageMs(diagnostics::GpuStage::BeautyDetect)) {
                detectAll.push_back(*ms);
                if (*ms > 0.05) detect.push_back(*ms);
            }
        }
        av_frame_free(&frame);
        const auto median = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        const auto p95 = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, v.size() * 95 / 100)]; };
        const auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return v.empty() ? 0.0 : s / double(v.size()); };
        std::printf("TIMING extent=%ux%u content=%s nodeMedianMs=%.3f nodeP95Ms=%.3f detectMeanMs=%.3f detectFrameMedianMs=%.3f detectFrameP95Ms=%.3f samples=%zu detectFrames=%zu faces=%u\n",
                    width, height, run.label, median(node), p95(node), mean(detectAll), median(detect), p95(detect), node.size(), detect.size(),
                    gpu->graph->beautyFaceCount());
        check(ok && node.size() > 40, name + " " + run.label + ": measured");
    }
    return failures ? 1 : 0;
}

// Tuning aid, not a check: VEYRA_BEAUTY_TUNE="tag;stems;amount;radius;detail;texture;rows" runs
// the smoothing on each picture (rows 0 = native size) and writes the largest face
// before | after as <tag>-<stem>.png, with the skin / eye texture change.
int tuneCase() {
    const char* spec = std::getenv("VEYRA_BEAUTY_TUNE");
    if (!spec) { std::printf("VEYRA_BEAUTY_TUNE not set\n"); return 2; }
    std::vector<std::string> f;
    { std::stringstream s(spec); std::string item; while (std::getline(s, item, ';')) f.push_back(item); }
    if (f.size() != 7) { std::printf("bad VEYRA_BEAUTY_TUNE\n"); return 2; }
    BeautySettings b = smoothNode(std::stof(f[2]), std::stof(f[3]));
    b.detail = std::stof(f[4]); b.texture = std::stof(f[5]);
    const unsigned rows = unsigned(std::stoul(f[6]));
    std::stringstream stems(f[1]); std::string stem;
    while (std::getline(stems, stem, ',')) {
        Picture loaded;
        if (!loadPicture(stem, loaded)) { check(false, "tune: picture " + stem); continue; }
        const Picture p = rows ? enlarged(loaded, rows) : loaded;
        AVFrame* frame = rgbaFrame(p.image);
        auto gpu = std::make_unique<Gpu>();
        sink::RgbaImage off, on;
        const bool ok = frame && gpu->start(engine::EnhancementSettings{}, p.image.width, p.image.height, false) && gpu->render(frame, true, off) &&
                        gpu->start(withBeauty(b), p.image.width, p.image.height, false) && gpu->render(frame, true, on);
        av_frame_free(&frame);
        if (!ok) { check(false, "tune: graph " + stem); continue; }
        const auto nearEye = [&](unsigned x, unsigned y) {
            for (const auto& face : p.faces)
                for (int e = 0; e < 2; ++e)
                    if (std::hypot(x + 0.5 - face.lm[e * 2], y + 0.5 - face.lm[e * 2 + 1]) < face.w * 0.09) return true;
            return false;
        };
        const auto skin = [&](unsigned x, unsigned y) { return inCore(p, x, y) && !nearEye(x, y); };
        const double s0 = texture(off, skin), s1 = texture(on, skin), e0 = texture(off, nearEye), e1 = texture(on, nearEye);
        const auto changed = compare(on, off, [&](unsigned x, unsigned y) { return inCore(p, x, y); });
        std::printf("TUNE tag=%s stem=%s size=%ux%u skin=%.3f->%.3f (%.1f%%) eyes=%.3f->%.3f (%.1f%%) coreMaxDiff=%d\n", f[0].c_str(), stem.c_str(),
                    p.image.width, p.image.height, s0, s1, 100.0 * (s1 / std::max(s0, 1e-9) - 1.0), e0, e1, 100.0 * (e1 / std::max(e0, 1e-9) - 1.0), changed.maxDiff);
        // Largest face, 1.4x its box, before | after.
        const auto& face = *std::max_element(p.faces.begin(), p.faces.end(), [](const Face& a, const Face& c) { return a.h < c.h; });
        const int side = int(std::max(face.w, face.h) * 1.4);
        const int x0 = std::clamp(int(face.x + face.w / 2) - side / 2, 0, std::max(0, int(p.image.width) - side));
        const int y0 = std::clamp(int(face.y + face.h / 2) - side / 2, 0, std::max(0, int(p.image.height) - side));
        const int w = std::min(side, int(p.image.width)), h = std::min(side, int(p.image.height));
        sink::RgbaImage pair; pair.width = uint32_t(w * 2 + 4); pair.height = uint32_t(h); pair.pixels.assign(size_t(pair.width) * pair.height * 4, 255);
        for (int y = 0; y < h; ++y) {
            std::memcpy(pair.pixels.data() + (size_t(y) * pair.width) * 4, off.pixels.data() + (size_t(y0 + y) * off.width + x0) * 4, size_t(w) * 4);
            std::memcpy(pair.pixels.data() + (size_t(y) * pair.width + w + 4) * 4, on.pixels.data() + (size_t(y0 + y) * on.width + x0) * 4, size_t(w) * 4);
        }
        save(f[0] + "-" + stem, pair);
    }
    return failures ? 1 : 0;
}

struct Case { std::string name; std::function<int()> run; };
std::vector<Case> cases() {
    std::vector<Case> c;
    c.push_back({"off-identity", [] { return offIdentityCase(); }});
    c.push_back({"restore-grade", [] { return restoreCase("restore-grade", 1.0f); }});
    c.push_back({"restore-half", [] { return restoreCase("restore-half", 0.5f); }});
    c.push_back({"smooth-lena", [] { return smoothCase("smooth-lena", "lena", strongNode()); }});
    c.push_back({"smooth-astronaut", [] { return smoothCase("smooth-astronaut", "astronaut", strongNode()); }});
    c.push_back({"smooth-messi", [] { return smoothCase("smooth-messi", "messi5", strongNode()); }});
    c.push_back({"smooth-game-night", [] { return smoothCase("smooth-game-night", "gta-f040", strongNode()); }});
    c.push_back({"two-nodes", [] { return twoNodeCase(); }});
    c.push_back({"hdr", [] { return hdrCase(); }});
    c.push_back({"tracking", [] { return trackingCase(); }});
    c.push_back({"tracking-motion", [] { return trackingMotionCase(); }});
    c.push_back({"mesh-landmarks", [] { return meshLandmarksCase(); }});
    c.push_back({"mesh-contour", [] { return meshContourCase(); }});
    c.push_back({"mesh-features", [] { return meshFeaturesCase(); }});
    c.push_back({"no-model", [] { return noModelCase(); }});
    c.push_back({"tune", [] { return tuneCase(); }});
    c.push_back({"timing-1080p", [] { return timingCase("timing-1080p", 1920, 1080); }});
    c.push_back({"timing-2160p", [] { return timingCase("timing-2160p", 3840, 2160); }});
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
    if (argc == 5 && std::wstring_view(argv[1]) == L"--case") {
        gRefs = argv[3]; gOut = argv[4];
        std::error_code ec; fs::create_directories(gOut, ec);
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;   // WIC picture loading
        const std::wstring wanted = argv[2];
        for (const auto& c : all)
            if (std::wstring(c.name.begin(), c.name.end()) == wanted) {
                const int result = c.run();
                std::printf(result ? "beauty gpu: %s FAILED\n" : "beauty gpu: %s passed\n", c.name.c_str());
                return result;
            }
        std::cout << "unknown case" << std::endl;
        return 2;
    }
    std::cout << "usage: veyra_beauty_gpu_tests --list | --case <name> <reference-dir> <output-dir>" << std::endl;
    return 2;
}
