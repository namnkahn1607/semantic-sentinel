//
// Created by nlnk on Mar 5, 26.
//

#ifndef SENTINEL_ENGINE_CONSTANT_HH
#define SENTINEL_ENGINE_CONSTANT_HH

namespace engine {
inline constexpr int64_t G_SHUTDOWN_TIMEOUT = 5;
inline constexpr int64_t MAIN_THREAD_BLOCKED_ROUTINE = 100;

inline constexpr size_t VECTOR_DIM = 384;
inline constexpr size_t VECTOR_MEMSIZE = VECTOR_DIM * sizeof(float);

inline constexpr size_t L0_MAX_SLOTS = 1'000;
inline constexpr size_t L1_MAX_SLOTS = 500'000;
inline constexpr size_t BUFFER_PAYLOAD_SIZE = 512 * 1024 * 1024;

inline constexpr int64_t PENDING_LIFESPAN = 30;

inline constexpr uint32_t VALID_IDENTIFIER = 0xDEADBEEF;
inline constexpr size_t GC_LOW_WATERMARK = 256 * 1024 * 1024;
inline constexpr size_t GC_HIGH_WATERMARK = 435 * 1024 * 1024;
}  // namespace engine

#endif  // SENTINEL_ENGINE_CONSTANT_HH
