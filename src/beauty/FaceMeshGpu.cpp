#include "veyra/beauty/FaceMeshGpu.h"

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/beauty/TfliteLite.h"
#include "veyra/pipeline/GpuPassUtils.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace veyra::beauty {
namespace {
template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

constexpr const wchar_t* kModelName = L"face_landmark.tflite";
constexpr uint32_t kConstantDwords = 16;
constexpr uint32_t kSrvs = 3, kUavs = 3, kPerSlot = kSrvs + kUavs;   // t0 weights, t1 faces, t2 source | u0 scratch, u1 shapes, u2 state
constexpr uint32_t kNone = 0xffffffffu;
constexpr uint32_t kSide = FaceMeshGpu::kInputSize;
constexpr uint32_t kLandmarkValues = FaceMeshGpu::kLandmarks * 3, kLandmarkQuads = kLandmarkValues / 4;
constexpr uint64_t kStateFloat4s = 2ull * FaceMeshGpu::kMaxFaces * (1 + FaceMeshGpu::kLandmarks);   // two halves, header + points per face
constexpr uint64_t kCropFloat4s = 2ull * FaceMeshGpu::kMaxFaces;

enum class Kind : uint32_t { Crop, Conv, Depthwise, Pointwise, Decode, Count };
constexpr const char* kShaderFile[] = {"BeautyMeshCrop.dxil", "BeautyMeshConv.dxil", "BeautyMeshDepthwise.dxil",
                                       "BeautyMeshPointwise.dxil", "BeautyMeshDecode.dxil"};

uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                                  D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heap;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = std::max<uint64_t>(bytes, 16); d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}

struct Tensor {
    uint32_t w = 0, h = 0, q = 0;           // quads = channels / 4, rounded up
    uint64_t offset = 0;                    // float4 units in the scratch buffer
    int lastRead = -1;                      // layer index
    bool pinned = false;                    // kept to the end (the outputs)
    uint64_t float4s() const { return uint64_t(w) * h * q * FaceMeshGpu::kMaxFaces; }
};
struct Layer {
    Kind kind = Kind::Conv;
    int in = -1, res = -1, out = -1;        // tensors
    uint32_t wOff = 0, bOff = 0, aOff = kNone;
    uint32_t kernel = 1, stride = 1, padTop = 0, padLeft = 0;
    uint32_t residual = 0;                  // 0 none, 1 the same-size tensor, 2 its 2x2 max-pool
};

// One-shot work on a private queue (weight upload, the test hook).
struct Submit {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    bool open(ID3D12Device* device)
    {
        return SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
               SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) &&
               SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    }
    bool run(ID3D12CommandQueue* queue)
    {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, 20000);
        CloseHandle(event);
        return fence->GetCompletedValue() >= 1;
    }
};

void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
}
void uavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = r;
    list->ResourceBarrier(1, &b);
}
} // namespace

