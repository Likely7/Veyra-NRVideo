#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace veyra::beauty {

// A deliberately small TFLite reader: just enough of the flatbuffer schema to read
// a compact float CNN (tensors with their constant data, the builtin operators of
// such a network with their convolution / pooling options, graph inputs and
// outputs). Float16 constants are widened to float32 and DEQUANTIZE operators on
// constants are folded away. Anything it does not understand is rejected with a
// message, never guessed. Used by the face mesh to read the model file that ships
// next to the program (the model is never part of the source tree).
struct TfliteTensor {
    std::string name;
    std::vector<int32_t> shape;
    int32_t type = 0;                    // TensorType: 0 float32, 1 float16, 2 int32
    std::vector<float> floats;           // float constants (widened); empty for activations
    std::vector<int32_t> ints;           // int32 constants (paddings)
    bool constant() const { return !floats.empty() || !ints.empty(); }
    size_t elementCount() const
    {
        size_t n = 1;
        for (const auto d : shape) n *= size_t(d < 0 ? 0 : d);
        return n;
    }
};

enum class TfliteOp : int32_t { Add = 0, Conv2D = 3, DepthwiseConv2D = 4, Dequantize = 6, MaxPool2D = 17, Pad = 34, Prelu = 54 };

struct TfliteOperator {
    TfliteOp op = TfliteOp::Add;
    std::vector<int32_t> inputs, outputs;   // tensor indices, -1 = optional input left out
    bool samePadding = true;
    int32_t strideW = 1, strideH = 1;
    int32_t filterW = 0, filterH = 0;       // pooling
    int32_t depthMultiplier = 1;
    int32_t dilationW = 1, dilationH = 1;
    int32_t activation = 0;                 // fused activation: 0 none
};

struct TfliteModel {
    uint32_t version = 0;
    std::vector<TfliteTensor> tensors;
    std::vector<TfliteOperator> operators;  // constant DEQUANTIZE operators are folded away
    std::vector<int32_t> inputs, outputs;
};

bool loadTflite(const std::filesystem::path& path, TfliteModel& model, std::string& error);

} // namespace veyra::beauty
