//
// Created by nlnk on Apr 16, 26.
//

#ifndef SENTINEL_ENGINE_ARENA_HH
#define SENTINEL_ENGINE_ARENA_HH

#include <atomic>

// Avoid False Sharing using alignas(64) since cache line size for
// almost all modern x86 AMD and Intel CPUs is 64 bytes.
struct alignas(64) MetaNode {
    std::atomic<uint8_t> state;
    std::atomic<uint64_t> created_at;
    std::atomic<uint64_t> payload_offset;
};

enum class NodeState : uint8_t {
    DEAD = 0,
    PENDING = 1,
    READY = 2,
    MIGRATING = 3
};

class MemoryArena {
public:
    MemoryArena();
    ~MemoryArena();

    // No Copy Constructor & Copy Assignment Operator
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    [[nodiscard]] inline MetaNode& GetL0Node(size_t i) const;
    [[nodiscard]] inline MetaNode& GetL1Node(size_t i) const;
    [[nodiscard]] inline uint64_t GetWriteHead() const;

    inline uint64_t AllocatePayload(size_t length);

private:
    // Vector Arena
    MetaNode* l0_metadata;
    MetaNode* l1_metadata;
    float* l0_vectors;
    float* l1_vectors;

    // Payload Arena
    uint8_t* buffer_payload;
    std::atomic<uint64_t> write_head;
};

#endif  // SENTINEL_ENGINE_ARENA_HH
