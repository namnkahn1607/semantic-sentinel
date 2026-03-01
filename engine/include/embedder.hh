//
// Created by nlnk on Mar 1, 26.
//

#ifndef SENTINEL_ENGINE_EMBEDDER_HH
#define SENTINEL_ENGINE_EMBEDDER_HH

#include <onnxruntime/onnxruntime_cxx_api.h>

class Embedder {  // Meyers Singleton
public:
    // getInstance() now is thread-safe. If multiple calls to it are made,
    // they'll have to wait for initialization to complete.
    static Embedder& getInstance() {
        // Only get initialization once called
        static Embedder instance;  // C++11 Magic Statics (Thread-safe local
                                   // static initialization)
        return instance;
    }

    // Block Copy Constructor & Copy Assignment Operator
    Embedder(const Embedder&) = delete;
    Embedder& operator=(const Embedder&) = delete;

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;

    // Ort::Session has no default constructor, C++ will force construction in
    // initializer list if not declared as pointer => Use smart pointer.
    std::unique_ptr<Ort::Session> session_;

    Embedder();
    ~Embedder() = default;
};

#endif  // SENTINEL_ENGINE_EMBEDDER_HH
