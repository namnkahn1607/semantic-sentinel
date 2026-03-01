"""
NOTE (C++ side): Once this model is loaded, client code must register custom ops:
  Ort::ThrowOnError(RegisterCustomOps(session_options, OrtGetApiBase()));
  // Link: libortextensions.so / onnxruntime_extensions.lib
"""

from pathlib import Path

import onnx
from onnx import compose
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
base_model.ir_version = tok_model.ir_version

tok_outputs = {o.name.lower(): o.name for o in tok_model.graph.output}
base_inputs = {i.name.lower(): i.name for i in base_model.graph.input}
io_map = [(tok_outputs[k], base_inputs[k]) for k in tok_outputs if k in base_inputs]

merged = compose.merge_models(tok_model, base_model, io_map=io_map)
onnx.checker.check_model(merged)
onnx.save(merged, OUTPUT_ONNX)

print(f"Saved: {OUTPUT_ONNX}")
print("Inputs :", [i.name for i in merged.graph.input])
print("Outputs:", [o.name for o in merged.graph.output])