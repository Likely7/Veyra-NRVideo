#include "veyra/beauty/OnnxLite.h"

#include <cstring>
#include <fstream>

namespace veyra::beauty {
namespace {

// Wire-level protobuf cursor. Every read is bounds checked; a malformed or
// truncated file turns into a clean error instead of an out-of-range access.
struct Reader {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool bad = false;
    bool more() const { return !bad && p < end; }
    uint64_t varint() {
        uint64_t value = 0;
        for (unsigned shift = 0; shift < 70; shift += 7) {
            if (p >= end) { bad = true; return 0; }
            const uint8_t byte = *p++;
            value |= uint64_t(byte & 0x7f) << shift;
            if (!(byte & 0x80)) return value;
        }
        bad = true;
        return 0;
    }
    Reader bytes() {
        const uint64_t length = varint();
        if (bad || length > uint64_t(end - p)) { bad = true; return {}; }
        Reader sub{p, p + length, false};
        p += length;
        return sub;
    }
    bool skip(uint32_t wire) {
        switch (wire) {
        case 0: varint(); break;
        case 1: if (end - p < 8) { bad = true; } else p += 8; break;
        case 2: (void)bytes(); break;
        case 5: if (end - p < 4) { bad = true; } else p += 4; break;
        default: bad = true;
        }
        return !bad;
    }
    std::string text() {
        const Reader sub = bytes();
        return std::string(reinterpret_cast<const char*>(sub.p), size_t(sub.end - sub.p));
    }
};

int64_t signedValue(uint64_t v) { return int64_t(v); }

// Repeated scalar fields come either packed (one length-delimited blob) or as
// individual varints; both encodings are valid in the ONNX schema.
void readInts(Reader& r, uint32_t wire, std::vector<int64_t>& out)
{
    if (wire == 2) {
        Reader sub = r.bytes();
        while (sub.more()) out.push_back(signedValue(sub.varint()));
        r.bad = r.bad || sub.bad;
    } else if (wire == 0) {
        out.push_back(signedValue(r.varint()));
    } else r.bad = true;
}

void readFloats(Reader& r, uint32_t wire, std::vector<float>& out)
{
    if (wire == 2) {
        Reader sub = r.bytes();
        if (sub.bad || ((sub.end - sub.p) % 4) != 0) { r.bad = true; return; }
        while (sub.p < sub.end) { float f; std::memcpy(&f, sub.p, 4); sub.p += 4; out.push_back(f); }
    } else if (wire == 5) {
        if (r.end - r.p < 4) { r.bad = true; return; }
        float f; std::memcpy(&f, r.p, 4); r.p += 4; out.push_back(f);
    } else r.bad = true;
}

bool parseTensor(Reader r, OnnxTensor& t, std::string& error)
{
    std::vector<uint8_t> raw;
    bool hasRaw = false;
    while (r.more()) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3), wire = uint32_t(key & 7);
        if (field == 1) readInts(r, wire, t.dims);
        else if (field == 2 && wire == 0) t.dataType = int32_t(r.varint());
        else if (field == 4) readFloats(r, wire, t.floats);
        else if (field == 7) readInts(r, wire, t.ints);
        else if (field == 8 && wire == 2) t.name = r.text();
        else if (field == 9 && wire == 2) {
            const Reader sub = r.bytes();
            raw.assign(sub.p, sub.end);
            hasRaw = true;
        } else if (field == 13) { error = "tensor uses external data"; return false; }
        else if (!r.skip(wire)) break;
    }
    if (r.bad) { error = "malformed tensor"; return false; }
    if (hasRaw) {
        if (t.dataType == 1) {
            if (raw.size() % 4) { error = "float tensor raw_data size"; return false; }
            t.floats.resize(raw.size() / 4);
            std::memcpy(t.floats.data(), raw.data(), raw.size());
        } else if (t.dataType == 7) {
            if (raw.size() % 8) { error = "int64 tensor raw_data size"; return false; }
            t.ints.resize(raw.size() / 8);
            std::memcpy(t.ints.data(), raw.data(), raw.size());
        }
    }
    return true;
}

bool parseAttribute(Reader r, OnnxNode& node, std::string& error)
{
    std::string name, text;
    std::vector<int64_t> ints;
    std::vector<float> floats;
    bool isInt = false, isFloat = false, isString = false;
    while (r.more()) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3), wire = uint32_t(key & 7);
        if (field == 1 && wire == 2) name = r.text();
        else if (field == 2) { readFloats(r, wire, floats); isFloat = true; }
        else if (field == 3) { readInts(r, wire, ints); isInt = true; }
        else if (field == 4 && wire == 2) { text = r.text(); isString = true; }
        else if (field == 7) { readFloats(r, wire, floats); isFloat = true; }
        else if (field == 8) { readInts(r, wire, ints); isInt = true; }
        else if (!r.skip(wire)) break;   // tensor/graph/type fields are not needed for this model family
    }
    if (r.bad) { error = "malformed attribute"; return false; }
    if (isInt) node.ints[name] = std::move(ints);
    if (isFloat) node.floats[name] = std::move(floats);
    if (isString) node.strings[name] = std::move(text);
    return true;
}

