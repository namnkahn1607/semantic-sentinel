//
// Created by nlnk on Apr 16, 26.
//

#include "arena.hh"

#include <cstring>
#include <iostream>

#include "constant.hh"

void MetaNode::PackInfo(const uint32_t length, const uint32_t offset) {
    const uint32_t packed =
        (static_cast<uint64_t>(length) << 32) | static_cast<uint64_t>(offset);
    payload_info.store(packed, std::memory_order_relaxed);
}

void MetaNode::UnpackInfo(uint32_t& length, uint32_t& offset) const {
    const uint64_t packed = payload_info.load(std::memory_order_acquire);
    length = static_cast<uint32_t>(packed >> 32);
    offset = static_cast<uint32_t>(packed & 0xFFFFFFFF);
}

MemoryArena::MemoryArena() : write_head(0), read_tail(0) {
    // Allocating metadata Node array
    l0_metadata = new MetaNode[engine::L0_MAX_SLOTS];
    l1_metadata = new MetaNode[engine::L1_MAX_SLOTS];

    // Allocating Vector array
    l0_vectors = static_cast<float*>(
        std::aligned_alloc(32, engine::L0_MAX_SLOTS * engine::VECTOR_MEMSIZE));
    l1_vectors = static_cast<float*>(
        std::aligned_alloc(32, engine::L1_MAX_SLOTS * engine::VECTOR_MEMSIZE));

    // Allocating Ring Buffer payload array
    buffer_payload = static_cast<uint8_t*>(
        std::aligned_alloc(32, engine::BUFFER_PAYLOAD_SIZE));

    // Warming up by touching all pages to avoid Page Faults at runtime
    std::memset(l0_vectors, 0, engine::L0_MAX_SLOTS * engine::VECTOR_MEMSIZE);
    std::memset(l1_vectors, 0, engine::L1_MAX_SLOTS * engine::VECTOR_MEMSIZE);
    std::memset(buffer_payload, 0, engine::BUFFER_PAYLOAD_SIZE);

    for (size_t i = 0; i < engine::L0_MAX_SLOTS; ++i) {
        l0_metadata[i].state.store(NodeState::DEAD, std::memory_order_relaxed);
        l0_metadata[i].ref_bit.store(EvictState::COLD,
                                     std::memory_order_relaxed);
        l0_metadata[i].created_at.store(0, std::memory_order_relaxed);
        l0_metadata[i].payload_info.store(0, std::memory_order_relaxed);
    }

    for (size_t i = 0; i < engine::L1_MAX_SLOTS; ++i) {
        l1_metadata[i].state.store(NodeState::DEAD, std::memory_order_relaxed);
        l1_metadata[i].ref_bit.store(EvictState::COLD,
                                     std::memory_order_relaxed);
        l1_metadata[i].created_at.store(0, std::memory_order_relaxed);
        l1_metadata[i].payload_info.store(0, std::memory_order_relaxed);
    }

    std::cout << "[Vector Engine] Initialized Dual Memory Arena" << std::endl;
}

MemoryArena::~MemoryArena() {
    free(l0_vectors);
    free(l1_vectors);
    free(buffer_payload);

    delete[] l0_metadata;
    delete[] l1_metadata;
}

MetaNode& MemoryArena::GetL0Node(const size_t i) const {
    return l0_metadata[i];
}

MetaNode& MemoryArena::GetL1Node(const size_t i) const {
    return l1_metadata[i];
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

uint64_t MemoryArena::AllocatePayload(const size_t length) {
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
        uint32_t padding = 0;

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
