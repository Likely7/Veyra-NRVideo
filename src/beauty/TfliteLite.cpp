#include "veyra/beauty/TfliteLite.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>

namespace veyra::beauty {
namespace {
// Flatbuffer cursor over the whole file. Every read is bounds checked; a malformed
// or truncated file turns into a clean error instead of an out-of-range access.
struct Buffer {
    const std::vector<uint8_t>& bytes;
    bool bad = false;

    bool fits(size_t offset, size_t size) const { return offset <= bytes.size() && size <= bytes.size() - offset; }
    template <typename T> T read(size_t offset)
    {
        T value{};
        if (!fits(offset, sizeof(T))) { bad = true; return value; }
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }
    // uoffset at `offset` -> absolute position of what it points to.
    size_t follow(size_t offset)
    {
        const uint32_t delta = read<uint32_t>(offset);
        if (bad || !fits(offset, size_t(delta))) { bad = true; return 0; }
        return offset + delta;
    }
};

// One flatbuffer table: field accessors by field id.
struct Table {
    Buffer* b = nullptr;
    size_t at = 0, vtable = 0;
    uint16_t vtableSize = 0;

    static Table open(Buffer& b, size_t at)
    {
        Table t;
        t.b = &b;
        t.at = at;
        const int32_t back = b.read<int32_t>(at);
        const int64_t vt = int64_t(at) - int64_t(back);
        if (b.bad || vt < 0 || !b.fits(size_t(vt), 4)) { b.bad = true; return t; }
        t.vtable = size_t(vt);
        t.vtableSize = b.read<uint16_t>(t.vtable);
        return t;
    }
    // Absolute position of a field, 0 when it is absent.
    size_t field(int id) const
    {
        if (!b || b->bad) return 0;
        const size_t slot = 4 + 2 * size_t(id);
        if (slot + 2 > vtableSize) return 0;
        const uint16_t offset = b->read<uint16_t>(vtable + slot);
        return offset ? at + offset : 0;
    }
    template <typename T> T scalar(int id, T fallback) const
    {
        const size_t f = field(id);
        return f ? b->read<T>(f) : fallback;
    }
    Table table(int id) const
    {
        const size_t f = field(id);
        if (!f) return {};
        return open(*b, b->follow(f));
    }
    // Vector of `id`: element count and the position of the first element.
    bool vector(int id, size_t& first, uint32_t& count, size_t elementSize) const
    {
        first = 0; count = 0;
        const size_t f = field(id);
        if (!f) return false;
        const size_t v = b->follow(f);
        count = b->read<uint32_t>(v);
        first = v + 4;
        if (b->bad || !b->fits(first, size_t(count) * elementSize)) { b->bad = true; count = 0; return false; }
        return true;
    }
    std::vector<int32_t> ints(int id) const
    {
        size_t first; uint32_t count;
        std::vector<int32_t> out;
        if (!vector(id, first, count, 4)) return out;
        out.resize(count);
        for (uint32_t i = 0; i < count; ++i) out[i] = b->read<int32_t>(first + 4 * size_t(i));
        return out;
    }
    std::string string(int id) const
    {
        size_t first; uint32_t count;
        if (!vector(id, first, count, 1)) return {};
        return std::string(reinterpret_cast<const char*>(b->bytes.data() + first), count);
    }
    Table element(int id, uint32_t index) const
    {
        size_t first; uint32_t count;
        if (!vector(id, first, count, 4) || index >= count) return {};
        return open(*b, b->follow(first + 4 * size_t(index)));
    }
    uint32_t count(int id) const
    {
        size_t first; uint32_t n;
        vector(id, first, n, 4);
        return n;
    }
    bool valid() const { return b && !b->bad; }
};

float halfToFloat(uint16_t h)
{
    const uint32_t sign = uint32_t(h >> 15) << 31;
    uint32_t exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff, bits;
    if (exponent == 0) {
        if (mantissa == 0) bits = sign;
        else {                                  // subnormal: renormalise
            exponent = 113;
            while (!(mantissa & 0x400)) { mantissa <<= 1; --exponent; }
            bits = sign | (exponent << 23) | ((mantissa & 0x3ff) << 13);
        }
    } else if (exponent == 31) bits = sign | 0x7f800000u | (mantissa << 13);
    else bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
} // namespace

bool loadTflite(const std::filesystem::path& path, TfliteModel& model, std::string& error)
{
    model = {};
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "cannot open " + path.filename().string(); return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() < 16 || bytes.size() > (size_t(1) << 30)) { error = "unexpected model file size"; return false; }
    if (std::memcmp(bytes.data() + 4, "TFL3", 4) != 0) { error = "not a TFLite model (identifier)"; return false; }
    Buffer b{bytes};
    const Table root = Table::open(b, b.follow(0));
    model.version = root.scalar<uint32_t>(0, 0);
    if (!root.valid() || model.version != 3) { error = std::format("unsupported TFLite schema version {}", model.version); return false; }