bool parseNode(Reader r, OnnxNode& node, std::string& error)
{
    while (r.more()) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3), wire = uint32_t(key & 7);
        if (field == 1 && wire == 2) node.inputs.push_back(r.text());
        else if (field == 2 && wire == 2) node.outputs.push_back(r.text());
        else if (field == 3 && wire == 2) node.name = r.text();
        else if (field == 4 && wire == 2) node.op = r.text();
        else if (field == 5 && wire == 2) { if (!parseAttribute(r.bytes(), node, error)) return false; }
        else if (!r.skip(wire)) break;
    }
    if (r.bad) { error = "malformed node"; return false; }
    return true;
}

bool parseValueInfo(Reader r, OnnxValueInfo& info, std::string& error)
{
    while (r.more()) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3), wire = uint32_t(key & 7);
        if (field == 1 && wire == 2) info.name = r.text();
        else if (field == 2 && wire == 2) {            // TypeProto
            Reader type = r.bytes();
            while (type.more()) {
                const uint64_t k2 = type.varint();
                const uint32_t f2 = uint32_t(k2 >> 3), w2 = uint32_t(k2 & 7);
                if (f2 == 1 && w2 == 2) {              // tensor_type
                    Reader tensor = type.bytes();
                    while (tensor.more()) {
                        const uint64_t k3 = tensor.varint();
                        const uint32_t f3 = uint32_t(k3 >> 3), w3 = uint32_t(k3 & 7);
                        if (f3 == 2 && w3 == 2) {      // shape
                            Reader shape = tensor.bytes();
                            while (shape.more()) {
                                const uint64_t k4 = shape.varint();
                                const uint32_t f4 = uint32_t(k4 >> 3), w4 = uint32_t(k4 & 7);
                                if (f4 == 1 && w4 == 2) {   // dim
                                    Reader dim = shape.bytes();
                                    int64_t value = -1;
                                    std::string symbol;
                                    while (dim.more()) {
                                        const uint64_t k5 = dim.varint();
                                        const uint32_t f5 = uint32_t(k5 >> 3), w5 = uint32_t(k5 & 7);
                                        if (f5 == 1 && w5 == 0) value = signedValue(dim.varint());
                                        else if (f5 == 2 && w5 == 2) symbol = dim.text();
                                        else if (!dim.skip(w5)) break;
                                    }
                                    info.dims.push_back(value);
                                    info.symbolicDims.push_back(symbol);
                                } else if (!shape.skip(w4)) break;
                            }
                        } else if (!tensor.skip(w3)) break;
                    }
                } else if (!type.skip(w2)) break;
            }
        } else if (!r.skip(wire)) break;
    }
    if (r.bad) { error = "malformed value info"; return false; }
    return true;
}

bool parseGraph(Reader r, OnnxModel& model, std::string& error)
{
    while (r.more()) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3), wire = uint32_t(key & 7);
        if (field == 1 && wire == 2) {
            OnnxNode node;
            if (!parseNode(r.bytes(), node, error)) return false;
            model.nodes.push_back(std::move(node));
        } else if (field == 5 && wire == 2) {
            OnnxTensor tensor;
            if (!parseTensor(r.bytes(), tensor, error)) return false;
            const std::string name = tensor.name;
            model.initializers[name] = std::move(tensor);
        } else if (field == 11 && wire == 2) {
            OnnxValueInfo info;
            if (!parseValueInfo(r.bytes(), info, error)) return false;
            model.inputs.push_back(std::move(info));
        } else if (field == 12 && wire == 2) {
            OnnxValueInfo info;
            if (!parseValueInfo(r.bytes(), info, error)) return false;
            model.outputs.push_back(std::move(info));
        } else if (!r.skip(wire)) break;
    }
    if (r.bad) { error = "malformed graph"; return false; }
    return true;
}

} // namespace

bool parseOnnx(const std::vector<uint8_t>& bytes, OnnxModel& model, std::string& error)
{
    model = {};
    if (bytes.size() < 16) { error = "file too small to be an ONNX model"; return false; }
    Reader r{bytes.data(), bytes.data() + bytes.size(), false};
    bool sawGraph = false;
    while (r.more()) {
        const uint64_t key = r.varint();
        const uint32_t field = uint32_t(key >> 3), wire = uint32_t(key & 7);
        if (field == 2 && wire == 2) model.producer = r.text();
        else if (field == 7 && wire == 2) {
            if (!parseGraph(r.bytes(), model, error)) return false;
            sawGraph = true;
        } else if (field == 8 && wire == 2) {
            Reader opset = r.bytes();
            std::string domain;
            int64_t version = 0;
            while (opset.more()) {
                const uint64_t k2 = opset.varint();
                const uint32_t f2 = uint32_t(k2 >> 3), w2 = uint32_t(k2 & 7);
                if (f2 == 1 && w2 == 2) domain = opset.text();
                else if (f2 == 2 && w2 == 0) version = signedValue(opset.varint());
                else if (!opset.skip(w2)) break;
            }
            if (domain.empty() || domain == "ai.onnx") model.opset = version;
        } else if (!r.skip(wire)) break;
    }
    if (r.bad) { error = "malformed model (truncated or not protobuf)"; return false; }
    if (!sawGraph || model.nodes.empty()) { error = "model has no graph"; return false; }
    return true;
}

bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { error = "cannot open " + path.string(); return false; }
    const std::streamsize size = file.tellg();
    if (size <= 0 || size > (256ll << 20)) { error = "unexpected model size"; return false; }
    bytes.resize(size_t(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) { error = "read failed"; return false; }
    return true;
}

} // namespace veyra::beauty
