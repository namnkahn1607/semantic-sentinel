from pathlib import Path

from typing import cast
import numpy as np
from transformers import BertTokenizer
import onnxruntime as ort

ROOT        = Path(__file__).resolve().parents[1]
MODEL_PATH  = ROOT / "engine/model/model_quint8_avx2.onnx"

tokenizer = BertTokenizer.from_pretrained("sentence-transformers/all-MiniLM-L6-v2")
inputs = tokenizer("hello world", return_tensors="np", padding=True)

sess = ort.InferenceSession(MODEL_PATH)
output = cast(np.ndarray, sess.run(["last_hidden_state"], dict(inputs))[0]) # [1, seq_len, 384]
vec_a = output[0].mean(axis=0) # mean pooling → [384]
print("Pipeline A (Base model) - First 8 floats:\n", vec_a[:8])