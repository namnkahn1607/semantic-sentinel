//
// Created by nlnk on Mar 1, 26.
//

#include "embedder.hh"

Embedder::Embedder() {
    const char* model_path = std::getenv("INFERENCE_MODEL_PATH");
    const char* ext_path = std::getenv("ORT_EXTENSIONS_PATH");

    if (model_path == nullptr || ext_path == nullptr) {
        throw std::runtime_error(
            "Environment variables INFERENCE_MODEL_PATH or ORT_EXTENSIONS_PATH "
            "are not set");
    }

    env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "onnx-env");

    session_options_ = Ort::SessionOptions();

    // Highest level of graph optimization
    session_options_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    session_options_.SetIntraOpNumThreads(1);

    try {
        session_options_.RegisterCustomOpsLibrary(ext_path);
    } catch (const Ort::Exception& e) {
        throw std::runtime_error(
            std::string("Failed to load custom ops library: ") + e.what());
    }

    session_ =
        std::make_unique<Ort::Session>(env_, model_path, session_options_);
}

std::vector<float> Embedder::Encode(const std::string& prompt) const {
    // 1. Initialize standard allocator for ONNX
    Ort::MemoryInfo mem_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const Ort::AllocatorWithDefaultOptions allocator;

    // 2. Define Tensor Input structure
    const std::vector<int64_t> input_shape = {1};
    const char* input_string = prompt.c_str();

    // 3. Create String Tensor
    Ort::Value input_tensor = Ort::Value::CreateTensor(
        allocator, input_shape.data(), input_shape.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING);
    input_tensor.FillStringTensor(&input_string, 1);

    // 4. Prepare Run configurations
    const char* input_names[] = {"text"};
    const char* output_names[] = {"last_hidden_state"};

    // 5. Use Neural Network to execute the Input Tensor
    const auto output_tensors =
        session_->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
                      output_names, 1);

    // 6. Extract Output Data
    const Ort::Value& output_tensor = output_tensors.front();
    const auto type_info = output_tensor.GetTensorTypeAndShapeInfo();
    // Output shape is always [1, N, 384].
    const std::vector<int64_t> output_shape = type_info.GetShape();

    const int64_t seq_length = output_shape[1];  // number of tokens in sequence
    const int64_t vec_dimension = output_shape[2];  // vector dimension: 384

    if (seq_length == 0) {
        throw std::runtime_error(
            "Sequence length is 0. Cannot compute mean pooling.");
    }

    const auto* float_array = output_tensor.GetTensorData<float>();

    // 7. Squeeze 2D array [N][384] into [384] array using Mean Pooling
    std::vector pooled_vector(vec_dimension, 0.0f);

    for (int64_t i = 0; i < seq_length; ++i) {
        for (int64_t j = 0; j < vec_dimension; ++j) {
            pooled_vector[j] += float_array[i * vec_dimension + j];
        }
    }

    const auto seq_len_f = static_cast<float>(seq_length);
    for (float& val : pooled_vector) {
        val /= seq_len_f;
    }

    return pooled_vector;
}

float Embedder::CosineSimilarity(const std::vector<float>& vec_a,
                                 const std::vector<float>& vec_b) {
    constexpr int32_t VECTOR_SIZE = 384;

    if (vec_a.size() != VECTOR_SIZE || vec_b.size() != VECTOR_SIZE) {
        throw std::runtime_error("Wrong dimension. Vector size must be 384.");
    }

    float dot_product = 0.0f;
    float norm_a_sq = 0.0f;
    float norm_b_sq = 0.0f;

    for (int32_t i = 0; i < VECTOR_SIZE; ++i) {
        dot_product += vec_a[i] * vec_b[i];
        norm_a_sq += vec_a[i] * vec_a[i];
        norm_b_sq += vec_b[i] * vec_b[i];
    }

    if (norm_a_sq == 0.0f || norm_b_sq == 0.0f) {  // Handle empty vectors
        return 0.0f;
    }

    return dot_product / (std::sqrt(norm_a_sq) * std::sqrt(norm_b_sq));
}
