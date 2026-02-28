#!/bin/bash

MODEL_DIR="engine/model"
MODEL_NAME="model_quint8_avx2.onnx"
MODEL_URL="https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/model_quint8_avx2.onnx"
EXPECTED_SHA256="b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21"

mkdir -p "$MODEL_DIR"
cd "$MODEL_DIR" || exit

echo "Downloading $MODEL_NAME..."
curl -L -o "$MODEL_NAME" "$MODEL_URL"

echo "Verifying checksum..."
ACTUAL_SHA256=$(sha256sum "$MODEL_NAME" | awk '{ print $1 }')

if [ "$EXPECTED_SHA256" != "$ACTUAL_SHA256" ]; then
    echo "ERROR: Checksum mismatch! File corrupted."
    rm "$MODEL_NAME"
    exit 1
fi

echo "Model downloaded and verified succesfully"