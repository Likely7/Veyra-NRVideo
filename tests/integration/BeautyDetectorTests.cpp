// GPU YuNet face detector (beauty node) against OpenCV's reference outputs from
// scripts/beauty/yunet-reference.py, one folder per picture and detector size:
//
//   veyra_beauty_detector_tests.exe <model.onnx> <reference-dir> [--timing]
//
// network : the exact OpenCV input blob goes into the detector's input buffer; the
//           merged head tensors (cls/obj before the sigmoid, box, landmarks) must
//           match OpenCV's twelve raw outputs, and the decoded + NMS faces must
//           match FaceDetectorYN.
// prep    : the same picture as a linear RGBA16F texture through the GPU prep
//           (sRGB encode, BGR 0..255): input buffer vs. the blob, faces vs. reference.
// resample: the full-size original through the prep's area average down to the
//           detector size: faces must overlap the reference.
// timing  : (--timing) GPU milliseconds per layer kind at 320 / 512 / 640 wide.
// VEYRA_BEAUTY_GPU_DEBUG=1 enables the D3D12 debug layer and fails on its errors.
#include "veyra/beauty/FaceMeshGpu.h"
#include "veyra/beauty/YuNetGpu.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/sink/ImageExportSink.h"
#include <DirectXPackedVector.h>
#include <d3d12sdklayers.h>
#include <objbase.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace veyra;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

int failures = 0;
void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

std::vector<float> readFloats(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<float> v(bytes.size() / 4);
    if (!v.empty()) std::memcpy(v.data(), bytes.data(), v.size() * 4);
    return v;
}

struct Reference {
    fs::path dir;
    std::string name, stem;
    uint32_t w = 0, h = 0, pw = 0, ph = 0;
    std::vector<float> blob;                                  // NCHW, BGR 0..255
    std::map<std::string, std::vector<float>> outputs;
    std::vector<beauty::FaceBox> faces;
};

bool loadReference(const fs::path& dir, Reference& r) {
    r.dir = dir; r.name = dir.filename().string();
    r.stem = r.name.substr(0, r.name.rfind('-'));
    std::ifstream meta(dir / "meta.txt");
    if (!(meta >> r.w >> r.h >> r.pw >> r.ph)) return false;
    r.blob = readFloats(dir / "blob.bin");
    if (r.blob.size() != size_t(3) * r.pw * r.ph) return false;
    for (const char* kind : {"cls", "obj", "bbox", "kps"})
        for (int stride : {8, 16, 32}) {
            const std::string key = std::string(kind) + "_" + std::to_string(stride);
            r.outputs[key] = readFloats(dir / (key + ".bin"));
        }
    std::ifstream faces(dir / "faces.txt");
    std::string line;
    while (std::getline(faces, line)) {
        std::istringstream row(line);
        std::vector<float> v; float x;
        while (row >> x) v.push_back(x);
        if (v.size() != 15) continue;
        beauty::FaceBox f;
        f.x = v[0]; f.y = v[1]; f.w = v[2]; f.h = v[3];
        for (int k = 0; k < 10; ++k) f.landmarks[size_t(k)] = v[4 + size_t(k)];
        f.score = v[14];
        r.faces.push_back(f);
    }
    return true;
}

float iou(const beauty::FaceBox& a, const beauty::FaceBox& b) {
    const float x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w), y2 = std::min(a.y + a.h, b.y + b.h);
    const float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0 ? inter / uni : 0.0f;
}

struct Match { size_t reference = 0, found = 0, matched = 0; float minIou = 1, maxScore = 0, maxLandmark = 0; };
Match match(const std::vector<beauty::FaceBox>& reference, const std::vector<beauty::FaceBox>& found, float minimumIou) {
    Match m; m.reference = reference.size(); m.found = found.size();
    std::vector<bool> used(found.size(), false);
    for (const auto& r : reference) {
        int best = -1; float bestIou = 0;
        for (size_t i = 0; i < found.size(); ++i)
            if (!used[i] && iou(r, found[i]) > bestIou) { bestIou = iou(r, found[i]); best = int(i); }
        if (best < 0 || bestIou < minimumIou) continue;
        used[size_t(best)] = true; ++m.matched;
        const auto& f = found[size_t(best)];
        m.minIou = std::min(m.minIou, bestIou);
        m.maxScore = std::max(m.maxScore, std::abs(f.score - r.score));
        for (int k = 0; k < 10; ++k) m.maxLandmark = std::max(m.maxLandmark, std::abs(f.landmarks[size_t(k)] - r.landmarks[size_t(k)]));
    }
    return m;
}

