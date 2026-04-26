//
// Created by nlnk on Apr 26, 26.
//

#include <benchmark/benchmark.h>

#include <random>

#include "avx_math.hh"

constexpr uint32_t ALIGN = 32;
constexpr uint32_t DIM = 384;
constexpr uint32_t MEMSIZE = DIM * sizeof(float);

static void BenchLatency(benchmark::State& state) {
    alignas(ALIGN) float v1[DIM];
    alignas(ALIGN) float v2[DIM];
    std::ranges::fill(v1, 0.1f);
    std::ranges::fill(v2, 0.2f);

    for ([[maybe_unused]] auto _ : state) {
        benchmark::DoNotOptimize(v1);
        benchmark::DoNotOptimize(v2);

        float res = CosineSimilarity(v1, v2);
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations() * DIM);
}

BENCHMARK(BenchLatency)->Unit(benchmark::kNanosecond);

static void Bench1KBatch(benchmark::State& state) {
    constexpr uint32_t NUM_VECTORS = 1000;
    constexpr uint32_t TOTAL_FLOATS = DIM * NUM_VECTORS;

    auto* l0_cache =
        static_cast<float*>(_mm_malloc(TOTAL_FLOATS * sizeof(float), ALIGN));
    auto* query = static_cast<float*>(_mm_malloc(DIM * sizeof(float), ALIGN));

    std::mt19937 gen(42);  // NOLINT(cert-msc51-cpp)
    std::uniform_real_distribution dist(-1.0f, 1.0f);

    for (int32_t i = 0; i < TOTAL_FLOATS; ++i) {
        l0_cache[i] = dist(gen);
    }

    for (int32_t i = 0; i < DIM; ++i) {
        query[i] = dist(gen);
    }

    for ([[maybe_unused]] auto _ : state) {
        for (int32_t i = 0; i < NUM_VECTORS; ++i) {
            float* node_vec = l0_cache + (i * DIM);

            benchmark::DoNotOptimize(query);
            benchmark::DoNotOptimize(node_vec);

            float res = CosineSimilarity(query, node_vec);

            benchmark::DoNotOptimize(res);
        }
    }

    state.SetItemsProcessed(state.iterations() * NUM_VECTORS);
    state.SetBytesProcessed(
        // NOLINTNEXTLINE(cppcoreguidelines-narrowing-conversions)
        state.iterations() * TOTAL_FLOATS * sizeof(float));

    _mm_free(l0_cache);
    _mm_free(query);
}

BENCHMARK(Bench1KBatch)->Unit(benchmark::kNanosecond)->Iterations(10000);

BENCHMARK_MAIN();
