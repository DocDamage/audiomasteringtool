#!/usr/bin/env python3
import argparse
from pathlib import Path

import onnx
from onnx import TensorProto, helper

parser = argparse.ArgumentParser()
parser.add_argument("output")
args = parser.parse_args()

output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)

x = helper.make_tensor_value_info("X", TensorProto.FLOAT, [1, 4])
y = helper.make_tensor_value_info("Y", TensorProto.FLOAT, [1, 4])
node = helper.make_node("Abs", ["X"], ["Y"], name="phase0_abs")
graph = helper.make_graph([node], "amt_phase0_abs", [x], [y])
model = helper.make_model(
    graph,
    producer_name="audiomasteringtool-phase0",
    opset_imports=[helper.make_opsetid("", 21)],
)
onnx.checker.check_model(model)
onnx.save(model, output)
print(output)