struct Gpu {
    gfx::D3D12DeviceContext ctx;
    gfx::CommandSlotRing ring;
    bool debug = std::getenv("VEYRA_BEAUTY_GPU_DEBUG") != nullptr;
    bool start() {
        gfx::DeviceContextDesc device; Status st;
        device.enableDebugLayer = debug;
        return ctx.initialize(device, st) && ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 2, st);
    }
    ID3D12GraphicsCommandList* begin() { Status st; return ring.acquire(0, st); }
    bool finish() { return ring.submitAndSignal(0) && ring.waitIdle(); }
    ComPtr<ID3D12Resource> buffer(uint64_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1;
        d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r;
        if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
        return r;
    }
    static void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
        list->ResourceBarrier(1, &b);
    }
    // 8-bit sRGB picture -> linear RGBA16F texture in the non-pixel SRV state.
    ComPtr<ID3D12Resource> texture(const sink::RgbaImage& image) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = image.width; d.Height = image.height;
        d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count = 1;
        ComPtr<ID3D12Resource> tex;
        if (FAILED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) return nullptr;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes = 0;
        ctx.device()->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        auto upload = buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        uint8_t* mapped = nullptr;
        if (!upload || FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return nullptr;
        float lut[256];
        for (int i = 0; i < 256; ++i) lut[i] = float(srgbToLinear(i / 255.0));
        for (uint32_t y = 0; y < image.height; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(mapped + fp.Offset + size_t(y) * fp.Footprint.RowPitch);
            for (uint32_t x = 0; x < image.width; ++x) {
                const uint8_t* p = image.pixels.data() + (size_t(y) * image.width + x) * 4;
                for (int c = 0; c < 3; ++c) row[x * 4 + c] = DirectX::PackedVector::XMConvertFloatToHalf(lut[p[c]]);
                row[x * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
            }
        }
        upload->Unmap(0, nullptr);
        auto* list = begin();
        if (!list) return nullptr;
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
        dst.pResource = tex.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(list, tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return finish() ? tex : nullptr;
    }
    bool write(ID3D12Resource* target, const void* data, uint64_t bytes) {
        auto upload = buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped = nullptr;
        if (!upload || FAILED(upload->Map(0, nullptr, &mapped))) return false;
        std::memcpy(mapped, data, size_t(bytes));
        upload->Unmap(0, nullptr);
        auto* list = begin();
        if (!list) return false;
        barrier(list, target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(target, 0, upload.Get(), 0, bytes);
        barrier(list, target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return finish();
    }
    bool read(ID3D12Resource* source, uint64_t bytes, std::vector<float>& out) {
        auto readback = buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto* list = readback ? begin() : nullptr;
        if (!list) return false;
        barrier(list, source, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback.Get(), 0, source, 0, bytes);
        barrier(list, source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!finish()) return false;
        void* mapped = nullptr; D3D12_RANGE range{0, SIZE_T(bytes)};
        if (FAILED(readback->Map(0, &range, &mapped))) return false;
        out.resize(size_t(bytes / 4));
        std::memcpy(out.data(), mapped, size_t(bytes));
        D3D12_RANGE none{}; readback->Unmap(0, &none);
        return true;
    }
    unsigned debugErrors() {
        if (!debug) return 0;
        ComPtr<ID3D12InfoQueue> info;
        if (FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)))) return 1;
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
    ~Gpu() { ring.drainQueue(); ring.shutdown(); }
};

// Head tensors vs. OpenCV's raw outputs (cls/obj compared after the sigmoid).
struct HeadError { float cls = 0, obj = 0, box = 0, kps = 0; };
bool compareHeads(Gpu& gpu, beauty::YuNetGpu& det, const Reference& r, HeadError& e) {
    for (uint32_t s = 0; s < 3; ++s) {
        const uint32_t gw = r.pw >> (3 + s), gh = r.ph >> (3 + s), stride = 8u << s;
        std::vector<float> head;
        if (!gpu.read(det.headBuffer(s), uint64_t(gw) * gh * 64, head)) return false;
        const auto& cls = r.outputs.at("cls_" + std::to_string(stride));
        const auto& obj = r.outputs.at("obj_" + std::to_string(stride));
        const auto& box = r.outputs.at("bbox_" + std::to_string(stride));
        const auto& kps = r.outputs.at("kps_" + std::to_string(stride));
        const size_t anchors = size_t(gw) * gh;
        if (cls.size() != anchors || box.size() != anchors * 4 || kps.size() != anchors * 10) return false;
        for (size_t a = 0; a < anchors; ++a) {
            const float* v = head.data() + a * 16;
            e.cls = std::max(e.cls, std::abs(sigmoid(v[0]) - cls[a]));
            e.obj = std::max(e.obj, std::abs(sigmoid(v[1]) - obj[a]));
            for (int k = 0; k < 4; ++k) e.box = std::max(e.box, std::abs(v[2 + k] - box[a * 4 + size_t(k)]));
            for (int k = 0; k < 10; ++k) e.kps = std::max(e.kps, std::abs(v[6 + k] - kps[a * 10 + size_t(k)]));
        }
    }
    return true;
}

void report(const char* stage, const Reference& r, const Match& m) {
    std::printf("%s %s faces ref=%zu gpu=%zu matched=%zu minIoU=%.4f maxScoreDiff=%.4f maxLandmarkPx=%.3f\n", stage, r.name.c_str(),
                m.reference, m.found, m.matched, m.minIou, m.maxScore, m.maxLandmark);
}

void runCase(Gpu& gpu, beauty::YuNetGpu& det, const Reference& r, const fs::path& originals) {
    // 1. Network on the exact OpenCV blob.
    std::vector<float> nhwc(size_t(r.pw) * r.ph * 4, 0.0f);
    for (uint32_t y = 0; y < r.ph; ++y)
        for (uint32_t x = 0; x < r.pw; ++x)
            for (uint32_t c = 0; c < 3; ++c)
                nhwc[(size_t(y) * r.pw + x) * 4 + c] = r.blob[(size_t(c) * r.ph + y) * r.pw + x];
    bool ok = gpu.write(det.inputBuffer(), nhwc.data(), nhwc.size() * 4);
    auto* list = ok ? gpu.begin() : nullptr;
    ok = list && det.recordNetwork(list, 0, r.w, r.h) && gpu.finish();
    HeadError e;
    ok = ok && compareHeads(gpu, det, r, e);
    std::vector<beauty::FaceBox> faces;
    uint32_t raw = 0;
    ok = ok && det.collect(0, faces, &raw);
    std::printf("NETWORK %s maxErr cls=%.2e obj=%.2e box=%.2e kps=%.2e candidates=%u\n", r.name.c_str(), e.cls, e.obj, e.box, e.kps, raw);
    check(ok && e.cls < 2e-4f && e.obj < 2e-4f && e.box < 2e-3f && e.kps < 2e-3f, "network heads " + r.name);
    Match m = match(r.faces, faces, 0.98f);
    report("NETWORK", r, m);
    check(ok && m.matched == r.faces.size() && faces.size() == r.faces.size() && m.maxScore < 2e-3f && m.maxLandmark < 0.1f, "network faces " + r.name);

    // 2. Prep from a linear texture of the same picture.
    sink::RgbaImage picture;
    ok = sink::loadImage((r.dir / "input.png").wstring(), picture) && picture.width == r.w && picture.height == r.h;
    auto tex = ok ? gpu.texture(picture) : nullptr;
    beauty::YuNetGpu::Source source;
    source.texture = tex.Get(); source.width = r.w; source.height = r.h;
    list = tex ? gpu.begin() : nullptr;
    ok = list && det.record(list, 0, source, r.w, r.h) && gpu.finish();
    std::vector<float> input;
    float inputErr = 0;
    ok = ok && gpu.read(det.inputBuffer(), uint64_t(r.pw) * r.ph * 16, input);
    if (ok)
        for (size_t i = 0; i < nhwc.size(); ++i) inputErr = std::max(inputErr, std::abs(input[i] - nhwc[i]));
    faces.clear();
    ok = ok && det.collect(0, faces);
    m = match(r.faces, faces, 0.95f);
    std::printf("PREP %s maxInputErr=%.3f (8-bit levels)\n", r.name.c_str(), inputErr);
    report("PREP", r, m);
    check(ok && inputErr < 0.5f, "prep input " + r.name);
    check(ok && m.matched == r.faces.size() && faces.size() == r.faces.size() && m.maxScore < 0.01f && m.maxLandmark < 0.5f, "prep faces " + r.name);

    // 3. Full-size original, area-averaged down by the prep.
    fs::path original;
    for (const auto& f : fs::directory_iterator(originals))
        if (f.path().stem().string() == r.stem) original = f.path();
    sink::RgbaImage full;
    ok = !original.empty() && sink::loadImage(original.wstring(), full);
    tex = ok ? gpu.texture(full) : nullptr;
    source = {};
    source.texture = tex.Get(); source.width = full.width; source.height = full.height;
    list = tex ? gpu.begin() : nullptr;
    ok = list && det.record(list, 0, source, r.w, r.h) && gpu.finish();
    faces.clear();
    ok = ok && det.collect(0, faces);
    m = match(r.faces, faces, 0.5f);
    report("RESAMPLE", r, m);
    // Few-face pictures: every face, tight boxes. Crowds: borderline faces near
    // the threshold may swap; most must still be found.
    const bool crowd = r.faces.size() > 4;
    check(ok && (crowd ? m.matched * 10 >= r.faces.size() * 8 : (m.matched == r.faces.size() && faces.size() == r.faces.size() && m.minIou > 0.8f)),
          "resample faces " + r.name);
}

void timing(Gpu& gpu, beauty::YuNetGpu& det, const fs::path& originals) {
    fs::path picture;
    for (const auto& f : fs::directory_iterator(originals))
        if (f.path().stem().string() == "largest_selfie") picture = f.path();
    sink::RgbaImage full;
    if (picture.empty() || !sink::loadImage(picture.wstring(), full)) { check(false, "timing picture"); return; }
    auto tex = gpu.texture(full);
    const uint32_t layers = det.layerCount();
    D3D12_QUERY_HEAP_DESC qd{}; qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = layers + 1;
    ComPtr<ID3D12QueryHeap> heap;
    if (!tex || FAILED(gpu.ctx.device()->CreateQueryHeap(&qd, IID_PPV_ARGS(&heap)))) { check(false, "timing resources"); return; }
    auto readback = gpu.buffer(uint64_t(layers + 1) * 8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency = 0; gpu.ctx.directQueue()->GetTimestampFrequency(&frequency);
    det.setProfiler(heap.Get(), 0);
    for (const uint32_t longest : {320u, 512u, 640u}) {
        const uint32_t w = longest, h = uint32_t(std::lround(double(full.height) * longest / full.width));
        std::map<std::string, std::vector<double>> kinds;
        std::vector<double> totals;
        for (int run = 0; run < 40; ++run) {
            beauty::YuNetGpu::Source source;
            source.texture = tex.Get(); source.width = full.width; source.height = full.height;
            auto* list = gpu.begin();
            if (!list || !det.record(list, 0, source, w, h)) { check(false, "timing record"); return; }
            list->ResolveQueryData(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, layers + 1, readback.Get(), 0);
            if (!gpu.finish()) { check(false, "timing submit"); return; }
            uint64_t* t = nullptr; D3D12_RANGE range{0, SIZE_T(layers + 1) * 8};
            if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&t)))) return;
            std::map<std::string, double> sums;
            for (uint32_t i = 0; i < layers; ++i) {
                std::string kind = det.layerName(i);
                if (kind.rfind("head", 0) == 0) kind = "head";
                else if (kind.rfind("decode", 0) == 0) kind = "decode";
                else if (kind != "prep") kind = "backbone";
                sums[kind] += double(t[i + 1] - t[i]) * 1000.0 / double(frequency);
            }
            totals.push_back(double(t[layers] - t[0]) * 1000.0 / double(frequency));
            D3D12_RANGE none{}; readback->Unmap(0, &none);
            if (run >= 5) for (const auto& [k, v] : sums) kinds[k].push_back(v);
            if (run < 5) totals.pop_back();
        }
        const auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        std::printf("TIMING detector=%ux%u (padded %ux%u) MAC=%.1fM medianMs=%.3f", w, h, det.paddedExtent(w), det.paddedExtent(h),
                    double(det.multiplyAccumulates(w, h)) / 1e6, median(totals));
        for (const auto& [k, v] : kinds) std::printf(" %s=%.3f", k.c_str(), median(v));
        std::printf("\n");
    }
    det.setProfiler(nullptr, 0);
}
} // namespace

