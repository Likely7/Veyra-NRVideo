#pragma once
#include "veyra/beauty/FaceMeshGpu.h"
#include "veyra/beauty/YuNetGpu.h"
#include "veyra/engine/BeautySettings.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <filesystem>
#include <string>
#include <vector>

namespace veyra::beauty {
// The shipped YuNet model (OpenCV Zoo 47534e27, MIT). It is never part of the source
// tree: packages carry it in runtime/models, development trees in runtime_local/models,
// staged tests next to the executable in models/. Empty when none is present.
std::filesystem::path locateFaceModel();

// Faces for every beauty node of one graph: the YuNet detector on every new frame,
// before any effect, followed by the GPU tracking pass (shaders/BeautyTrack.hlsl)
// and the face mesh of every track (FaceMeshGpu: contour, eyes, brows, lips).
// All of it is recorded into the frame's own command list; the node passes read the
// faces buffer later in the same frame, so the faces belong to that frame. Only a
// 64-byte status header is read back, a few frames late, for the UI.
class FaceTracker {
public:
    struct Frame {
        ID3D12Resource* source = nullptr;   // linear RGBA16F / RGBA32F picture before every effect node
        uint32_t width = 0, height = 0;
        bool hdr = false;                   // scRGB working space
        uint32_t slot = 0;                  // command ring slot of `list`
        uint64_t frameId = 0;               // the same source frame is never detected twice
        double ptsMs = 0.0;
        bool reset = false;                 // seek, scene cut, timeline break, new source
        bool still = false;                 // a picture: fresh faces, no smoothing
    };

    bool initialize(ID3D12Device* device, const std::filesystem::path& model, uint32_t ringSlots, std::string& error);
    void shutdown();
    bool ready() const { return ready_; }
    // Live: input size, face count and smoothing, and the lowest confidence any node accepts.
    void configure(const engine::BeautyDetection& detection, float minConfidence);
    // Detection when due, then tracking. Leaves the faces buffer in the non-pixel SRV state.
    bool record(ID3D12GraphicsCommandList* list, pipeline::StateTracker& tracker, const Frame& frame);
    ID3D12Resource* faces() const { return faces_.Get(); }
    // Face outlines (FaceMeshGpu layout); all "no mesh" when the mesh model is missing.
    ID3D12Resource* shapes() const { return shapes_.Get(); }
    bool meshReady() const { return meshReady_; }
    const std::string& meshDescription() const { return mesh_.description(); }
    // Reads the status this slot carried last time (its fence has completed when the ring hands the slot out).
    void collect(uint32_t slot);
    uint32_t faceCount() const { return faceCount_; }
    uint32_t rawCandidates() const { return candidates_; }
    uint64_t detections() const { return detections_; }
    bool detectedThisFrame() const { return detectedNow_; }
    const std::string& description() const { return detector_.modelDescription(); }
    static constexpr uint32_t kFaceEntries = 9;   // header + eight faces, 64 bytes each

private:
    YuNetGpu detector_;
    FaceMeshGpu mesh_;
    pipeline::ComPtr<ID3D12Resource> shapes_, shapesClear_;
    bool meshReady_ = false, meshCleared_ = false;
    pipeline::ComputePass track_;
    pipeline::ComPtr<ID3D12DescriptorHeap> staging_;
    pipeline::ComPtr<ID3D12Resource> faces_;
    std::vector<pipeline::ComPtr<ID3D12Resource>> readbacks_;
    std::vector<bool> pending_;
    engine::BeautyDetection detection_{};
    float threshold_ = 0.6f;      // a new face needs this score
    float keepThreshold_ = 0.45f; // a tracked face continues down to this one (the detector's own threshold)
    uint32_t slots_ = 0;
    bool ready_ = false, haveFrame_ = false, detectedNow_ = false;
    uint64_t lastFrameId_ = 0, detections_ = 0;
    double lastPts_ = 0.0;
    uint32_t faceCount_ = 0, candidates_ = 0;
};
} // namespace veyra::beauty
