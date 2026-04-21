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

#endif  // SENTINEL_ENGINE_RAII_VECTOR_HH
