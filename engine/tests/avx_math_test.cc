//
// Created by nlnk on Apr 24, 26.
//

#include "avx_math.hh"

#include <grpc/grpc.h>
#include <gtest/gtest.h>
#include <immintrin.h>

#include <random>

constexpr int32_t ALIGN = 32;
constexpr int32_t DIM = 384;

class AVX2CosineTest : public ::testing::Test {
protected:
    float* query = nullptr;
    float* node_vector = nullptr;

    void SetUp() override {
        constexpr size_t bytes = DIM * sizeof(float);
        query = static_cast<float*>(_mm_malloc(bytes, ALIGN));
        node_vector = static_cast<float*>(_mm_malloc(bytes, ALIGN));
    }

    void TearDown() override {
        _mm_free(query);
        _mm_free(node_vector);
    }

    void GenerateNormalizedVectors() const {
        std::mt19937 gen(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        float norm_q = 0.0f;
        float norm_n = 0.0f;
        for (int32_t i = 0; i < DIM; ++i) {
            query[i] = dist(gen);
            node_vector[i] = dist(gen);
            norm_q += (query[i] * query[i]);
            norm_n += (node_vector[i] * node_vector[i]);
        }

        norm_q = std::sqrt(norm_q);
        norm_n = std::sqrt(norm_n);

        for (int32_t i = 0; i < DIM; ++i) {
            query[i] /= norm_q;
            node_vector[i] /= norm_n;
        }
    }
};

float ScalarCosineSimilarity(const float* query, const float* node_vector) {
    float sum = 0.0f;

    for (int32_t i = 0; i < DIM; ++i) {
        sum += (query[i] * node_vector[i]);
    }

    return sum;
}

TEST_F(AVX2CosineTest, IdenticalVectors) {
    GenerateNormalizedVectors();
    std::copy_n(query, DIM, node_vector);
    const float result = CosineSimilarity(query, node_vector);
    EXPECT_NEAR(result, 1.0f, 1e-4f);
}

TEST_F(AVX2CosineTest, OppositeVectors) {
    GenerateNormalizedVectors();
    for (int32_t i = 0; i < DIM; ++i) {
        node_vector[i] = -query[i];
    }

    const float result = CosineSimilarity(query, node_vector);
    EXPECT_NEAR(result, -1.0f, 1e-4f);
}

TEST_F(AVX2CosineTest, CompareWithScalarOracle) {
    for (int iter = 0; iter < 1000; ++iter) {
        GenerateNormalizedVectors();
        const float avx2_result = CosineSimilarity(query, node_vector);
        const float scalar_result = ScalarCosineSimilarity(query, node_vector);
        EXPECT_NEAR(avx2_result, scalar_result, 1e-4f);
    }
}