// Face mesh network against scripts/beauty/facemesh-reference.py: the same 192 x 192
// crops in, the 1404 landmark values and the face flag out, for every reference face.
void meshCases(Gpu& gpu, const fs::path& dir) {
    const auto modelPath = beauty::locateFaceMeshModel();
    check(!modelPath.empty(), "mesh: model next to the test (models/face_landmark.tflite)");
    if (modelPath.empty()) return;
    // The buffers the mesh binds; the network test reads neither.
    auto faces = gpu.buffer(64 * 9, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    ComPtr<ID3D12Resource> shapes;    // written by the mesh: unordered access
    {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = beauty::FaceMeshGpu::kShapeBytes; d.Height = 1;
        d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(gpu.ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&shapes))))
            shapes.Reset();
    }
    beauty::FaceMeshGpu mesh;
    std::string error;
    const bool ready = faces && shapes && mesh.initialize(gpu.ctx.device(), modelPath, faces.Get(), shapes.Get(), 1, error);
    check(ready, "mesh: initialise " + error);
    if (!ready) return;
    std::printf("MESH %s\n", mesh.description().c_str());
    std::vector<fs::path> cases;
    for (const auto& f : fs::directory_iterator(dir))
        if (f.is_directory() && fs::exists(f.path() / "input.bin")) cases.push_back(f.path());
    std::sort(cases.begin(), cases.end());
    constexpr size_t kInput = 192 * 192 * 3, kOutput = 1404 + 1;
    double worstPoint = 0, worstFlag = 0;
    size_t compared = 0;
    for (size_t first = 0; first < cases.size(); first += beauty::FaceMeshGpu::kMaxFaces) {
        const size_t n = std::min<size_t>(beauty::FaceMeshGpu::kMaxFaces, cases.size() - first);
        std::vector<float> input, expected, output;
        for (size_t k = 0; k < n; ++k) {
            const auto in = readFloats(cases[first + k] / "input.bin"), out = readFloats(cases[first + k] / "output.bin");
            if (in.size() != kInput || out.size() != kOutput) { check(false, "mesh: reference " + cases[first + k].filename().string()); return; }
            input.insert(input.end(), in.begin(), in.end());
            expected.insert(expected.end(), out.begin(), out.end());
        }
        if (!mesh.runNetwork(gpu.ctx.directQueue(), input, uint32_t(n), output, error)) { check(false, "mesh: run " + error); return; }
        for (size_t k = 0; k < n; ++k) {
            for (size_t v = 0; v < 1404; ++v)
                worstPoint = std::max(worstPoint, double(std::abs(output[k * kOutput + v] - expected[k * kOutput + v])));
            worstFlag = std::max(worstFlag, double(std::abs(output[k * kOutput + 1404] - expected[k * kOutput + 1404])));
        }
        compared += n;
    }
    std::printf("METRIC mesh faces=%zu worstLandmarkErrorCropPx=%.5f worstFlagLogitError=%.5f\n", compared, worstPoint, worstFlag);
    check(compared >= 8, "mesh: reference faces found");
    check(worstPoint < 0.02, "mesh: every landmark within 0.02 crop pixels of the reference");
    check(worstFlag < 0.02, "mesh: face flag within 0.02 of the reference logit");
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: veyra_beauty_detector_tests <model.onnx> <reference-dir> [--timing]\n"); return 2; }
    const fs::path model = argv[1], refs = argv[2];
    const bool withTiming = argc > 3 && std::wstring(argv[3]) == L"--timing";
    // sink::loadImage decodes through WIC.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) { std::printf("FAIL COM\n"); return 1; }
    Gpu gpu;
    if (!gpu.start()) { std::printf("FAIL device\n"); return 1; }
    beauty::YuNetGpu det;
    beauty::YuNetConfig config;
    config.ringSlots = 1;
    std::string error;
    const bool ready = det.initialize(gpu.ctx.device(), model, config, error);
    check(ready, "detector initialise " + error);
    if (!ready) return 1;
    std::printf("MODEL %s weights=%llu working=%llu\n", det.modelDescription().c_str(), (unsigned long long)det.weightBytes(),
                (unsigned long long)det.workingSetBytes());
    std::vector<fs::path> cases;
    for (const auto& f : fs::directory_iterator(refs))
        if (f.is_directory() && fs::exists(f.path() / "meta.txt")) cases.push_back(f.path());
    std::sort(cases.begin(), cases.end());
    for (const auto& dir : cases) {
        Reference r;
        if (!loadReference(dir, r)) { check(false, "reference " + dir.filename().string()); continue; }
        runCase(gpu, det, r, refs / "originals");
    }
    if (fs::exists(refs / "mesh")) meshCases(gpu, refs / "mesh");
    if (withTiming) timing(gpu, det, refs / "originals");
    const unsigned debugErrors = gpu.debugErrors();
    check(debugErrors == 0, "D3D12 debug layer");
    det.shutdown();
    std::printf("%s: %d failures\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
