"""
NOTE (C++ side): Once this model is loaded, client code must register custom ops:
  Ort::ThrowOnError(RegisterCustomOps(session_options, OrtGetApiBase()));
  // Link: libortextensions.so / onnxruntime_extensions.lib
"""

from pathlib import Path

import numpy as np
import onnx
from onnx import compose, helper, numpy_helper
from transformers import BertTokenizer
from onnxruntime_extensions import gen_processing_models

ROOT        = Path(__file__).resolve().parents[1]
INPUT_ONNX  = ROOT / "engine/model/model_quint8_avx2.onnx"
OUTPUT_ONNX = INPUT_ONNX.parent / "sentinel-minilm-with-tokenizer.onnx"

# Directly use BertTokenizer (not AutoTokenizer) so that gen_processing_models
# recognizes the right class, avoiding "Unknown tokenizer".
tokenizer = BertTokenizer.from_pretrained("sentence-transformers/all-MiniLM-L6-v2")

# padding=True: padding is calculated on real length of sentences in batch,
# no longer hardcoded to 128 → avoid wasteful inference on padding tokens.
tok_model, _ = gen_processing_models(
    tokenizer,
    pre_kwargs={"max_length": 128, "padding": True, "truncation": True},
    post_kwargs=None, # type: ignore[arg-type]
)
assert tok_model is not None, "Can't create tokenizer ONNX - check tokenizer type"

base_model = onnx.load(INPUT_ONNX)
base_model.ir_version = tok_model.ir_version  # fix IR version mismatch

# ── Patch token_type_ids ───────────────────────────────────────────────────────
# model_quint8_avx2 have stripped token_type_ids embedding table down to 1 row
# (which is feasible with index 0 (FUCK)). BERT Tokenizer outputs token_type_ids = 1 with 
# sentence B as input.
# → Gather throw "idx=1 out of bounds [-1,0]".
# IDEA: inject a single node ConstantOfShape into tok_model to override output
# token_type_ids, becoming tensor zeros which shares the same shape as input_ids.
g = tok_model.graph

# Node 1: Take input_ids's shape → [batch, seq_len]
g.node.append(
    helper.make_node("Shape", inputs=["input_ids"], outputs=["_tti_shape"])
)
# Node 2: Create tensor zeros which has that shape
g.node.append(
    helper.make_node(
        "ConstantOfShape",
        inputs=["_tti_shape"],
        outputs=["_tti_zeros"],
        value=numpy_helper.from_array(np.array([0], dtype=np.int64)),
    )
)

# Redirect graph output "token_type_ids" → points to _tti_zeros
for out in g.output:
    if out.name == "token_type_ids":
        out.name = "_tti_zeros"

# Update all nodes using "token_type_ids" as input → Use "_tti_zeros"
# (actually there's none, but let's be a safe boy)
for node in g.node:
    for idx, inp in enumerate(node.input):
        if inp == "token_type_ids":
            node.input[idx] = "_tti_zeros"

tok_outputs = {o.name: o.name for o in tok_model.graph.output}
base_inputs = {i.name: i.name for i in base_model.graph.input}

# 1. Create initializer containing axis to insert (axis = 0)
axes_tensor = numpy_helper.from_array(np.array([0], dtype=np.int64), name="batch_axis")
g.initializer.append(axes_tensor)

# 2. Inject node Unsqueeze to all 3 tensors
g.node.append(helper.make_node("Unsqueeze", inputs=["input_ids", "batch_axis"], outputs=["input_ids_2d"]))
g.node.append(helper.make_node("Unsqueeze", inputs=["_tti_zeros", "batch_axis"], outputs=["_tti_zeros_2d"]))
g.node.append(helper.make_node("Unsqueeze", inputs=["attention_mask", "batch_axis"], outputs=["attention_mask_2d"]))

# 3. Register these tensor 2D being the official output of Tokenizer Graph
for out in g.output:
    if out.name == "input_ids":
        out.name = "input_ids_2d"
    elif out.name == "_tti_zeros":
        out.name = "_tti_zeros_2d"
    elif out.name == "attention_mask":
        out.name = "attention_mask_2d"

# 4. Modified io_map to join all tensor 2D into Base Model
io_map = [
    ("input_ids_2d",      "input_ids"),
    ("_tti_zeros_2d",     "token_type_ids"),
    ("attention_mask_2d", "attention_mask"),
]

merged = compose.merge_models(tok_model, base_model, io_map=io_map)
onnx.checker.check_model(merged)
onnx.save(merged, OUTPUT_ONNX)

print(f"Saved: {OUTPUT_ONNX}")
print("Inputs :", [i.name for i in merged.graph.input])
print("Outputs:", [o.name for o in merged.graph.output])
