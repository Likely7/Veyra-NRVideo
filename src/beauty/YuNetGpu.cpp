#include "veyra/beauty/YuNetGpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <unordered_map>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/beauty/OnnxLite.h"

namespace veyra::beauty {
namespace {

template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

constexpr uint32_t kConstantDwords = 16;
constexpr uint32_t kSrvPerSlot = 2;     // t0 weights, t1 source frame
constexpr uint32_t kUavPerLayer = 3;    // u0 first input, u1 second input, u2 output
constexpr uint32_t kMaxTaps = 8;

enum class Kind : uint8_t { Prep, Stem, Pointwise, Depthwise, Pool, UpAdd, Decode, Count };
constexpr const char* kShaderFile[] = {"BeautyYuNetPrep.dxil", "BeautyYuNetStem.dxil", "BeautyYuNetPointwise.dxil",
    "BeautyYuNetDepthwise.dxil", "BeautyYuNetPool.dxil", "BeautyYuNetUpAdd.dxil", "BeautyYuNetDecode.dxil"};
constexpr const char* kKindName[] = {"prep", "stem", "pointwise", "depthwise", "pool", "upadd", "decode"};

struct TensorDesc {
    std::string name;
    uint32_t quads = 0;       // channels / 4
    uint32_t shift = 0;       // log2 of the stride relative to the padded input
    int firstWrite = -1, lastRead = -1;
    int buffer = -1;
    bool pinned = false;      // never shared with other tensors
};

struct LayerDesc {
    Kind kind = Kind::Stem;
    int in0 = -1, in1 = -1, out = -1;   // tensor ids; Decode writes the candidate buffer instead
    uint32_t wOff = 0, bOff = 0;
    bool relu = false;
    int scale = -1;                      // Decode: 0/1/2 -> stride 8/16/32
    std::string name;
};

uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

D3D12_RESOURCE_DESC bufferDesc(uint64_t bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    return d;
}

ComPtr<ID3D12Resource> createBuffer(ID3D12Device* device, uint64_t bytes, D3D12_HEAP_TYPE heap,
                                    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heap;
    const auto desc = bufferDesc(bytes, flags);
    ComPtr<ID3D12Resource> r;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&r))))
        return nullptr;
    return r;
}

bool loadDxil(const char* name, std::vector<uint8_t>& bytes)
{
    const auto path = runtime::shaderDirectory() / name;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        log::error("beauty-detect", std::format("shader missing: {}", path.string()));
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes.resize(size_t(size > 0 ? size : 0));
    const size_t read = bytes.empty() ? 0 : fread(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return !bytes.empty() && read == bytes.size();
}

} // namespace

struct YuNetGpu::Impl {
    ID3D12Device* device = nullptr;
    YuNetConfig cfg;
    std::string description;
    std::vector<TensorDesc> tensors;
    std::vector<LayerDesc> layers;
    std::vector<float> weights;                   // float4-aligned
    std::vector<ComPtr<ID3D12Resource>> buffers;
    std::vector<uint64_t> bufferBytes;
    ComPtr<ID3D12Resource> weightBuffer;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelines[size_t(Kind::Count)];
    ComPtr<ID3D12DescriptorHeap> heap, stagingHeap;
    UINT increment = 0;
    std::vector<ComPtr<ID3D12Resource>> candidates, readbacks;
    ComPtr<ID3D12Resource> zeroUpload;
    uint64_t candidateBytes = 0;
    uint32_t uavBase = 0;                         // first UAV descriptor in the heap
    std::vector<uint32_t> layerUav;               // per layer: base index; Decode: base of slot 0
    int headTensor[3] = {-1, -1, -1};
    int inputTensor = -1;
    ID3D12QueryHeap* profileHeap = nullptr;
    uint32_t profileBase = 0;
    uint64_t workingSet = 0;
    bool ready = false;

    // ------------------------------------------------------------------ plan
    uint32_t allocate(size_t float4Count)
    {
        const size_t offset = weights.size() / 4;
        weights.resize(weights.size() + float4Count * 4, 0.0f);
        return uint32_t(offset);
    }

    int newTensor(const std::string& name, uint32_t quads, uint32_t shift, bool pinned = false)
    {
        TensorDesc t;
        t.name = name; t.quads = quads; t.shift = shift; t.pinned = pinned;
        tensors.push_back(std::move(t));
        return int(tensors.size()) - 1;
    }

    // Packs one convolution into the shared weight array; returns false for a
    // shape the kernels do not implement.
    bool packStem(const OnnxTensor& w, const OnnxTensor& b, LayerDesc& layer, std::string& error)
    {
        if (w.dims.size() != 4 || w.dims[1] != 3 || w.dims[2] != 3 || w.dims[3] != 3 || w.dims[0] % 4 || b.floats.size() != size_t(w.dims[0])) {
            error = "stem convolution shape"; return false;
        }
        const size_t cout = size_t(w.dims[0]), quads = cout / 4;
        layer.wOff = allocate(quads * 9 * 3);
        layer.bOff = allocate(quads);
        for (size_t q = 0; q < quads; ++q) {
            for (size_t t = 0; t < 9; ++t) for (size_t c = 0; c < 3; ++c) for (size_t k = 0; k < 4; ++k)
                weights[(layer.wOff + (q * 9 + t) * 3 + c) * 4 + k] = w.floats[((4 * q + k) * 3 + c) * 9 + t];
            for (size_t k = 0; k < 4; ++k) weights[(layer.bOff + q) * 4 + k] = b.floats[4 * q + k];
        }
        return true;
    }

