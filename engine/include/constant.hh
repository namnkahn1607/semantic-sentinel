//
// Created by nlnk on Mar 5, 26.
//

#ifndef STRIX_ENGINE_CONSTANT_HH
#define STRIX_ENGINE_CONSTANT_HH

#include <cstdint>
#include <cstdlib>

namespace engine {
inline constexpr uint32_t G_SHUTDOWN_TIMEOUT = 5;
inline constexpr uint32_t MAIN_THREAD_BLOCKED_ROUTINE = 100;

inline constexpr size_t VECTOR_DIM = 384;
inline constexpr size_t VECTOR_MEMSIZE = VECTOR_DIM * sizeof(float);
inline constexpr float SIMILARITY_THRESHOLD = 0.85f;

inline constexpr size_t L0_MAX_SLOTS = 1'000;
inline constexpr size_t L1_MAX_SLOTS = 500'000;
inline constexpr size_t TOTAL_MAX_SLOTS = L0_MAX_SLOTS + L1_MAX_SLOTS;

inline constexpr uint32_t MAX_PAYLOAD_LENGTH = 0xFFFFFF;
inline constexpr uint64_t VIRTUAL_OFFSET_MASK = 0x1FFFFFFFFF;
inline constexpr size_t PAYLOAD_BUFFER_SIZE = 4ULL * 1024 * 1024 * 1024;

inline constexpr uint32_t PENDING_LIFESPAN = 30;

inline constexpr uint32_t VALID_IDENTIFIER = 0xDEADBEEF;

inline constexpr uint64_t LOW_WATERMARK_THRESHOLD = 2ULL * 1024 * 1024 * 1024;
inline constexpr uint32_t LOW_GC_SLEEP_MS = 10;
inline constexpr uint64_t HIGH_WATERMARK_THRESHOLD =
    (3ULL * 1024 + 512ULL) * 1024 * 1024;
inline constexpr uint32_t HIGH_GC_SLEEP_MS = 1;
}  // namespace engine

#endif  // STRIX_ENGINE_CONSTANT_HH
