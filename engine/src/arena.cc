//
// Created by nlnk on Apr 16, 26.
//

#include "arena.hh"

#include <bits/this_thread_sleep.h>

#include <cstring>
#include <iostream>

#include "constant.hh"

MemoryArena::MemoryArena() : write_head(0), read_tail(0) {
    // Allocating metadata Node array
    metadata = new MetaNode[engine::TOTAL_MAX_SLOTS];

    // Allocating Vector array
    vectors = static_cast<float*>(std::aligned_alloc(
        32, engine::TOTAL_MAX_SLOTS * engine::VECTOR_MEMSIZE));

    // Allocating Ring Buffer payload array
    buffer_payload = static_cast<uint8_t*>(
        std::aligned_alloc(32, engine::BUFFER_PAYLOAD_SIZE));

    // Warming up by touching all pages to avoid Page Faults at runtime
    std::memset(vectors, 0, engine::TOTAL_MAX_SLOTS * engine::VECTOR_MEMSIZE);
    std::memset(buffer_payload, 0, engine::BUFFER_PAYLOAD_SIZE);

    for (size_t i = 0; i < engine::TOTAL_MAX_SLOTS; ++i) {
        metadata[i].created_at.store(0, std::memory_order_relaxed);
        metadata[i].control_block.store(0, std::memory_order_relaxed);
    }

    std::cout << "[Vector Engine] Initialized Dual Memory Arena" << std::endl;
}

MemoryArena::~MemoryArena() {
    free(vectors);
    free(buffer_payload);
    delete[] metadata;
}

MetaNode& MemoryArena::GetNode(const size_t node_id) const {
    return metadata[node_id];
}

float* MemoryArena::GetVector(const size_t node_id) const {
    return vectors + (engine::VECTOR_DIM * node_id);
}

uint8_t* MemoryArena::GetBufferPayload() const {
    return buffer_payload;
}

uint64_t MemoryArena::GetWriteHead() const {
    return write_head.load(std::memory_order_acquire);
}

uint64_t MemoryArena::GetReadTail() const {
    return read_tail.load(std::memory_order_acquire);
}

uint64_t MemoryArena::AllocatePayload(const uint32_t length) {
    const size_t total_size = sizeof(PayloadHeader) + length;
    uint64_t curr_write = write_head.load(std::memory_order_relaxed);
    uint64_t allocated_offset;

    while (true) {
        // Backpressure mechanism: System memory resource exhausted
        if (curr_write + total_size -
                read_tail.load(std::memory_order_relaxed) >=
            engine::BUFFER_PAYLOAD_SIZE) {
            throw std::runtime_error("[Vector Engine] Resource Exhausted");
        }

        const uint64_t actual_index =
            curr_write & (engine::BUFFER_PAYLOAD_SIZE - 1);
        uint64_t padding = 0;

        // Wrap-around Payload Header protection
        if (engine::BUFFER_PAYLOAD_SIZE - actual_index <
            sizeof(PayloadHeader)) {
            padding = engine::BUFFER_PAYLOAD_SIZE - actual_index;
        }

        allocated_offset = curr_write + padding;

        if (const uint64_t next_write = allocated_offset + total_size;
            write_head.compare_exchange_weak(curr_write, next_write,
                                             std::memory_order_relaxed)) {
            break;
        }
    }

    return allocated_offset;
}
