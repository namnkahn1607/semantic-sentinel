//
// Created by nlnk on Mar 1, 26.
//

#include "embedder.hh"

Embedder::Embedder() {
    const char* model_path = std::getenv("INFERENCE_MODEL_PATH");
    if (model_path == nullptr) {
        throw std::runtime_error(
            "Environment variable INFERENCE_MODEL_PATH is not set");
    }

    env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "onnx-env");

    session_options_ = Ort::SessionOptions();

    // Highest level of graph optimization
    session_options_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    session_options_.SetIntraOpNumThreads(1);

    session_ =
        std::make_unique<Ort::Session>(env_, model_path, session_options_);
}
