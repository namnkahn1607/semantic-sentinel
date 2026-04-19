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
    [[nodiscard]] inline MetaNode& GetNode(size_t node_id) const;
    [[nodiscard]] inline float* GetVector(size_t node_id) const;

    [[nodiscard]] inline uint8_t* GetBufferPayload() const;
    [[nodiscard]] inline uint64_t GetWriteHead() const;
    [[nodiscard]] inline uint64_t GetReadTail() const;

    // Setters
    uint64_t AllocatePayload(uint32_t length);

private:
    // Vector Arena
    MetaNode* metadata;
    float* vectors;

    // Payload Arena
    uint8_t* buffer_payload;
    std::atomic<uint64_t> write_head;
    std::atomic<uint64_t> read_tail;
};

#endif  // SENTINEL_ENGINE_ARENA_HH