std::filesystem::path locateFaceMeshModel()
{
    wchar_t exe[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    const auto exeDirectory = length && length < std::size(exe) ? std::filesystem::path(exe).parent_path() : std::filesystem::current_path();
    const auto root = runtime::applicationRoot();
    const std::filesystem::path folders[] = {root / "runtime" / "models", root / "runtime_local" / "models", exeDirectory / "models"};
    std::error_code ec;
    for (const auto& folder : folders)
        if (std::filesystem::is_regular_file(folder / kModelName, ec)) return folder / kModelName;
    return {};
}

struct FaceMeshGpu::Impl {
    ID3D12Device* device = nullptr;
    std::string description;
    std::vector<Tensor> tensors;
    std::vector<Layer> layers;
    std::vector<float> weights;                    // float4 aligned
    int inputTensor = -1, landmarkTensor = -1, flagTensor = -1;
    uint64_t scratchFloat4s = 0;
    ComPtr<ID3D12Resource> weightBuffer, scratch, state;
    ID3D12Resource* faces = nullptr;
    ID3D12Resource* shapes = nullptr;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipelines[size_t(Kind::Count)];
    ComPtr<ID3D12DescriptorHeap> heap, staging;
    UINT increment = 0;
    uint32_t slots = 0, parity = 0;
    bool ready = false;

    // ------------------------------------------------------------------ plan
    uint32_t allocate(size_t float4s)
    {
        const size_t at = weights.size() / 4;
        weights.resize(weights.size() + float4s * 4, 0.0f);
        return uint32_t(at);
    }
    float& weight(uint32_t float4Index, uint32_t component) { return weights[size_t(float4Index) * 4 + component]; }

    // Convolution [O, k, k, I] (TFLite OHWI) and its bias.
    void packConv(const TfliteTensor& w, const TfliteTensor& b, Layer& l)
    {
        const uint32_t o = uint32_t(w.shape[0]), k = uint32_t(w.shape[1]), in = uint32_t(w.shape[3]);
        const uint32_t oq = (o + 3) / 4, iq = (in + 3) / 4;
        l.wOff = allocate(size_t(oq) * k * k * iq * 4);
        for (uint32_t q = 0; q < oq; ++q)
            for (uint32_t ky = 0; ky < k; ++ky)
                for (uint32_t kx = 0; kx < k; ++kx)
                    for (uint32_t c = 0; c < in; ++c)
                        for (uint32_t j = 0; j < 4 && 4 * q + j < o; ++j)
                            weight(l.wOff + ((q * k + ky) * k + kx) * iq * 4 + c, j) = w.floats[((size_t(4 * q + j) * k + ky) * k + kx) * in + c];
        l.bOff = allocate(oq);
        for (uint32_t c = 0; c < o; ++c) weight(l.bOff + c / 4, c % 4) = b.floats[c];
    }
    // Depthwise [1, 3, 3, C] and its bias.
    void packDepthwise(const TfliteTensor& w, const TfliteTensor& b, Layer& l)
    {
        const uint32_t channels = uint32_t(w.shape[3]), cq = (channels + 3) / 4;
        l.wOff = allocate(size_t(cq) * 9);
        for (uint32_t tap = 0; tap < 9; ++tap)
            for (uint32_t c = 0; c < channels; ++c) weight(l.wOff + (c / 4) * 9 + tap, c % 4) = w.floats[size_t(tap) * channels + c];
        l.bOff = allocate(cq);
        for (uint32_t c = 0; c < channels; ++c) weight(l.bOff + c / 4, c % 4) = b.floats[c];
    }

    int newTensor(uint32_t w, uint32_t h, uint32_t channels)
    {
        Tensor t;
        t.w = w; t.h = h; t.q = (channels + 3) / 4;
        tensors.push_back(t);
        return int(tensors.size()) - 1;
    }

    // The network as fused layers: every convolution, with the residual add (of the
    // block input itself, or of its 2x2 max-pool, channel padded) and the PReLU that
    // follow it folded into the 1x1 convolution. MAX_POOL / PAD / ADD / PRELU appear
    // only inside those patterns; anything else is refused.
    bool buildPlan(const TfliteModel& m, std::string& error)
    {
        const size_t tensorCount = m.tensors.size();
        std::vector<std::vector<int>> consumers(tensorCount);
        std::vector<int> producer(tensorCount, -1);
        for (size_t i = 0; i < m.operators.size(); ++i) {
            for (const auto t : m.operators[i].inputs) if (t >= 0) consumers[size_t(t)].push_back(int(i));
            for (const auto t : m.operators[i].outputs) producer[size_t(t)] = int(i);
        }
        const auto single = [&](int t) { return consumers[size_t(t)].size() == 1 ? consumers[size_t(t)][0] : -1; };
        const auto shapeOf = [&](int t, uint32_t& w, uint32_t& h, uint32_t& c) {
            const auto& s = m.tensors[size_t(t)].shape;
            if (s.size() != 4 || s[0] != 1 || s[1] <= 0 || s[2] <= 0 || s[3] <= 0) return false;
            h = uint32_t(s[1]); w = uint32_t(s[2]); c = uint32_t(s[3]);
            return true;
        };
        if (m.tensors[size_t(m.inputs[0])].shape != std::vector<int32_t>{1, int32_t(kSide), int32_t(kSide), 3}) {
            error = "model input is not 1 x 192 x 192 x 3"; return false;
        }
        std::vector<int> gpu(tensorCount, -1);
        inputTensor = newTensor(kSide, kSide, 3);
        gpu[size_t(m.inputs[0])] = inputTensor;
        std::vector<bool> done(m.operators.size(), false);
        for (size_t i = 0; i < m.operators.size(); ++i) {
            if (done[i]) continue;
            const TfliteOperator& op = m.operators[i];
            // A shortcut's max-pool / padding may come before the 1x1 convolution that
            // takes them in; every operator must have been taken in by the end.
            if (op.op == TfliteOp::MaxPool2D || op.op == TfliteOp::Pad) continue;
            if (op.op != TfliteOp::Conv2D && op.op != TfliteOp::DepthwiseConv2D) {
                error = std::format("operator {} is outside a supported block", i); return false;
            }
            if (op.inputs.size() < 3 || op.inputs[0] < 0 || gpu[size_t(op.inputs[0])] < 0 || op.inputs[1] < 0 || op.inputs[2] < 0 ||
                op.outputs.size() != 1) {
                error = std::format("convolution {} input is not available", i); return false;
            }
            const TfliteTensor& w = m.tensors[size_t(op.inputs[1])];
            const TfliteTensor& b = m.tensors[size_t(op.inputs[2])];
            uint32_t inW, inH, inC, outW, outH, outC;
            if (w.floats.empty() || b.floats.empty() || op.activation != 0 || op.dilationW != 1 || op.dilationH != 1 ||
                op.strideW != op.strideH || op.strideW < 1 || op.strideW > 3 ||
                !shapeOf(op.inputs[0], inW, inH, inC) || !shapeOf(op.outputs[0], outW, outH, outC) || b.floats.size() != outC) {
                error = std::format("convolution {} has an unsupported configuration", i); return false;
            }
            Layer layer;
            layer.in = gpu[size_t(op.inputs[0])];
            layer.stride = uint32_t(op.strideW);
            if (op.op == TfliteOp::Conv2D) {
                if (w.shape.size() != 4 || uint32_t(w.shape[0]) != outC || uint32_t(w.shape[3]) != inC || w.shape[1] != w.shape[2] ||
                    w.shape[1] < 1 || w.shape[1] > 3) {
                    error = std::format("convolution {} weights", i); return false;
                }
                layer.kernel = uint32_t(w.shape[1]);
                layer.kind = layer.kernel == 1 && layer.stride == 1 ? Kind::Pointwise : Kind::Conv;
                packConv(w, b, layer);
            } else {
                if (w.shape != std::vector<int32_t>{1, 3, 3, int32_t(inC)} || op.depthMultiplier != 1 || inC != outC) {
                    error = std::format("depthwise convolution {} weights", i); return false;
                }
                layer.kernel = 3;
                layer.kind = Kind::Depthwise;
                packDepthwise(w, b, layer);
            }
            // TensorFlow SAME: the odd pixel of the padding goes to the bottom / right.
            const auto expected = [&](uint32_t size) {
                return op.samePadding ? (size + layer.stride - 1) / layer.stride : (size - layer.kernel) / layer.stride + 1;
            };
            if (outW != expected(inW) || outH != expected(inH)) { error = std::format("convolution {} output size", i); return false; }
            if (op.samePadding) {
                const auto pad = [&](uint32_t size, uint32_t out) {
                    const int64_t total = std::max<int64_t>(int64_t(out - 1) * layer.stride + layer.kernel - size, 0);
                    return uint32_t(total / 2);
                };
                layer.padTop = pad(inH, outH);
                layer.padLeft = pad(inW, outW);
            }
            int finalOut = op.outputs[0];
            int next = single(finalOut);
            if (layer.kind == Kind::Pointwise && next >= 0 && m.operators[size_t(next)].op == TfliteOp::Add) {
                const TfliteOperator& add = m.operators[size_t(next)];
                if (add.inputs.size() != 2 || add.activation != 0) { error = std::format("residual add {}", next); return false; }
                const int other = add.inputs[0] == finalOut ? add.inputs[1] : add.inputs[0];
                if (other >= 0 && gpu[size_t(other)] >= 0) {
                    const Tensor& r = tensors[size_t(gpu[size_t(other)])];
                    if (r.w != outW || r.h != outH || r.q != (outC + 3) / 4) { error = std::format("residual add {} shapes", next); return false; }
                    layer.res = gpu[size_t(other)];
                    layer.residual = 1;
                } else {
                    int pool = other >= 0 ? producer[size_t(other)] : -1, padOp = -1;
                    if (pool >= 0 && m.operators[size_t(pool)].op == TfliteOp::Pad) {
                        padOp = pool;
                        const TfliteOperator& pad = m.operators[size_t(padOp)];
                        const auto& pads = pad.inputs.size() == 2 && pad.inputs[1] >= 0 ? m.tensors[size_t(pad.inputs[1])].ints : std::vector<int32_t>{};
                        if (pads.size() != 8 || pads[0] || pads[1] || pads[2] || pads[3] || pads[4] || pads[5] || pads[6] || pads[7] < 0 ||
                            single(pad.inputs[0]) != padOp) {
                            error = std::format("shortcut padding {}", padOp); return false;
                        }
                        pool = producer[size_t(pad.inputs[0])];
                    }
                    if (pool < 0 || m.operators[size_t(pool)].op != TfliteOp::MaxPool2D) { error = std::format("residual add {} input", next); return false; }
                    const TfliteOperator& mp = m.operators[size_t(pool)];
                    if (mp.filterW != 2 || mp.filterH != 2 || mp.strideW != 2 || mp.strideH != 2 || mp.samePadding || mp.activation ||
                        mp.inputs.empty() || mp.inputs[0] < 0 || gpu[size_t(mp.inputs[0])] < 0 || single(mp.outputs[0]) != (padOp >= 0 ? padOp : next)) {
                        error = std::format("shortcut pooling {}", pool); return false;
                    }
                    const int source = gpu[size_t(mp.inputs[0])];
                    const Tensor& r = tensors[size_t(source)];
                    if (r.w != outW * 2 || r.h != outH * 2 || r.q > (outC + 3) / 4) { error = std::format("shortcut pooling {} shapes", pool); return false; }
                    layer.res = source;
                    layer.residual = 2;
                    done[size_t(pool)] = true;
                    if (padOp >= 0) done[size_t(padOp)] = true;
                }
                done[size_t(next)] = true;
                finalOut = add.outputs[0];
                next = single(finalOut);
            }
            if (next >= 0 && m.operators[size_t(next)].op == TfliteOp::Prelu) {
                const TfliteOperator& prelu = m.operators[size_t(next)];
                if (prelu.inputs.size() != 2 || prelu.inputs[1] < 0 || m.tensors[size_t(prelu.inputs[1])].floats.size() != outC) {
                    error = std::format("PReLU {} slopes", next); return false;
                }
                const auto& slopes = m.tensors[size_t(prelu.inputs[1])].floats;
                layer.aOff = allocate((outC + 3) / 4);
                for (uint32_t c = 0; c < outC; ++c) weight(layer.aOff + c / 4, c % 4) = slopes[c];
                done[size_t(next)] = true;
                finalOut = prelu.outputs[0];
            }
            layer.out = newTensor(outW, outH, outC);
            gpu[size_t(finalOut)] = layer.out;
            layers.push_back(layer);
            done[i] = true;
        }
        for (size_t i = 0; i < m.operators.size(); ++i)
            if (!done[i]) { error = std::format("operator {} is not part of a supported block", i); return false; }
        for (const auto output : m.outputs) {
            uint32_t w, h, c;
            if (output < 0 || gpu[size_t(output)] < 0 || !shapeOf(output, w, h, c) || w != 1 || h != 1) { error = "model outputs"; return false; }
            if (c == kLandmarkValues) landmarkTensor = gpu[size_t(output)];
            else if (c == 1) flagTensor = gpu[size_t(output)];
        }
        if (landmarkTensor < 0 || flagTensor < 0) { error = "expected a 1404-value landmark output and a face flag"; return false; }
        tensors[size_t(landmarkTensor)].pinned = tensors[size_t(flagTensor)].pinned = true;
        for (size_t i = 0; i < layers.size(); ++i) {
            tensors[size_t(layers[i].in)].lastRead = int(i);
            if (layers[i].res >= 0) tensors[size_t(layers[i].res)].lastRead = int(i);
        }
        return true;
    }

    // Tensor placement in the scratch buffer (after the crop parameters): first fit,
    // reusing the space of tensors no later layer reads.
    void placeTensors()
    {
        struct Block { uint64_t offset, size; int tensor; };
        std::vector<Block> live;
        uint64_t end = kCropFloat4s;
        const auto place = [&](int t) {
            const uint64_t size = tensors[size_t(t)].float4s();
            std::sort(live.begin(), live.end(), [](const Block& a, const Block& b) { return a.offset < b.offset; });
            uint64_t at = kCropFloat4s;
            for (const auto& b : live) {
                if (at + size <= b.offset) break;
                at = std::max(at, b.offset + b.size);
            }
            tensors[size_t(t)].offset = at;
            live.push_back({at, size, t});
            end = std::max(end, at + size);
        };
        place(inputTensor);
        for (size_t i = 0; i < layers.size(); ++i) {
            std::erase_if(live, [&](const Block& b) { const Tensor& t = tensors[size_t(b.tensor)]; return !t.pinned && t.lastRead < int(i); });
            place(layers[i].out);
        }
        scratchFloat4s = end;
    }

    // ------------------------------------------------------------- resources
    bool upload(ID3D12CommandQueue* queue, std::string& error)
    {
        const uint64_t bytes = uint64_t(weights.size()) * 4;
        weightBuffer = makeBuffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        auto staged = makeBuffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!weightBuffer || !staged) { error = "weight buffer allocation failed"; return false; }
        void* mapped = nullptr;
        if (FAILED(staged->Map(0, nullptr, &mapped))) { error = "weight upload map failed"; return false; }
        std::memcpy(mapped, weights.data(), size_t(bytes));
        staged->Unmap(0, nullptr);
        Submit s;
        if (!s.open(device)) { error = "weight upload list creation failed"; return false; }
        s.list->CopyBufferRegion(weightBuffer.Get(), 0, staged.Get(), 0, bytes);
        barrier(s.list.Get(), weightBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (!s.run(queue)) { error = "weight upload timed out"; return false; }
        return true;
    }

    bool createPipelines(std::string& error)
    {
        D3D12_DESCRIPTOR_RANGE1 srv{};
        srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = kSrvs;
        srv.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_DESCRIPTOR_RANGE1 uav{};
        uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uav.NumDescriptors = kUavs;
        uav.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_ROOT_PARAMETER1 rp[3]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[0].Constants.Num32BitValues = kConstantDwords;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &srv;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[2].DescriptorTable.NumDescriptorRanges = 1;
        rp[2].DescriptorTable.pDescriptorRanges = &uav;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd{};
        rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rd.Desc_1_1.NumParameters = 3;
        rd.Desc_1_1.pParameters = rp;
        ComPtr<ID3DBlob> blob, message;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rd, &blob, &message)) ||
            FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) {
            error = "face mesh root signature creation failed"; return false;
        }
        for (size_t k = 0; k < size_t(Kind::Count); ++k) {
            std::vector<uint8_t> code;
            if (!pipeline::loadShaderBytes(kShaderFile[k], code)) { error = std::string("shader missing: ") + kShaderFile[k]; return false; }
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = root.Get();
            pd.CS = {code.data(), code.size()};
            if (FAILED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipelines[k])))) {
                error = std::string("face mesh pipeline failed: ") + kShaderFile[k]; return false;
            }
        }
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(UINT index) const
    {
        auto h = heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(index) * increment;
        return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpuAt(UINT index) const
    {
        auto h = heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += UINT64(index) * increment;
        return h;
    }

    bool createDescriptors(std::string& error)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = slots * kPerSlot;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        D3D12_DESCRIPTOR_HEAP_DESC sd = hd;
        sd.NumDescriptors = slots;
        sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap))) || FAILED(device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&staging)))) {
            error = "face mesh descriptor heap creation failed"; return false;
        }
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto structuredSrv = [&](ID3D12Resource* r, UINT elements, UINT stride, UINT at) {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.Format = DXGI_FORMAT_UNKNOWN;
            d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Buffer.NumElements = elements;
            d.Buffer.StructureByteStride = stride;
            device->CreateShaderResourceView(r, &d, cpuAt(at));
        };
        const auto structuredUav = [&](ID3D12Resource* r, UINT elements, UINT stride, UINT at) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
            d.Format = DXGI_FORMAT_UNKNOWN;
            d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            d.Buffer.NumElements = elements;
            d.Buffer.StructureByteStride = stride;
            device->CreateUnorderedAccessView(r, nullptr, &d, cpuAt(at));
        };
        for (uint32_t s = 0; s < slots; ++s) {
            const UINT base = s * kPerSlot;
            structuredSrv(weightBuffer.Get(), UINT(weights.size() / 4), 16, base);
            structuredSrv(faces, 1 + kMaxFaces, 64, base + 1);
            // t2 (the source picture) is written for every recorded frame; a null view until then.
            D3D12_SHADER_RESOURCE_VIEW_DESC nd{};
            nd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            nd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            nd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nd.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(nullptr, &nd, cpuAt(base + 2));
            structuredUav(scratch.Get(), UINT(scratchFloat4s), 16, base + 3);
            structuredUav(shapes, kMaxFaces * kShapeEntries, 8, base + 4);
            structuredUav(state.Get(), UINT(kStateFloat4s), 16, base + 5);
        }
        return true;
    }

    // ----------------------------------------------------------------- record
    void dispatch(ID3D12GraphicsCommandList* list, uint32_t slot, Kind kind, const uint32_t* constants, uint32_t groupsX, uint32_t groupsZ)
    {
        list->SetPipelineState(pipelines[size_t(kind)].Get());
        list->SetComputeRoot32BitConstants(0, kConstantDwords, constants, 0);
        list->SetComputeRootDescriptorTable(1, gpuAt(slot * kPerSlot));
        list->SetComputeRootDescriptorTable(2, gpuAt(slot * kPerSlot + kSrvs));
        list->Dispatch(groupsX, 1, groupsZ);
    }

    void recordLayers(ID3D12GraphicsCommandList* list, uint32_t slot, uint32_t capacity, uint32_t forced)
    {
        for (const auto& l : layers) {
            const Tensor& in = tensors[size_t(l.in)];
            const Tensor& out = tensors[size_t(l.out)];
            uint32_t c[kConstantDwords]{};
            c[0] = in.w; c[1] = in.h; c[2] = in.q;
            c[3] = out.w; c[4] = out.h; c[5] = out.q;
            c[6] = l.wOff; c[7] = l.bOff; c[8] = l.aOff;
            c[9] = uint32_t(in.offset); c[10] = uint32_t(out.offset);
            c[11] = l.res >= 0 ? uint32_t(tensors[size_t(l.res)].offset) : 0u;
            c[12] = (l.res >= 0 ? tensors[size_t(l.res)].q : 0u) | (l.residual << 16);
            c[13] = l.kernel | (l.stride << 8) | (l.padTop << 16) | (l.padLeft << 24);
            c[14] = capacity; c[15] = forced;
            dispatch(list, slot, l.kind, c, (out.w * out.h * out.q + 63) / 64, capacity);
            uavBarrier(list, scratch.Get());
        }
    }
};

