#include "veyra/beauty/FaceTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"

namespace veyra::beauty {
namespace {
constexpr const wchar_t* kModelNames[] = {L"face_detection_yunet_2023mar.onnx", L"face_detection_yunet_2026may.onnx"};
constexpr UINT kTrackConstants = 16;
constexpr UINT kDescriptorsPerSlot = 3;     // t0 (unused), u0 faces, u1 candidates
constexpr UINT64 kFaceBytes = 64;

pipeline::ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                                             D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heap;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    pipeline::ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}
uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
} // namespace

std::filesystem::path locateFaceModel()
{
    wchar_t exe[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    const auto exeDirectory = length && length < std::size(exe) ? std::filesystem::path(exe).parent_path() : std::filesystem::current_path();
    const auto root = runtime::applicationRoot();
    const std::filesystem::path folders[] = {root / "runtime" / "models", root / "runtime_local" / "models", exeDirectory / "models"};
    std::error_code ec;
    for (const auto& folder : folders)
        for (const auto* name : kModelNames)
            if (std::filesystem::is_regular_file(folder / name, ec)) return folder / name;
    return {};
}

bool FaceTracker::initialize(ID3D12Device* device, const std::filesystem::path& model, uint32_t ringSlots, std::string& error)
{
    shutdown();
    if (!device || !ringSlots || model.empty()) { error = model.empty() ? "face model file not found" : "invalid face tracker configuration"; return false; }
    YuNetConfig config;
    config.ringSlots = ringSlots;
    config.readback = false;              // the tracking pass consumes the candidates on the GPU
    config.scoreThreshold = keepThreshold_;
    if (!detector_.initialize(device, model, config, error)) { shutdown(); return false; }
    std::vector<uint8_t> cs;
    if (!pipeline::loadShaderBytes("BeautyTrack.dxil", cs) ||
        !track_.create(device, cs, ringSlots * kDescriptorsPerSlot, 1, 2, kTrackConstants)) {
        error = "face tracking pass creation failed"; shutdown(); return false;
    }
    faces_ = makeBuffer(device, kFaceEntries * kFaceBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_COMMON);
    shapes_ = makeBuffer(device, FaceMeshGpu::kShapeBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COMMON);
    if (!faces_ || !shapes_) { error = "face buffer allocation failed"; shutdown(); return false; }
    D3D12_DESCRIPTOR_HEAP_DESC sd{};
    sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; sd.NumDescriptors = 1; sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&staging_)))) { error = "face tracker descriptor heap failed"; shutdown(); return false; }
    // SRVs go through a non-visible heap (DescriptorStager's driver workaround); UAVs are written directly.
    D3D12_SHADER_RESOURCE_VIEW_DESC none{};
    none.Format = DXGI_FORMAT_R32_TYPELESS;
    none.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    none.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    none.Buffer.NumElements = 1;
    none.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    device->CreateShaderResourceView(nullptr, &none, staging_->GetCPUDescriptorHandleForHeapStart());
    const auto at = [&](UINT index) { auto h = track_.heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(index) * track_.increment; return h; };
    for (uint32_t s = 0; s < ringSlots; ++s) {
        device->CopyDescriptorsSimple(1, at(s * kDescriptorsPerSlot), staging_->GetCPUDescriptorHandleForHeapStart(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC faces{};
        faces.Format = DXGI_FORMAT_UNKNOWN;
        faces.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        faces.Buffer.NumElements = kFaceEntries;
        faces.Buffer.StructureByteStride = UINT(kFaceBytes);
        device->CreateUnorderedAccessView(faces_.Get(), nullptr, &faces, at(s * kDescriptorsPerSlot + 1));
        D3D12_UNORDERED_ACCESS_VIEW_DESC raw{};
        raw.Format = DXGI_FORMAT_R32_TYPELESS;
        raw.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        raw.Buffer.NumElements = UINT(detector_.candidateBufferBytes() / 4);
        raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(detector_.candidateBuffer(s), nullptr, &raw, at(s * kDescriptorsPerSlot + 2));
        readbacks_.push_back(makeBuffer(device, kFaceBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST));
        if (!readbacks_.back()) { error = "face status readback allocation failed"; shutdown(); return false; }
    }
    pending_.assign(ringSlots, false);
    slots_ = ringSlots;
    ready_ = true;
    // The face mesh is optional: without it every face keeps the oval from its box.
    std::string meshError;
    meshReady_ = mesh_.initialize(device, locateFaceMeshModel(), faces_.Get(), shapes_.Get(), ringSlots, meshError);
    shapesClear_ = makeBuffer(device, FaceMeshGpu::kShapeBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    meshReady_ = meshReady_ && shapesClear_;
    if (!meshReady_) log::warn("beauty-mesh", "face mesh unavailable (" + meshError + "); faces keep the oval from their box");
    log::info("beauty-detect", std::format("face tracker ready: {}; model {}; slots {}", detector_.modelDescription(),
                                           model.filename().string(), ringSlots));
    return true;
}

void FaceTracker::shutdown()
{
    mesh_.shutdown();
    shapes_.Reset();
    shapesClear_.Reset();
    meshReady_ = meshCleared_ = false;
    detector_.shutdown();
    track_ = {};
    staging_.Reset();
    faces_.Reset();
    readbacks_.clear();
    pending_.clear();
    slots_ = 0;
    ready_ = haveFrame_ = detectedNow_ = false;
    lastFrameId_ = detections_ = 0;
    lastPts_ = 0.0;
    faceCount_ = candidates_ = 0;
}

void FaceTracker::configure(const engine::BeautyDetection& detection, float minConfidence)
{
    detection_ = detection;
    threshold_ = std::clamp(minConfidence, 0.3f, 0.95f);
    // Hysteresis: a tracked face may dip a little below the score that starts one.
    keepThreshold_ = std::max(0.3f, threshold_ - 0.15f);
    detector_.setScoreThreshold(keepThreshold_);
}

bool FaceTracker::record(ID3D12GraphicsCommandList* list, pipeline::StateTracker& tracker, const Frame& f)
{
    detectedNow_ = false;
    if (!ready_ || !list || f.slot >= slots_ || !f.source || !f.width || !f.height) return false;
    // A repeated frame (paused tuning, a redraw) keeps its faces: no detection, no time.
    const bool sameFrame = haveFrame_ && !f.reset && f.frameId == lastFrameId_;
    double dt = 0.0;
    if (haveFrame_ && !f.reset && !sameFrame) dt = std::clamp(f.ptsMs - lastPts_, 0.0, 250.0);
    // Every new frame is detected (about 0.3 ms at the balanced size on an RTX 5070):
    // the faces are this frame's, so the region never trails the picture.
    const bool detect = !sameFrame;
    const double scale = double(detection_.inputLongSide()) / double(std::max(f.width, f.height));
    const uint32_t width = std::clamp(uint32_t(std::lround(f.width * scale)), 32u, 640u);
    const uint32_t height = std::clamp(uint32_t(std::lround(f.height * scale)), 32u, 640u);
    if (detect) {
        YuNetGpu::Source source;
        source.texture = f.source; source.width = f.width; source.height = f.height; source.hdr = f.hdr;
        if (!detector_.record(list, f.slot, source, width, height)) return false;
        ++detections_;
        detectedNow_ = true;
    }
    uint32_t c[kTrackConstants]{};
    c[0] = width; c[1] = height; c[2] = detector_.candidateCapacity();
    c[3] = (detect ? 1u : 0u) | (f.reset ? 2u : 0u) | (f.still ? 4u : 0u);
    c[4] = bitsOf(float(dt));
    // One Euro smoothing (shaders/BeautyTrack.hlsl): the stability sets the cutoff of a
    // face at rest (3 -> 0.3 Hz) and how fast it rises with speed (20 -> 6 Hz per face
    // width per second); tuned in docs/BEAUTY_IMPLEMENTATION_PLAN_2026-10-17.md.
    const float stability = std::clamp(detection_.stability, 0.0f, 1.0f);
    c[5] = bitsOf(3.0f + (0.3f - 3.0f) * stability);
    c[6] = bitsOf(0.3f);
    c[7] = bitsOf(120.0f);      // a missed face keeps moving this long
    c[8] = bitsOf(120.0f);      // then fades out over this long
    c[9] = detection_.maxFaces;
    c[10] = bitsOf(keepThreshold_);
    c[11] = bitsOf(20.0f + (6.0f - 20.0f) * stability);
    c[12] = bitsOf(4.0f);       // speed estimate cutoff, Hz
    c[13] = bitsOf(80.0f);      // a new face fades in over this long
    c[14] = bitsOf(threshold_);
    tracker.transition(list, faces_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto gpu = [&](UINT index) { return track_.heap->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(index) * track_.increment; };
    track_.bind(list, reinterpret_cast<const float*>(c), gpu(f.slot * kDescriptorsPerSlot), gpu(f.slot * kDescriptorsPerSlot + 1));
    list->Dispatch(1, 1, 1);
    tracker.uavBarrier(list, faces_.Get());
    tracker.transition(list, faces_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readbacks_[f.slot].Get(), 0, faces_.Get(), 0, kFaceBytes);
    tracker.transition(list, faces_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    pending_[f.slot] = true;
    // The outlines of this frame's tracks, smoothed like the tracks themselves. While the
    // status of the last frames reports no face, the mesh's ~45 dispatches would find
    // only empty slots: its outlines are cleared once instead, and a face that appears
    // meanwhile keeps the oval from its box until the mesh runs again (a few frames,
    // during its fade-in). Resets and pictures always run it.
    const bool meshWanted = f.reset || f.still || faceCount_ > 0 || detections_ <= slots_;
    if (meshReady_ && detect && !meshWanted && !meshCleared_) {
        tracker.transition(list, shapes_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        tracker.transition(list, shapesClear_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(shapes_.Get(), 0, shapesClear_.Get(), 0, FaceMeshGpu::kShapeBytes);
        tracker.transition(list, shapes_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        meshCleared_ = true;
    }
    if (meshReady_ && detect && meshWanted) {
        meshCleared_ = false;
        FaceMeshGpu::Frame m;
        m.source = f.source; m.width = f.width; m.height = f.height; m.hdr = f.hdr; m.slot = f.slot;
        m.maxFaces = detection_.maxFaces; m.dtMs = float(dt); m.reset = f.reset; m.still = f.still;
        m.restCutoff = 3.0f + (0.3f - 3.0f) * stability;
        m.speedFactor = 20.0f + (6.0f - 20.0f) * stability;
        m.speedCutoff = 4.0f;
        if (!mesh_.record(list, tracker, m)) log::warn("beauty-mesh", "face mesh could not be recorded for this frame");
    }
    if (!sameFrame) { lastPts_ = f.ptsMs; lastFrameId_ = f.frameId; }
    haveFrame_ = true;
    return true;
}

void FaceTracker::collect(uint32_t slot)
{
    if (!ready_ || slot >= slots_ || !pending_[slot]) return;
    void* mapped = nullptr;
    D3D12_RANGE range{0, SIZE_T(kFaceBytes)};
    if (SUCCEEDED(readbacks_[slot]->Map(0, &range, &mapped)) && mapped) {
        uint32_t meta[4];
        std::memcpy(meta, static_cast<const uint8_t*>(mapped) + 48, sizeof(meta));
        faceCount_ = std::min(meta[0], 8u);
        candidates_ = meta[3];
        D3D12_RANGE none{0, 0};
        readbacks_[slot]->Unmap(0, &none);
    }
    pending_[slot] = false;
}
} // namespace veyra::beauty
