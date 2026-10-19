#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace veyra::beauty {

// A deliberately small ONNX reader: just enough protobuf to read the graph of a
// compact CNN (nodes, attributes, float/int64 initializers, graph inputs and
// outputs). It is not a general ONNX implementation: anything it does not
// understand is rejected with a message, never guessed. Used by the face
// detector to read the model file that ships next to the program (the model is
// never part of the source tree).
struct OnnxTensor {
    std::string name;
    std::vector<int64_t> dims;
    int32_t dataType = 0;            // ONNX TensorProto.DataType (1 = float, 7 = int64)
    std::vector<float> floats;       // filled for float tensors
    std::vector<int64_t> ints;       // filled for int64 tensors
    size_t elementCount() const {
        size_t n = 1;
        for (const auto d : dims) n *= size_t(d < 0 ? 0 : d);
        return n;
    }
};

struct OnnxNode {
    std::string op, name;
    std::vector<std::string> inputs, outputs;
    std::map<std::string, std::vector<int64_t>> ints;   // INT and INTS attributes
    std::map<std::string, std::vector<float>> floats;   // FLOAT and FLOATS attributes
    std::map<std::string, std::string> strings;
    int64_t intOr(const std::string& key, int64_t fallback) const {
        const auto it = ints.find(key);
        return it == ints.end() || it->second.empty() ? fallback : it->second.front();
    }
};

struct OnnxValueInfo {
    std::string name;
    std::vector<int64_t> dims;                 // -1 for symbolic dimensions
    std::vector<std::string> symbolicDims;     // "" for fixed dimensions
};

struct OnnxModel {
    int64_t opset = 0;                          // default-domain opset
    std::string producer;
    std::vector<OnnxNode> nodes;
    std::unordered_map<std::string, OnnxTensor> initializers;
    std::vector<OnnxValueInfo> inputs, outputs;
};

bool parseOnnx(const std::vector<uint8_t>& bytes, OnnxModel& model, std::string& error);
bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error);

} // namespace veyra::beauty