FaceMeshGpu::FaceMeshGpu() : impl_(std::make_unique<Impl>()) {}
FaceMeshGpu::~FaceMeshGpu() = default;
bool FaceMeshGpu::ready() const { return impl_ && impl_->ready; }
const std::string& FaceMeshGpu::description() const { return impl_->description; }

bool FaceMeshGpu::initialize(ID3D12Device* device, const std::filesystem::path& model, ID3D12Resource* faces, ID3D12Resource* shapes,
                             uint32_t ringSlots, std::string& error)
{
    shutdown();
    if (!device || !faces || !shapes || !ringSlots) { error = "invalid face mesh configuration"; return false; }
    if (model.empty()) { error = "face mesh model file not found"; return false; }
    TfliteModel m;
    if (!loadTflite(model, m, error)) return false;
    Impl& d = *impl_;
    d.device = device;
    d.faces = faces;
    d.shapes = shapes;
    d.slots = ringSlots;
    if (!d.buildPlan(m, error)) { shutdown(); return false; }
    d.placeTensors();
    d.scratch = makeBuffer(device, d.scratchFloat4s * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    d.state = makeBuffer(device, kStateFloat4s * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!d.scratch || !d.state) { error = "face mesh buffer allocation failed"; shutdown(); return false; }
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) || !d.upload(queue.Get(), error) || !d.createPipelines(error) ||
        !d.createDescriptors(error)) {
        if (error.empty()) error = "face mesh upload queue creation failed";
        shutdown();
        return false;
    }
    d.description = std::format("MediaPipe face mesh: {} operators, {} GPU layers, {} weights, scratch {:.1f} MiB for {} faces",
                                m.operators.size(), d.layers.size(), d.weights.size(), double(d.scratchFloat4s) * 16.0 / 1048576.0, kMaxFaces);
    d.ready = true;
    log::info("beauty-mesh", d.description + "; model " + model.filename().string());
    return true;
}