    bool packPointwise(const std::vector<float>& w, const std::vector<float>& b, size_t cout, size_t cin, LayerDesc& layer, std::string& error)
    {
        if (cout % 4 || cin % 4 || w.size() != cout * cin || b.size() != cout) { error = "pointwise convolution shape"; return false; }
        layer.wOff = allocate(cout / 4 * cin);
        layer.bOff = allocate(cout / 4);
        for (size_t g = 0; g < cout / 4; ++g) {
            for (size_t c = 0; c < cin; ++c) for (size_t k = 0; k < 4; ++k)
                weights[(layer.wOff + g * cin + c) * 4 + k] = w[(4 * g + k) * cin + c];
            for (size_t k = 0; k < 4; ++k) weights[(layer.bOff + g) * 4 + k] = b[4 * g + k];
        }
        return true;
    }

    bool packDepthwise(const std::vector<float>& w, const std::vector<float>& b, size_t channels, LayerDesc& layer, std::string& error)
    {
        if (channels % 4 || w.size() != channels * 9 || b.size() != channels) { error = "depthwise convolution shape"; return false; }
        layer.wOff = allocate(channels / 4 * 9);
        layer.bOff = allocate(channels / 4);
        for (size_t q = 0; q < channels / 4; ++q) {
            for (size_t t = 0; t < 9; ++t) for (size_t k = 0; k < 4; ++k)
                weights[(layer.wOff + q * 9 + t) * 4 + k] = w[(4 * q + k) * 9 + t];
            for (size_t k = 0; k < 4; ++k) weights[(layer.bOff + q) * 4 + k] = b[4 * q + k];
        }
        return true;
    }

    bool buildPlan(const OnnxModel& model, std::string& error)
    {
        static const std::set<std::string> allowed = {"Conv", "Relu", "MaxPool", "Resize", "Add", "Transpose", "Reshape", "Sigmoid"};
        for (const auto& n : model.nodes) if (!allowed.count(n.op)) { error = "unsupported operator " + n.op; return false; }
        if (model.opset < 9 || model.opset > 21) { error = std::format("unexpected opset {}", model.opset); return false; }
        if (model.inputs.size() != 1 || model.inputs[0].dims.size() != 4 || model.inputs[0].dims[1] != 3) {
            error = "model input is not a single NCHW RGB tensor"; return false;
        }
        std::unordered_map<std::string, int> producer, consumers;
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            for (const auto& o : model.nodes[i].outputs) producer[o] = int(i);
            for (const auto& in : model.nodes[i].inputs) ++consumers[in];
        }
        const auto init = [&](const std::string& name) -> const OnnxTensor* {
            const auto it = model.initializers.find(name);
            return it == model.initializers.end() ? nullptr : &it->second;
        };
        const auto convTensors = [&](const OnnxNode& n, const OnnxTensor*& w, const OnnxTensor*& b) {
            w = n.inputs.size() > 1 ? init(n.inputs[1]) : nullptr;
            b = n.inputs.size() > 2 ? init(n.inputs[2]) : nullptr;
            return w && b && w->dataType == 1 && b->dataType == 1 && w->floats.size() == w->elementCount() && b->floats.size() == b->elementCount();
        };

        // 1. The twelve head outputs: trace each back to its (1x1, depthwise 3x3) pair.
        struct HeadChain { int pw = -1, dw = -1; std::string feature; };
        static const char* kTypeName[4] = {"cls", "obj", "bbox", "kps"};
        static const size_t kTypeChannels[4] = {1, 1, 4, 10};
        static const uint32_t kStrides[3] = {8, 16, 32};
        HeadChain heads[4][3];
        std::vector<bool> headNode(model.nodes.size(), false);
        for (int t = 0; t < 4; ++t) for (int s = 0; s < 3; ++s) {
            const std::string output = std::format("{}_{}", kTypeName[t], kStrides[s]);
            if (std::none_of(model.outputs.begin(), model.outputs.end(), [&](const OnnxValueInfo& o) { return o.name == output; })) {
                error = "missing model output " + output; return false;
            }
            const auto at = producer.find(output);
            int n = at == producer.end() ? -1 : at->second;
            const auto step = [&](const char* op) -> bool {
                if (n < 0 || model.nodes[size_t(n)].op != op || model.nodes[size_t(n)].inputs.empty()) { error = std::format("head {} does not match the expected {} chain", output, op); return false; }
                headNode[size_t(n)] = true;
                return true;
            };
            const auto advance = [&]() { const auto it = producer.find(model.nodes[size_t(n)].inputs[0]); n = it == producer.end() ? -1 : it->second; };
            if (t < 2) { if (!step("Sigmoid")) return false; advance(); }
            if (!step("Reshape")) return false; advance();
            if (!step("Transpose")) return false; advance();
            if (!step("Conv")) return false;
            heads[t][s].dw = n; advance();
            if (!step("Conv")) return false;
            heads[t][s].pw = n;
            heads[t][s].feature = model.nodes[size_t(n)].inputs[0];
            const auto& pw = model.nodes[size_t(heads[t][s].pw)];
            const auto& dw = model.nodes[size_t(heads[t][s].dw)];
            const OnnxTensor *pww, *pwb, *dww, *dwb;
            if (!convTensors(pw, pww, pwb) || !convTensors(dw, dww, dwb)) { error = "head convolution weights"; return false; }
            const size_t c = kTypeChannels[t];
            if (pww->dims.size() != 4 || size_t(pww->dims[0]) != c || pww->dims[1] != 64 || pww->dims[2] != 1 || pww->dims[3] != 1 ||
                dww->dims.size() != 4 || size_t(dww->dims[0]) != c || dww->dims[1] != 1 || dww->dims[2] != 3 || dww->dims[3] != 3 ||
                pw.intOr("group", 1) != 1 || dw.intOr("group", 1) != int64_t(c)) {
                error = "head convolution shapes"; return false;
            }
        }
        for (int s = 0; s < 3; ++s) for (int t = 1; t < 4; ++t)
            if (heads[t][s].feature != heads[0][s].feature) { error = "head branches use different features"; return false; }

