//
// Created by nlnk on Apr 16, 26.
//

#ifndef SENTINEL_ENGINE_ARENA_HH
#define SENTINEL_ENGINE_ARENA_HH

#include <atomic>

#include "node.hh"

// 12-byte Payload Header supporting O(1) lookup to Vector Arena.
struct alignas(4) PayloadHeader {
    uint32_t identifier;
    uint32_t node_id;
    uint32_t length;
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
    [[nodiscard]] inline uint64_t GetReadTail() const;

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
    std::atomic<uint64_t> read_tail;
};

#endif  // SENTINEL_ENGINE_ARENA_HH