void FaceMeshGpu::shutdown()
{
    *impl_ = Impl{};
}

bool FaceMeshGpu::record(ID3D12GraphicsCommandList* list, pipeline::StateTracker& tracker, const Frame& f)
{
    Impl& d = *impl_;
    if (!d.ready || !list || f.slot >= d.slots || !f.source || !f.width || !f.height) return false;
    // The source picture of this frame into the slot's t2.
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = f.source->GetDesc().Format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    auto staged = d.staging->GetCPUDescriptorHandleForHeapStart();
    staged.ptr += SIZE_T(f.slot) * d.increment;
    d.device->CreateShaderResourceView(f.source, &sd, staged);
    d.device->CopyDescriptorsSimple(1, d.cpuAt(f.slot * kPerSlot + 2), staged, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap* heaps[] = {d.heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(d.root.Get());
    tracker.transition(list, d.faces, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, f.source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    tracker.transition(list, d.shapes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const uint32_t capacity = std::clamp(f.maxFaces, 1u, kMaxFaces);
    uint32_t c[kConstantDwords]{};
    c[0] = f.width; c[1] = f.height; c[2] = f.hdr ? 1u : 0u;
    c[3] = kSide; c[4] = kSide; c[5] = 1;
    c[6] = bitsOf(kCropScale);
    c[10] = uint32_t(d.tensors[size_t(d.inputTensor)].offset);
    c[11] = 0;                                   // crop parameters at the start of the scratch buffer
    c[14] = capacity;
    d.dispatch(list, f.slot, Kind::Crop, c, (kSide * kSide + 63) / 64, capacity);
    uavBarrier(list, d.scratch.Get());
    d.recordLayers(list, f.slot, capacity, 0);
    uint32_t k[kConstantDwords]{};
    k[0] = f.width; k[1] = f.height;
    k[6] = bitsOf(f.restCutoff); k[7] = bitsOf(f.speedFactor); k[8] = bitsOf(f.speedCutoff);
    k[9] = uint32_t(d.tensors[size_t(d.landmarkTensor)].offset);
    k[10] = uint32_t(d.tensors[size_t(d.flagTensor)].offset);
    k[11] = 0;
    k[12] = bitsOf(f.dtMs);
    k[13] = (f.reset ? 1u : 0u) | (f.still ? 2u : 0u) | (d.parity << 2);
    k[14] = capacity;
    d.dispatch(list, f.slot, Kind::Decode, k, 1, kMaxFaces);   // every slot: empty ones are marked "no mesh"
    uavBarrier(list, d.state.Get());
    tracker.uavBarrier(list, d.shapes);
    tracker.transition(list, d.shapes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    d.parity ^= 1u;
    return true;
}

bool FaceMeshGpu::runNetwork(ID3D12CommandQueue* queue, const std::vector<float>& input, uint32_t count, std::vector<float>& output,
                             std::string& error)
{
    Impl& d = *impl_;
    const size_t perFace = size_t(kSide) * kSide * 3;
    if (!d.ready || !queue || !count || count > kMaxFaces || input.size() != perFace * count) { error = "invalid network test input"; return false; }
    const Tensor& in = d.tensors[size_t(d.inputTensor)];
    const Tensor& landmarks = d.tensors[size_t(d.landmarkTensor)];
    const Tensor& flag = d.tensors[size_t(d.flagTensor)];
    // Input: channel-last RGB -> one float4 per pixel, faces one after the other.
    const uint64_t inputBytes = uint64_t(count) * kSide * kSide * 16;
    auto upload = makeBuffer(d.device, inputBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    const uint64_t landmarkBytes = uint64_t(count) * landmarks.q * 16, flagBytes = uint64_t(count) * flag.q * 16;
    auto readback = makeBuffer(d.device, landmarkBytes + flagBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!upload || !readback) { error = "network test buffers"; return false; }
    float* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) { error = "network test map"; return false; }
    for (size_t p = 0; p < size_t(count) * kSide * kSide; ++p) {
        mapped[p * 4] = input[p * 3]; mapped[p * 4 + 1] = input[p * 3 + 1]; mapped[p * 4 + 2] = input[p * 3 + 2]; mapped[p * 4 + 3] = 0.0f;
    }
    upload->Unmap(0, nullptr);
    Submit s;
    if (!s.open(d.device)) { error = "network test list"; return false; }
    ID3D12GraphicsCommandList* list = s.list.Get();
    barrier(list, d.scratch.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(d.scratch.Get(), in.offset * 16, upload.Get(), 0, inputBytes);
    barrier(list, d.scratch.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[] = {d.heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(d.root.Get());
    barrier(list, d.faces, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    d.recordLayers(list, 0, count, count);
    barrier(list, d.faces, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    barrier(list, d.scratch.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback.Get(), 0, d.scratch.Get(), landmarks.offset * 16, landmarkBytes);
    list->CopyBufferRegion(readback.Get(), landmarkBytes, d.scratch.Get(), flag.offset * 16, flagBytes);
    barrier(list, d.scratch.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!s.run(queue)) { error = "network test timed out"; return false; }
    const float* result = nullptr;
    D3D12_RANGE range{0, SIZE_T(landmarkBytes + flagBytes)};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(const_cast<float**>(&result))))) { error = "network test readback"; return false; }
    output.assign(size_t(count) * (kLandmarkValues + 1), 0.0f);
    for (uint32_t face = 0; face < count; ++face) {
        std::memcpy(output.data() + size_t(face) * (kLandmarkValues + 1), result + size_t(face) * landmarks.q * 4, kLandmarkValues * 4);
        output[size_t(face) * (kLandmarkValues + 1) + kLandmarkValues] = result[landmarkBytes / 4 + size_t(face) * flag.q * 4];
    }
    D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return true;
}

} // namespace veyra::beauty
