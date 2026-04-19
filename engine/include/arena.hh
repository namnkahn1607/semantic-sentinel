//
// Created by nlnk on Apr 16, 26.
//

#ifndef SENTINEL_ENGINE_ARENA_HH
#define SENTINEL_ENGINE_ARENA_HH

#include <atomic>

enum class NodeState : uint8_t {
    DEAD = 0,
    PENDING = 1,
    READY = 2,
    MIGRATING = 3
};

enum class EvictState : uint8_t { COLD = 0, HOT = 1 };

// Avoid False Sharing using alignas(64) since cache line size for
// almost all modern x86 AMD and Intel CPUs is 64 bytes.
struct alignas(64) MetaNode {
    std::atomic<NodeState> state;
    std::atomic<EvictState> ref_bit;
    std::atomic<uint64_t> created_at;

    // 32-bit length & 32-bit offset
    std::atomic<uint64_t> payload_info;

    inline void PackInfo(uint32_t length, uint32_t offset);
    inline void UnpackInfo(uint32_t& length, uint32_t& offset) const;
};

class MemoryArena {
public:
    MemoryArena();
    ~MemoryArena();

    // No Copy Constructor & Copy Assignment Operator
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    // Getters
    [[nodiscard]] inline MetaNode& GetL0Node(size_t i) const;
    [[nodiscard]] inline MetaNode& GetL1Node(size_t i) const;
    [[nodiscard]] inline uint8_t* GetBufferPayload() const;
    [[nodiscard]] inline uint64_t GetWriteHead() const;

    // Setters
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
