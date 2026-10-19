#pragma once
#include <d3d12.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace veyra::pipeline { class StateTracker; }

namespace veyra::beauty {

// The shipped face mesh model: MediaPipe face_landmark (468 points, v0.8.9, Apache-2.0).
// Like the detector model it never enters the source tree; the same folders are searched
// (runtime/models, runtime_local/models, models next to the executable). Empty when absent.
std::filesystem::path locateFaceMeshModel();

// MediaPipe's face landmark network as D3D12 compute kernels. For every face the
// tracker keeps (shaders/BeautyTrack.hlsl) it crops the frame around the face, runs
// the network for all faces at once and turns the 468 points into the outlines the
// node passes mask with: the face contour, the eyes, the brows and the lips
// (shaders/BeautyFaces.hlsli kShape*). The network is read from the TFLite file at
// run time and fused into about fifty layers (stem, depthwise 3x3, 1x1 with the
// block's residual and PReLU, heads). Everything is recorded into the caller's
// command list; nothing is read back.
class FaceMeshGpu {
public:
    struct Frame {
        ID3D12Resource* source = nullptr;   // linear RGBA16F / RGBA32F picture, non-pixel SRV
        uint32_t width = 0, height = 0;
        bool hdr = false;                   // scRGB working space
        uint32_t slot = 0;                  // command ring slot of the list
        uint32_t maxFaces = 8;              // tracks to process (the max faces setting)
        float dtMs = 0.0f;                  // time since the previous recorded frame
        bool reset = false, still = false;  // the points start without history
        float restCutoff = 1.65f, speedFactor = 13.0f, speedCutoff = 4.0f;   // One Euro, as the tracks
    };

    FaceMeshGpu();
    ~FaceMeshGpu();
    FaceMeshGpu(const FaceMeshGpu&) = delete;
    FaceMeshGpu& operator=(const FaceMeshGpu&) = delete;

    // `faces`: the tracker's faces buffer (structured, 64-byte entries, read as SRV);
    // `shapes`: the outline buffer this writes (kShapeEntries float2 per face slot).
    bool initialize(ID3D12Device* device, const std::filesystem::path& model, ID3D12Resource* faces, ID3D12Resource* shapes,
                    uint32_t ringSlots, std::string& error);
    void shutdown();
    bool ready() const;
    const std::string& description() const;
    // Crop, network and outlines for this frame's tracks; the faces buffer must be in the
    // non-pixel SRV state. Leaves the outlines in the non-pixel SRV state.
    bool record(ID3D12GraphicsCommandList* list, pipeline::StateTracker& tracker, const Frame& frame);
    // Test hook, synchronous on `queue`: the network alone on `count` crops (192 x 192 x 3
    // RGB 0..1, NHWC, one after the other); `output` gets count x (1404 landmark values,
    // then the face flag logit).
    bool runNetwork(ID3D12CommandQueue* queue, const std::vector<float>& input, uint32_t count, std::vector<float>& output, std::string& error);

    static constexpr uint32_t kInputSize = 192, kLandmarks = 468, kShapeEntries = 128, kMaxFaces = 8;
    static constexpr uint64_t kShapeBytes = uint64_t(kMaxFaces) * kShapeEntries * 8;
    // Crop side over the face box's long side: MediaPipe crops 1.5x its detector's box;
    // YuNet's box is a little larger (the crop MediaPipe's tracking would take from the
    // points is 0.96 of ours, median of 48 reference faces).
    static constexpr float kCropScale = 1.44f;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace veyra::beauty
