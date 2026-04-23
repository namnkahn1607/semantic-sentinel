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

void MemoryArena::RunGarbageCollector(
    const std::atomic<bool>& g_shutdown_request) {
    while (!g_shutdown_request.load(std::memory_order_relaxed)) {
        const uint64_t head = write_head.load(std::memory_order_relaxed);
        const uint64_t tail = read_tail.load(std::memory_order_relaxed);

        if (const uint64_t used_space = head - tail;
            used_space < engine::LOW_WATERMARK_THRESHOLD) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(engine::LOW_GC_RATE));
            continue;
        } else if (used_space < engine::HIGH_WATERMARK_THRESHOLD) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(engine::HIGH_GC_RATE));
        }

        /* The Snowplow mechanism */
        const uint64_t tail_index = tail & (engine::BUFFER_PAYLOAD_SIZE - 1);

        // Perform leaping in case approaching buffer border
        if (const uint64_t padding = engine::BUFFER_PAYLOAD_SIZE - tail_index;
            padding < sizeof(PayloadHeader)) {
            read_tail.fetch_add(padding, std::memory_order_relaxed);
            continue;
        }

        // Read payload header
        const auto* header =
            reinterpret_cast<PayloadHeader*>(buffer_payload + tail_index);

        // Approach an invalid header identifier, advance tail by 1.
        if (header->identifier != engine::VALID_IDENTIFIER) {
            read_tail.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const uint32_t node_id = header->node_id;
        const uint32_t text_len = header->length;
        const uint32_t total_size = sizeof(PayloadHeader) + text_len;
        MetaNode& node = metadata[node_id];
        auto [state, ref_bit, length, offset] = node.LoadControl();

        if (state == NodeState::DEAD ||
            offset != static_cast<uint32_t>(tail & 0xFFFFFFFF)) {
            read_tail.fetch_add(total_size, std::memory_order_relaxed);
            continue;
        }

        if (ref_bit == EvictState::COLD) {
            // Evict in case encountering cold node
            uint64_t expected_ctrl =
                PackControl(state, ref_bit, length, offset);
            const uint64_t desired_ctrl =
                PackControl(NodeState::DEAD, ref_bit, length, offset);

            node.control_block.compare_exchange_weak(
                expected_ctrl, desired_ctrl, std::memory_order_release,
                std::memory_order_relaxed);

            node.created_at.store(0, std::memory_order_relaxed);
        } else {
            // Perform payload rescue & give it a Second chance
            // if the node is still hot.
            const uint64_t rescued_offset = AllocatePayload(text_len);
            const uint64_t rescued_index =
                rescued_offset & (engine::BUFFER_PAYLOAD_SIZE - 1);

            PayloadHeader new_header{engine::VALID_IDENTIFIER, node_id,
                                     text_len};
            std::memcpy(buffer_payload + rescued_index, &new_header,
                        sizeof(PayloadHeader));

            const uint64_t old_text_offset = tail + sizeof(PayloadHeader);
            const uint64_t new_text_offset =
                rescued_offset + sizeof(PayloadHeader);

            for (uint32_t i = 0; i < text_len; ++i) {
                const uint64_t src_idx =
                    (old_text_offset + i) & (engine::BUFFER_PAYLOAD_SIZE - 1);
                const uint64_t dst_idx =
                    (new_text_offset + i) & (engine::BUFFER_PAYLOAD_SIZE - 1);
                buffer_payload[dst_idx] = buffer_payload[src_idx];
            }

            uint64_t expected_ctrl =
                PackControl(state, EvictState::HOT, length, offset);
            const uint64_t desired_ctrl =
                PackControl(state, EvictState::COLD, length, rescued_offset);

            node.control_block.compare_exchange_strong(
                expected_ctrl, desired_ctrl, std::memory_order_release,
                std::memory_order_relaxed);
        }

        // Move read tail forward anyway
        read_tail.fetch_add(total_size, std::memory_order_relaxed);
    }
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
            throw std::runtime_error("Resource Exhausted");
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
