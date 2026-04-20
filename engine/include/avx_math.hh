//
// Created by nlnk on Apr 20, 26.
//

#ifndef SENTINEL_ENGINE_AVX_MATH_HH
#define SENTINEL_ENGINE_AVX_MATH_HH

#include <cmath>

// 1. AMD/Intel x86
#if defined(__x86_64__) || defined(_M_X64)

#include <immintrin.h>

// Cautious: both 'query' and 'node_vector' must be normalized!
inline float CosineSimilarity(const float* query, const float* node_vector) {
    // A 256-bit register holding eight 0.0f's
    __m256 sum_vec = _mm256_setzero_ps();

    for (int i = 0; i < 384; i += 8) {
        // Load 8 floats onto the register
        const __m256 q = _mm256_load_ps(query + i);
        const __m256 n = _mm256_load_ps(node_vector + i);
        // Fused Multiply-Add
        sum_vec = _mm256_fmadd_ps(q, n, sum_vec);
    }

    // AVX2 divides 256-bit into 2 128-bit lanes
    const __m128 sum_low = _mm256_castps256_ps128(sum_vec);
    const auto sum_high = _mm256_extractf128_ps(sum_vec, 1);

    // Sum 2 128-bit lane into a 128-bit (4 floats) register
    __m128 sum_128 = _mm_add_ps(sum_low, sum_high);

    // Continue horizontal add on that 128-bit
    sum_128 = _mm_hadd_ps(sum_128, sum_128);  // 2 floats
    sum_128 = _mm_hadd_ps(sum_128, sum_128);  // 1 duplicated float

    return _mm_cvtss_f32(sum_128);
}

#endif

#endif  // SENTINEL_ENGINE_AVX_MATH_HH
