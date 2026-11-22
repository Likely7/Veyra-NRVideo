# SPDX-License-Identifier: GPL-3.0-only
"""Build the RIFE models RifeDmlBackend loads from runtime/rife.

Input: the ONNX exports of Practical-RIFE (hzwer, MIT) that vs-mlrt publishes in
its external-models release, v2 layout (one 7-channel input: img0, img1,
timestep; the backwarp grid is computed inside the graph):
  https://github.com/AmusementClub/vs-mlrt/releases/tag/external-models
  rife_v4.26.7z       -> rife_v2/rife_v4.26.onnx       (2X)
  rife_v4.22_lite.7z  -> rife_v2/rife_v4.22_lite.onnx  (3X)

Changes, nothing else:
1. The 7-channel input becomes three inputs img0 [1,3,H,W], img1 [1,3,H,W] and
   timestep [1,1,H,W] joined by one Concat, so the backend binds its own D3D12
   buffers without a copy. H and W stay symbolic ("height", "width"); the
   backend fixes them per session (multiples of 64).
2. fp16 weights and arithmetic (onnxruntime.transformers.float16, inputs and
   output kept fp32). Shape and index ops stay fp32: converting them breaks
   the graph or moves it to the CPU under DirectML. On an RX 9070 XT at
   1920x1088 this takes RIFE 4.26 from 23.1 to 10.8 ms per frame.

  python make_rife_models.py <dir holding rife_v2/> <output dir>

Requires: onnx, onnxruntime (any build; only its float16 tool is used).
"""
import sys

import onnx
from onnx import TensorProto, helper
from onnxruntime.transformers.float16 import convert_float_to_float16

MODELS = ["rife_v4.26", "rife_v4.22_lite"]
KEEP_FP32 = ["Range", "Shape", "ConstantOfShape", "Expand", "Mod", "Equal", "Where", "ScatterND", "Gather", "Cast"]


def convert(source, target):
    model = onnx.load(source)
    graph = model.graph
    joined = graph.input[0].name
    graph.input.remove(graph.input[0])
    graph.input.extend(
        helper.make_tensor_value_info(name, TensorProto.FLOAT, [1, channels, "height", "width"])
        for name, channels in (("img0", 3), ("img1", 3), ("timestep", 1)))
    graph.node.insert(0, helper.make_node("Concat", ["img0", "img1", "timestep"], [joined], axis=1, name="veyra_input_concat"))
    graph.output[0].type.tensor_type.ClearField("shape")
    onnx.save(convert_float_to_float16(model, keep_io_types=True, op_block_list=KEEP_FP32), target)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    for name in MODELS:
        convert(f"{sys.argv[1]}/rife_v2/{name}.onnx", f"{sys.argv[2]}/{name}_fp16.onnx")
        print(f"{name}_fp16.onnx")


if __name__ == "__main__":
    main()
