#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace veyra::beauty {

// One detected face, in detector-input pixels (top-left origin, before any
// mapping back to the frame).
struct FaceBox {
    float x = 0, y = 0, w = 0, h = 0;
    // right eye, left eye, nose tip, right mouth corner, left mouth corner (x, y).
    std::array<float, 10> landmarks{};
    float score = 0;
    uint32_t id = 0;    // scale << 24 | anchor index: deterministic tie-break only
};

struct YuNetConfig {
    uint32_t maxWidth = 640;        // largest padded detector input (multiple of 32)
    uint32_t maxHeight = 640;
    uint32_t ringSlots = 3;         // detections that may be in flight at once
    uint32_t candidateCapacity = 1024;
    float scoreThreshold = 0.6f;
    float nmsThreshold = 0.3f;
    bool readback = true;           // copy each slot's candidates to a CPU-readable buffer (collect)
};

// YuNet (OpenCV Zoo / libfacedetection, MIT) face detector as D3D12 compute
// kernels. The network is read from the ONNX file at run time and compiled into
// a short list of layers (stem, 1x1, depthwise 3x3, max-pool, upsample+add and a
// per-stride decode); nothing is rendered or read back except a bounded list of
// candidate faces. Dispatches are recorded into the caller's command list, so the
// caller decides which queue runs them and when the result may be collected.
class YuNetGpu {
public:
    struct Source {
        ID3D12Resource* texture = nullptr;  // RGBA16F / RGBA32F float texture, readable as a non-pixel SRV
        uint32_t width = 0, height = 0;     // full texture size
        bool hdr = false;                   // scRGB (1.0 = 80 nits) instead of linear SDR
        uint32_t originX = 0, originY = 0;  // optional crop; 0 size = whole texture
        uint32_t regionWidth = 0, regionHeight = 0;
    };

    YuNetGpu();
    ~YuNetGpu();
    YuNetGpu(const YuNetGpu&) = delete;
    YuNetGpu& operator=(const YuNetGpu&) = delete;

    bool initialize(ID3D12Device* device, const std::filesystem::path& modelPath,
                    const YuNetConfig& config, std::string& error);
    void shutdown();
    bool ready() const;

    // Whole detection: prep (resample + encode the source), network, decode and
    // the copy of the candidate list into this slot's readback buffer.
    // detWidth/detHeight are the content size (<= padded maximum, any size, the
    // model input is padded up to a multiple of 32).
    bool record(ID3D12GraphicsCommandList* list, uint32_t slot, const Source& source,
                uint32_t detWidth, uint32_t detHeight);
    // Network + decode + readback copy only; the caller filled inputBuffer().
    bool recordNetwork(ID3D12GraphicsCommandList* list, uint32_t slot, uint32_t detWidth, uint32_t detHeight);

    // After the fence for `slot` completed: candidates -> NMS -> faces sorted by
    // score. Returns false when the readback is not available.
    bool collect(uint32_t slot, std::vector<FaceBox>& faces, uint32_t* rawCandidates = nullptr);
    static void nonMaxSuppression(std::vector<FaceBox>& boxes, float iouThreshold);

    // GPU consumers (the beauty tracking pass): this slot's candidate list as a raw
    // buffer (count, then 64-byte records), in the UAV state between records.
    ID3D12Resource* candidateBuffer(uint32_t slot) const;
    uint64_t candidateBufferBytes() const;
    uint32_t candidateCapacity() const;
    // Live: the decode keeps candidates at or above this score.
    void setScoreThreshold(float threshold);

    // Test and tooling hooks.
    ID3D12Resource* inputBuffer() const;                 // padded BGR0 float4 grid, UAV state
    ID3D12Resource* headBuffer(uint32_t scale) const;    // merged 16-channel head, UAV state
    uint32_t paddedExtent(uint32_t value) const { return (value + 31u) / 32u * 32u; }
    uint32_t layerCount() const;
    const char* layerName(uint32_t index) const;
    // Per-layer timestamps: EndQuery at base (start) and base + 1 + i after layer i.
    void setProfiler(ID3D12QueryHeap* heap, uint32_t baseIndex);
    uint64_t weightBytes() const;
    uint64_t workingSetBytes() const;                    // all tensor buffers at the maximum size
    uint64_t multiplyAccumulates(uint32_t detWidth, uint32_t detHeight) const;
    const std::string& modelDescription() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace veyra::beauty