        // 2. Backbone and neck: everything that is not part of a head.
        inputTensor = newTensor(model.inputs[0].name, 1, 0, true);
        std::unordered_map<std::string, int> tensorOf;
        tensorOf[model.inputs[0].name] = inputTensor;
        std::unordered_map<std::string, std::string> upsampled;     // Resize output -> its input
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (headNode[i]) continue;
            const OnnxNode& node = model.nodes[i];
            LayerDesc layer;
            layer.name = node.name.empty() ? node.outputs[0] : node.name;
            if (node.op == "Conv") {
                const OnnxTensor *w, *b;
                if (!convTensors(node, w, b) || w->dims.size() != 4) { error = "convolution weights in " + layer.name; return false; }
                const auto in = tensorOf.find(node.inputs[0]);
                if (in == tensorOf.end()) { error = "convolution input is not available: " + node.inputs[0]; return false; }
                const TensorDesc inDesc = tensors[size_t(in->second)];
                const int64_t group = node.intOr("group", 1), stride = node.intOr("strides", 1);
                const int64_t cout = w->dims[0], cinPerGroup = w->dims[1], kh = w->dims[2], kw = w->dims[3];
                uint32_t outShift = inDesc.shift;
                if (kh == 3 && kw == 3 && group == 1 && stride == 2 && cinPerGroup == 3 && inDesc.quads == 1) {
                    layer.kind = Kind::Stem; outShift += 1;
                    if (!packStem(*w, *b, layer, error)) return false;
                } else if (kh == 1 && kw == 1 && group == 1 && stride == 1 && size_t(cinPerGroup) == size_t(inDesc.quads) * 4) {
                    layer.kind = Kind::Pointwise;
                    if (!packPointwise(w->floats, b->floats, size_t(cout), size_t(cinPerGroup), layer, error)) return false;
                } else if (kh == 3 && kw == 3 && group == cout && cinPerGroup == 1 && stride == 1 && size_t(cout) == size_t(inDesc.quads) * 4) {
                    layer.kind = Kind::Depthwise;
                    if (!packDepthwise(w->floats, b->floats, size_t(cout), layer, error)) return false;
                } else { error = "unsupported convolution configuration in " + layer.name; return false; }
                std::string outName = node.outputs[0];
                if (i + 1 < model.nodes.size() && !headNode[i + 1] && model.nodes[i + 1].op == "Relu" &&
                    model.nodes[i + 1].inputs[0] == node.outputs[0] && consumers[node.outputs[0]] == 1) {
                    layer.relu = true;
                    outName = model.nodes[i + 1].outputs[0];
                    ++i;
                }
                layer.in0 = in->second;
                layer.out = newTensor(outName, uint32_t(cout / 4), outShift);
                tensorOf[outName] = layer.out;
            } else if (node.op == "MaxPool") {
                const auto in = tensorOf.find(node.inputs[0]);
                const auto& k = node.ints.find("kernel_shape");
                if (in == tensorOf.end() || k == node.ints.end() || k->second != std::vector<int64_t>{2, 2} || node.intOr("strides", 0) != 2) { error = "unsupported pooling in " + layer.name; return false; }
                layer.kind = Kind::Pool;
                layer.in0 = in->second;
                layer.out = newTensor(node.outputs[0], tensors[size_t(in->second)].quads, tensors[size_t(in->second)].shift + 1);
                tensorOf[node.outputs[0]] = layer.out;
            } else if (node.op == "Resize") {
                const OnnxTensor* scales = node.inputs.size() > 2 ? init(node.inputs[2]) : nullptr;
                const auto mode = node.strings.find("mode");
                if (!scales || scales->floats != std::vector<float>{1, 1, 2, 2} || mode == node.strings.end() || mode->second != "nearest") {
                    error = "unsupported resize in " + layer.name; return false;
                }
                upsampled[node.outputs[0]] = node.inputs[0];
                continue;       // fused into the Add that consumes it
            } else if (node.op == "Add") {
                std::string coarseName, skipName;
                if (upsampled.count(node.inputs[1])) { coarseName = upsampled[node.inputs[1]]; skipName = node.inputs[0]; }
                else if (upsampled.count(node.inputs[0])) { coarseName = upsampled[node.inputs[0]]; skipName = node.inputs[1]; }
                else { error = "unsupported add in " + layer.name; return false; }
                const auto coarse = tensorOf.find(coarseName), skip = tensorOf.find(skipName);
                if (coarse == tensorOf.end() || skip == tensorOf.end()) { error = "add inputs are not available in " + layer.name; return false; }
                const TensorDesc& c = tensors[size_t(coarse->second)];
                const TensorDesc& s = tensors[size_t(skip->second)];
                if (c.quads != s.quads || c.shift != s.shift + 1) { error = "add operand shapes differ in " + layer.name; return false; }
                layer.kind = Kind::UpAdd;
                layer.in0 = coarse->second; layer.in1 = skip->second;
                layer.out = newTensor(node.outputs[0], s.quads, s.shift);
                tensorOf[node.outputs[0]] = layer.out;
            } else { error = "unexpected operator " + node.op + " outside the heads"; return false; }
            layers.push_back(std::move(layer));
        }

        // 3. Heads: the four branches of a stride share their input, so their 1x1
        // convolutions and depthwise 3x3 convolutions are concatenated channel-wise
        // (cls, obj, box x4, landmarks x10) into one 16-channel pair per stride.
        for (int s = 0; s < 3; ++s) {
            const auto feature = tensorOf.find(heads[0][s].feature);
            if (feature == tensorOf.end() || tensors[size_t(feature->second)].shift != 3 + uint32_t(s) || tensors[size_t(feature->second)].quads != 16) {
                error = std::format("head feature for stride {} has an unexpected shape", kStrides[s]); return false;
            }
            std::vector<float> pw, pwBias, dw, dwBias;
            for (int t : {0, 1, 2, 3}) {
                const OnnxTensor *w, *b;
                (void)convTensors(model.nodes[size_t(heads[t][s].pw)], w, b);
                pw.insert(pw.end(), w->floats.begin(), w->floats.end());
                pwBias.insert(pwBias.end(), b->floats.begin(), b->floats.end());
                (void)convTensors(model.nodes[size_t(heads[t][s].dw)], w, b);
                dw.insert(dw.end(), w->floats.begin(), w->floats.end());
                dwBias.insert(dwBias.end(), b->floats.begin(), b->floats.end());
            }
            LayerDesc first, second;
            first.kind = Kind::Pointwise; first.name = std::format("head{}-pw", kStrides[s]);
            second.kind = Kind::Depthwise; second.name = std::format("head{}-dw", kStrides[s]);
            if (!packPointwise(pw, pwBias, 16, 64, first, error) || !packDepthwise(dw, dwBias, 16, second, error)) return false;
            first.in0 = feature->second;
            first.out = newTensor(std::format("head{}-pre", kStrides[s]), 4, 3 + uint32_t(s));
            second.in0 = first.out;
            second.out = newTensor(std::format("head{}", kStrides[s]), 4, 3 + uint32_t(s), true);
            headTensor[s] = second.out;
            layers.push_back(std::move(first));
            layers.push_back(std::move(second));
        }
        for (int s = 0; s < 3; ++s) {
            LayerDesc decode;
            decode.kind = Kind::Decode; decode.scale = s; decode.in0 = headTensor[s];
            decode.name = std::format("decode{}", kStrides[s]);
            layers.push_back(std::move(decode));
        }

        // The prep layer is layer 0 conceptually: it writes the input tensor.
        LayerDesc prep;
        prep.kind = Kind::Prep; prep.out = inputTensor; prep.name = "prep";
        layers.insert(layers.begin(), std::move(prep));

        // Liveness (first write / last read) for buffer sharing.
        for (size_t i = 0; i < layers.size(); ++i) {
            const LayerDesc& l = layers[i];
            for (const int t : {l.in0, l.in1}) if (t >= 0) tensors[size_t(t)].lastRead = std::max(tensors[size_t(t)].lastRead, int(i));
            if (l.out >= 0 && tensors[size_t(l.out)].firstWrite < 0) tensors[size_t(l.out)].firstWrite = int(i);
        }
        description = std::format("YuNet-compatible CNN: opset {}, {} nodes, {} GPU layers, {} weights, input {} (producer {})",
            model.opset, model.nodes.size(), layers.size(), weights.size(), model.inputs[0].name, model.producer);
        return true;
    }

    // ------------------------------------------------------------- resources
    uint64_t tensorBytes(const TensorDesc& t) const
    {
        const uint64_t w = std::max<uint64_t>(1, cfg.maxWidth >> t.shift), h = std::max<uint64_t>(1, cfg.maxHeight >> t.shift);
        return w * h * t.quads * 16;
    }

    bool createBuffers(std::string& error)
    {
        // Greedy buffer sharing: a tensor may reuse a buffer once every earlier
        // tensor that lives in it has been read for the last time.
        std::vector<int> owner;                       // per buffer: tensor currently held, -1 when free
        for (size_t i = 0; i < layers.size(); ++i) {
            for (size_t b = 0; b < owner.size(); ++b)
                if (owner[b] >= 0 && !tensors[size_t(owner[b])].pinned && tensors[size_t(owner[b])].lastRead < int(i)) owner[b] = -1;
            const int out = layers[i].out;
            if (out < 0 || tensors[size_t(out)].buffer >= 0) continue;
            TensorDesc& t = tensors[size_t(out)];
            const uint64_t need = tensorBytes(t);
            int pick = -1;
            if (!t.pinned) for (size_t b = 0; b < owner.size(); ++b)
                if (owner[b] < 0 && bufferBytes[b] >= need && (pick < 0 || bufferBytes[b] < bufferBytes[size_t(pick)])) pick = int(b);
            if (pick < 0) {
                auto buffer = createBuffer(device, need, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if (!buffer) { error = std::format("tensor buffer allocation failed ({} bytes)", need); return false; }
                buffers.push_back(buffer); bufferBytes.push_back(need); owner.push_back(-1);
                workingSet += need;
                pick = int(buffers.size()) - 1;
            }
            t.buffer = pick;
            owner[size_t(pick)] = out;
        }
        // Weights: upload once, then readable as a structured SRV.
        const uint64_t bytes = uint64_t(weights.size()) * 4;
        weightBuffer = createBuffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        auto upload = createBuffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!weightBuffer || !upload) { error = "weight buffer allocation failed"; return false; }
        void* mapped = nullptr;
        if (FAILED(upload->Map(0, nullptr, &mapped))) { error = "weight upload map failed"; return false; }
        std::memcpy(mapped, weights.data(), size_t(bytes));
        upload->Unmap(0, nullptr);
        // One-shot copy on a private queue so initialize() stays self-contained.
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) { error = "weight upload queue creation failed"; return false; }
        list->CopyBufferRegion(weightBuffer.Get(), 0, upload.Get(), 0, bytes);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = weightBuffer.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        queue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, 10000);
        CloseHandle(event);
        if (fence->GetCompletedValue() < 1) { error = "weight upload timed out"; return false; }

        candidateBytes = 16 + uint64_t(cfg.candidateCapacity) * 64;
        zeroUpload = createBuffer(device, 16, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!zeroUpload) { error = "zero buffer allocation failed"; return false; }
        void* zero = nullptr;
        if (SUCCEEDED(zeroUpload->Map(0, nullptr, &zero))) { std::memset(zero, 0, 16); zeroUpload->Unmap(0, nullptr); }
        for (uint32_t s = 0; s < cfg.ringSlots; ++s) {
            candidates.push_back(createBuffer(device, candidateBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            readbacks.push_back(createBuffer(device, candidateBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST));
            if (!candidates.back() || !readbacks.back()) { error = "candidate buffer allocation failed"; return false; }
        }
        return true;
    }

    bool createPipelines(std::string& error)
    {
        D3D12_DESCRIPTOR_RANGE1 srv{};
        srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = kSrvPerSlot;
        srv.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_DESCRIPTOR_RANGE1 uav{};
        uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uav.NumDescriptors = kUavPerLayer;
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
            FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) {
            error = "root signature creation failed"; return false;
        }
        for (size_t k = 0; k < size_t(Kind::Count); ++k) {
            std::vector<uint8_t> code;
            if (!loadDxil(kShaderFile[k], code)) { error = std::string("shader missing: ") + kShaderFile[k]; return false; }
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = rootSignature.Get();
            pd.CS = {code.data(), code.size()};
            const HRESULT hr = device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipelines[k]));
            if (FAILED(hr)) { error = std::format("pipeline {} failed hr=0x{:08X}", kKindName[k], unsigned(hr)); return false; }
        }
        return true;
    }

    void structuredUav(ID3D12Resource* resource, uint64_t bytes, D3D12_CPU_DESCRIPTOR_HANDLE at) const
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_UNKNOWN;
        d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        d.Buffer.NumElements = UINT(bytes / 16);
        d.Buffer.StructureByteStride = 16;
        device->CreateUnorderedAccessView(resource, nullptr, &d, at);
    }

    bool createDescriptors(std::string& error)
    {
        uint32_t decodeLayers = 0;
        for (const auto& l : layers) decodeLayers += l.kind == Kind::Decode;
        uavBase = kSrvPerSlot * cfg.ringSlots;
        const uint32_t total = uavBase + kUavPerLayer * (uint32_t(layers.size()) - decodeLayers + decodeLayers * cfg.ringSlots);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = total;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        D3D12_DESCRIPTOR_HEAP_DESC sd = hd;
        sd.NumDescriptors = cfg.ringSlots;
        sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap))) || FAILED(device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&stagingHeap)))) {
            error = "descriptor heap creation failed"; return false;
        }
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto cpuAt = [&](uint32_t index) { auto h = heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(index) * increment; return h; };
        // t0 weights in every slot pair; t1 (source frame) is written per detection.
        D3D12_SHADER_RESOURCE_VIEW_DESC wd{};
        wd.Format = DXGI_FORMAT_UNKNOWN;
        wd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        wd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        wd.Buffer.NumElements = UINT(weights.size() / 4);
        wd.Buffer.StructureByteStride = 16;
        for (uint32_t s = 0; s < cfg.ringSlots; ++s) {
            device->CreateShaderResourceView(weightBuffer.Get(), &wd, cpuAt(s * kSrvPerSlot));
            // Slot t1 starts as a null texture view so the pair is always valid.
            D3D12_SHADER_RESOURCE_VIEW_DESC nd{};
            nd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            nd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            nd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nd.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(nullptr, &nd, cpuAt(s * kSrvPerSlot + 1));
        }
        layerUav.assign(layers.size(), 0);
        uint32_t next = uavBase;
        for (size_t i = 0; i < layers.size(); ++i) {
            const LayerDesc& l = layers[i];
            layerUav[i] = next;
            const auto bufferOf = [&](int t) { return t < 0 ? nullptr : buffers[size_t(tensors[size_t(t)].buffer)].Get(); };
            const auto bytesOf = [&](int t) { return bufferBytes[size_t(tensors[size_t(t)].buffer)]; };
            if (l.kind == Kind::Decode) {
                for (uint32_t s = 0; s < cfg.ringSlots; ++s) {
                    structuredUav(bufferOf(l.in0), bytesOf(l.in0), cpuAt(next));
                    structuredUav(bufferOf(l.in0), bytesOf(l.in0), cpuAt(next + 1));
                    D3D12_UNORDERED_ACCESS_VIEW_DESC raw{};
                    raw.Format = DXGI_FORMAT_R32_TYPELESS;
                    raw.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    raw.Buffer.NumElements = UINT(candidateBytes / 4);
                    raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    device->CreateUnorderedAccessView(candidates[s].Get(), nullptr, &raw, cpuAt(next + 2));
                    next += kUavPerLayer;
                }
                continue;
            }
            const int in0 = l.in0 >= 0 ? l.in0 : l.out;
            const int in1 = l.in1 >= 0 ? l.in1 : in0;
            structuredUav(bufferOf(in0), bytesOf(in0), cpuAt(next));
            structuredUav(bufferOf(in1), bytesOf(in1), cpuAt(next + 1));
            structuredUav(bufferOf(l.out), bytesOf(l.out), cpuAt(next + 2));
            next += kUavPerLayer;
        }
        return true;
    }

    // ----------------------------------------------------------------- record
    D3D12_GPU_DESCRIPTOR_HANDLE gpuAt(uint32_t index) const
    {
        auto h = heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += UINT64(index) * increment;
        return h;
    }

    void uavBarrier(ID3D12GraphicsCommandList* list) const
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        list->ResourceBarrier(1, &b);
    }

    void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) const
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
    }

    bool recordLayers(ID3D12GraphicsCommandList* list, uint32_t slot, const Source* source, uint32_t detWidth, uint32_t detHeight)
    {
        const uint32_t padW = (detWidth + 31) / 32 * 32, padH = (detHeight + 31) / 32 * 32;
        if (!ready || slot >= cfg.ringSlots || detWidth < 32 || detHeight < 32 || padW > cfg.maxWidth || padH > cfg.maxHeight) return false;
        if (source && (!source->texture || !source->width || !source->height)) return false;

        ID3D12DescriptorHeap* heaps[] = {heap.Get()};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(rootSignature.Get());
        if (source) {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = source->texture->GetDesc().Format;   // RGBA16F or RGBA32F, both read as float4
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MipLevels = 1;
            auto staging = stagingHeap->GetCPUDescriptorHandleForHeapStart();
            staging.ptr += SIZE_T(slot) * increment;
            device->CreateShaderResourceView(source->texture, &sd, staging);
            auto visible = heap->GetCPUDescriptorHandleForHeapStart();
            visible.ptr += SIZE_T(slot * kSrvPerSlot + 1) * increment;
            device->CopyDescriptorsSimple(1, visible, staging, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        // Empty the candidate list of this slot.
        transition(list, candidates[slot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(candidates[slot].Get(), 0, zeroUpload.Get(), 0, 16);
        transition(list, candidates[slot].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uavBarrier(list);

        if (profileHeap) list->EndQuery(profileHeap, D3D12_QUERY_TYPE_TIMESTAMP, profileBase);
        for (size_t i = source ? 0 : 1; i < layers.size(); ++i) {
            const LayerDesc& l = layers[i];
            uint32_t c[kConstantDwords] = {};
            const auto dims = [&](int t, uint32_t& w, uint32_t& h, uint32_t& q) {
                const TensorDesc& d = tensors[size_t(t)];
                w = std::max(1u, padW >> d.shift); h = std::max(1u, padH >> d.shift); q = d.quads;
            };
            if (l.in0 >= 0) dims(l.in0, c[0], c[1], c[2]);
            if (l.out >= 0) dims(l.out, c[3], c[4], c[5]);
            c[6] = l.wOff; c[7] = l.bOff; c[8] = l.relu ? 1u : 0u;
            uint32_t groupsOf = 0;
            switch (l.kind) {
            case Kind::Prep: {
                uint32_t rw = source->regionWidth ? source->regionWidth : source->width, rh = source->regionHeight ? source->regionHeight : source->height;
                rw = std::min(rw, source->width - std::min(source->originX, source->width - 1));
                rh = std::min(rh, source->height - std::min(source->originY, source->height - 1));
                const float scale = std::max(float(rw) / float(detWidth), float(rh) / float(detHeight));
                const uint32_t taps = std::clamp(uint32_t(std::ceil(scale)), 1u, kMaxTaps);
                c[3] = padW; c[4] = padH;
                c[9] = rw; c[10] = rh; c[11] = detWidth; c[12] = detHeight; c[13] = source->hdr ? 1u : 0u; c[14] = taps;
                c[15] = (source->originX & 0xffffu) | (source->originY << 16);
                groupsOf = padW * padH;
                break;
            }
            case Kind::Stem: case Kind::Pointwise: case Kind::Depthwise: case Kind::Pool: case Kind::UpAdd:
                groupsOf = c[3] * c[4] * c[5];
                break;
            case Kind::Decode:
                c[3] = padW >> (3 + l.scale); c[4] = padH >> (3 + l.scale);
                c[9] = 8u << l.scale; c[10] = cfg.candidateCapacity; c[11] = bits(cfg.scoreThreshold); c[12] = uint32_t(l.scale);
                groupsOf = c[3] * c[4];
                break;
            default: return false;
            }
            list->SetPipelineState(pipelines[size_t(l.kind)].Get());
            list->SetComputeRoot32BitConstants(0, kConstantDwords, c, 0);
            list->SetComputeRootDescriptorTable(1, gpuAt(slot * kSrvPerSlot));
            list->SetComputeRootDescriptorTable(2, gpuAt(layerUav[i] + (l.kind == Kind::Decode ? slot * kUavPerLayer : 0)));
            list->Dispatch((groupsOf + 63) / 64, 1, 1);
            uavBarrier(list);
            if (profileHeap) list->EndQuery(profileHeap, D3D12_QUERY_TYPE_TIMESTAMP, profileBase + 1 + uint32_t(i));
        }
        if (cfg.readback) {
            transition(list, candidates[slot].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(readbacks[slot].Get(), 0, candidates[slot].Get(), 0, candidateBytes);
            transition(list, candidates[slot].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        } else {
            uavBarrier(list);
        }
        return true;
    }

    uint64_t macs(uint32_t detWidth, uint32_t detHeight) const
    {
        const uint32_t padW = (detWidth + 31) / 32 * 32, padH = (detHeight + 31) / 32 * 32;
        uint64_t total = 0;
        for (const auto& l : layers) {
            if (l.out < 0) continue;
            const TensorDesc& o = tensors[size_t(l.out)];
            const uint64_t pixels = uint64_t(std::max(1u, padW >> o.shift)) * std::max(1u, padH >> o.shift);
            const uint64_t cout = uint64_t(o.quads) * 4;
            if (l.kind == Kind::Stem) total += pixels * cout * 27;
            else if (l.kind == Kind::Pointwise) total += pixels * cout * uint64_t(tensors[size_t(l.in0)].quads) * 4;
            else if (l.kind == Kind::Depthwise) total += pixels * cout * 9;
        }
        return total;
    }
};

YuNetGpu::YuNetGpu() : impl_(std::make_unique<Impl>()) {}
YuNetGpu::~YuNetGpu() = default;

bool YuNetGpu::ready() const { return impl_ && impl_->ready; }
const std::string& YuNetGpu::modelDescription() const { return impl_->description; }
uint32_t YuNetGpu::layerCount() const { return uint32_t(impl_->layers.size()); }
const char* YuNetGpu::layerName(uint32_t index) const { return index < impl_->layers.size() ? impl_->layers[index].name.c_str() : ""; }
void YuNetGpu::setProfiler(ID3D12QueryHeap* heap, uint32_t baseIndex) { impl_->profileHeap = heap; impl_->profileBase = baseIndex; }
uint64_t YuNetGpu::weightBytes() const { return uint64_t(impl_->weights.size()) * 4; }
uint64_t YuNetGpu::workingSetBytes() const { return impl_->workingSet; }
uint64_t YuNetGpu::multiplyAccumulates(uint32_t w, uint32_t h) const { return impl_->macs(w, h); }
ID3D12Resource* YuNetGpu::candidateBuffer(uint32_t slot) const
{
    return impl_->ready && slot < impl_->candidates.size() ? impl_->candidates[slot].Get() : nullptr;
}
uint64_t YuNetGpu::candidateBufferBytes() const { return impl_->candidateBytes; }
uint32_t YuNetGpu::candidateCapacity() const { return impl_->cfg.candidateCapacity; }
void YuNetGpu::setScoreThreshold(float threshold) { impl_->cfg.scoreThreshold = threshold; }
ID3D12Resource* YuNetGpu::inputBuffer() const
{
    return impl_->ready ? impl_->buffers[size_t(impl_->tensors[size_t(impl_->inputTensor)].buffer)].Get() : nullptr;
}
ID3D12Resource* YuNetGpu::headBuffer(uint32_t scale) const
{
    return impl_->ready && scale < 3 ? impl_->buffers[size_t(impl_->tensors[size_t(impl_->headTensor[scale])].buffer)].Get() : nullptr;
}

bool YuNetGpu::initialize(ID3D12Device* device, const std::filesystem::path& modelPath, const YuNetConfig& config, std::string& error)
{
    shutdown();
    Impl& m = *impl_;
    m.device = device;
    m.cfg = config;
    if (!device || config.maxWidth < 32 || config.maxHeight < 32 || config.maxWidth % 32 || config.maxHeight % 32 || config.ringSlots == 0 || config.candidateCapacity == 0) {
        error = "invalid detector configuration"; return false;
    }
    std::vector<uint8_t> bytes;
    OnnxModel model;
    if (!readFileBytes(modelPath, bytes, error) || !parseOnnx(bytes, model, error)) return false;
    if (!m.buildPlan(model, error) || !m.createBuffers(error) || !m.createPipelines(error) || !m.createDescriptors(error)) {
        const std::string reason = error;
        shutdown();
        error = reason;
        log::error("beauty-detect", "face detector initialisation failed: " + reason);
        return false;
    }
    m.ready = true;
    log::info("beauty-detect", std::format("{}; weights {} KiB; working set {:.1f} MiB at {}x{}", m.description, weightBytes() / 1024,
        double(m.workingSet) / (1024.0 * 1024.0), config.maxWidth, config.maxHeight));
    return true;
}

void YuNetGpu::shutdown()
{
    Impl& m = *impl_;
    const YuNetConfig keep = m.cfg;
    m = Impl{};
    m.cfg = keep;
}

bool YuNetGpu::record(ID3D12GraphicsCommandList* list, uint32_t slot, const Source& source, uint32_t detWidth, uint32_t detHeight)
{
    return impl_->recordLayers(list, slot, &source, detWidth, detHeight);
}

bool YuNetGpu::recordNetwork(ID3D12GraphicsCommandList* list, uint32_t slot, uint32_t detWidth, uint32_t detHeight)
{
    return impl_->recordLayers(list, slot, nullptr, detWidth, detHeight);
}

// Same rule as OpenCV's FaceDetectorYN (dnn::NMSBoxes on Rect2i): boxes truncated
// to whole pixels, scores sorted descending with ties in anchor order, a box is
// dropped when its overlap with a kept box exceeds the threshold.
void YuNetGpu::nonMaxSuppression(std::vector<FaceBox>& boxes, float iouThreshold)
{
    std::sort(boxes.begin(), boxes.end(), [](const FaceBox& a, const FaceBox& b) { return a.score != b.score ? a.score > b.score : a.id < b.id; });
    struct Rect { int x, y, w, h; };
    const auto rect = [](const FaceBox& b) { return Rect{int(b.x), int(b.y), int(b.w), int(b.h)}; };
    std::vector<FaceBox> kept;
    for (const FaceBox& b : boxes) {
        const Rect r = rect(b);
        bool suppressed = false;
        for (const FaceBox& k : kept) {
            const Rect q = rect(k);
            const int x1 = std::max(r.x, q.x), y1 = std::max(r.y, q.y);
            const int x2 = std::min(r.x + r.w, q.x + q.w), y2 = std::min(r.y + r.h, q.y + q.h);
            const int inter = x2 > x1 && y2 > y1 ? (x2 - x1) * (y2 - y1) : 0;
            const int uni = r.w * r.h + q.w * q.h - inter;
            if (uni > 0 && float(inter) / float(uni) > iouThreshold) { suppressed = true; break; }
        }
        if (!suppressed) kept.push_back(b);
    }
    boxes = std::move(kept);
}

bool YuNetGpu::collect(uint32_t slot, std::vector<FaceBox>& faces, uint32_t* rawCandidates)
{
    Impl& m = *impl_;
    faces.clear();
    if (!m.ready || slot >= m.cfg.ringSlots) return false;
    void* mapped = nullptr;
    D3D12_RANGE range{0, size_t(m.candidateBytes)};
    if (FAILED(m.readbacks[slot]->Map(0, &range, &mapped)) || !mapped) return false;
    const auto* base = static_cast<const uint8_t*>(mapped);
    uint32_t count = 0;
    std::memcpy(&count, base, 4);
    if (rawCandidates) *rawCandidates = count;
    count = std::min(count, m.cfg.candidateCapacity);
    for (uint32_t i = 0; i < count; ++i) {
        float f[16];
        std::memcpy(f, base + 16 + size_t(i) * 64, 64);
        FaceBox b;
        b.x = f[0]; b.y = f[1]; b.w = f[2]; b.h = f[3];
        for (int k = 0; k < 10; ++k) b.landmarks[size_t(k)] = f[4 + k];
        b.score = f[14];
        std::memcpy(&b.id, &f[15], 4);
        if (std::isfinite(b.x + b.y + b.w + b.h + b.score) && b.w > 0 && b.h > 0) faces.push_back(b);
    }
    D3D12_RANGE none{0, 0};
    m.readbacks[slot]->Unmap(0, &none);
    nonMaxSuppression(faces, m.cfg.nmsThreshold);
    return true;
}

} // namespace veyra::beauty