    // Operator codes: TFLite takes the larger of the builtin code (field 3) and the
    // deprecated byte (field 0) that older files fill instead.
    std::vector<int32_t> codes;
    for (uint32_t i = 0; i < root.count(1); ++i) {
        const Table oc = root.element(1, i);
        codes.push_back(std::max(oc.scalar<int32_t>(3, 0), int32_t(oc.scalar<int8_t>(0, 0))));
    }
    if (root.count(2) != 1) { error = "expected exactly one subgraph"; return false; }
    const Table graph = root.element(2, 0);
    const uint32_t bufferCount = root.count(4);
    const uint32_t tensorCount = graph.count(0);
    model.tensors.resize(tensorCount);
    for (uint32_t i = 0; i < tensorCount; ++i) {
        const Table t = graph.element(0, i);
        TfliteTensor& out = model.tensors[i];
        out.shape = t.ints(0);
        out.type = t.scalar<int8_t>(1, 0);
        out.name = t.string(3);
        const uint32_t buffer = t.scalar<uint32_t>(2, 0);
        if (buffer >= bufferCount) { error = "tensor buffer index out of range"; return false; }
        if (t.field(4)) {
            const Table q = t.table(4);
            if (q.count(0) || q.count(1) || q.count(2) || q.count(3)) { error = "quantized tensor " + out.name + " is not supported"; return false; }
        }
        const Table data = root.element(4, buffer);
        size_t first = 0; uint32_t size = 0;
        if (data.valid() && data.vector(0, first, size, 1) && size) {
            const size_t n = out.elementCount();
            if (out.type == 0 && size_t(size) == n * 4) {
                out.floats.resize(n);
                std::memcpy(out.floats.data(), bytes.data() + first, n * 4);
            } else if (out.type == 1 && size_t(size) == n * 2) {
                out.floats.resize(n);
                for (size_t k = 0; k < n; ++k) out.floats[k] = halfToFloat(b.read<uint16_t>(first + 2 * k));
            } else if (out.type == 2 && size_t(size) == n * 4) {
                out.ints.resize(n);
                std::memcpy(out.ints.data(), bytes.data() + first, n * 4);
            } else { error = "constant " + out.name + " has an unsupported type or size"; return false; }
        }
    }
    model.inputs = graph.ints(1);
    model.outputs = graph.ints(2);
    const auto inRange = [&](int32_t index) { return index >= -1 && index < int32_t(tensorCount); };
    for (uint32_t i = 0; i < graph.count(3); ++i) {
        const Table op = graph.element(3, i);
        const uint32_t code = op.scalar<uint32_t>(0, 0);
        if (code >= codes.size()) { error = "operator code index out of range"; return false; }
        TfliteOperator o;
        o.inputs = op.ints(1);
        o.outputs = op.ints(2);
        for (const auto t : o.inputs) if (!inRange(t)) { error = "operator input out of range"; return false; }
        for (const auto t : o.outputs) if (t < 0 || t >= int32_t(tensorCount)) { error = "operator output out of range"; return false; }
        const int32_t builtin = codes[code];
        switch (builtin) {
        case 0: case 3: case 4: case 6: case 17: case 34: case 54: o.op = TfliteOp(builtin); break;
        default: error = std::format("unsupported operator (builtin code {})", builtin); return false;
        }
        const Table options = op.field(4) ? op.table(4) : Table{};
        if (options.valid()) {
            if (o.op == TfliteOp::Conv2D) {
                o.samePadding = options.scalar<int8_t>(0, 0) == 0;
                o.strideW = options.scalar<int32_t>(1, 1); o.strideH = options.scalar<int32_t>(2, 1);
                o.activation = options.scalar<int8_t>(3, 0);
                o.dilationW = options.scalar<int32_t>(4, 1); o.dilationH = options.scalar<int32_t>(5, 1);
            } else if (o.op == TfliteOp::DepthwiseConv2D) {
                o.samePadding = options.scalar<int8_t>(0, 0) == 0;
                o.strideW = options.scalar<int32_t>(1, 1); o.strideH = options.scalar<int32_t>(2, 1);
                o.depthMultiplier = options.scalar<int32_t>(3, 1);
                o.activation = options.scalar<int8_t>(4, 0);
                o.dilationW = options.scalar<int32_t>(5, 1); o.dilationH = options.scalar<int32_t>(6, 1);
            } else if (o.op == TfliteOp::MaxPool2D) {
                o.samePadding = options.scalar<int8_t>(0, 0) == 0;
                o.strideW = options.scalar<int32_t>(1, 1); o.strideH = options.scalar<int32_t>(2, 1);
                o.filterW = options.scalar<int32_t>(3, 1); o.filterH = options.scalar<int32_t>(4, 1);
                o.activation = options.scalar<int8_t>(5, 0);
            } else if (o.op == TfliteOp::Add) {
                o.activation = options.scalar<int8_t>(0, 0);
            }
        }
        if (o.op == TfliteOp::Dequantize) {
            // A constant float16 tensor widened at load time: fold it.
            if (o.inputs.size() != 1 || o.outputs.size() != 1 || o.inputs[0] < 0 || model.tensors[size_t(o.inputs[0])].floats.empty()) {
                error = "DEQUANTIZE of a non-constant tensor is not supported"; return false;
            }
            auto& target = model.tensors[size_t(o.outputs[0])];
            target.floats = model.tensors[size_t(o.inputs[0])].floats;
            target.type = 0;
            continue;
        }
        model.operators.push_back(std::move(o));
    }
    if (b.bad) { error = "truncated or malformed TFLite file"; return false; }
    if (model.inputs.size() != 1 || model.outputs.empty()) { error = "unexpected graph inputs / outputs"; return false; }
    return true;
}

} // namespace veyra::beauty
