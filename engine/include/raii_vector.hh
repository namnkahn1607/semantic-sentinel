//
// Created by nlnk on Apr 21, 26.
//

#ifndef SENTINEL_ENGINE_RAII_VECTOR_HH
#define SENTINEL_ENGINE_RAII_VECTOR_HH

#include <memory>

struct AlignedFree {
    void operator()(void* ptr) const { std::free(ptr); }
};

using AlignedVector = std::unique_ptr<float[], AlignedFree>;

[[nodiscard]] inline AlignedVector NewAlignedVector(const size_t dim) {
    void* ptr = std::aligned_alloc(32, dim * sizeof(float));

    if (ptr == nullptr) {
        throw std::bad_alloc();
    }

    return AlignedVector{static_cast<float*>(ptr)};
}

#endif  // SENTINEL_ENGINE_RAII_VECTOR_HH
