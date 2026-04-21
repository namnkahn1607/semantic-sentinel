//
// Created by nlnk on Apr 17, 26.
//

#include "service.hh"

#include "arena.hh"
#include "constant.hh"

SemanticServiceImpl::SemanticServiceImpl(MemoryArena& arena)
    : memory_arena(arena) {}

grpc::Status SemanticServiceImpl::CheckCache(
    [[maybe_unused]] grpc::ServerContext* context,
    const proto::CheckCacheRequest* request,
    [[maybe_unused]] proto::CheckCacheResponse* response) {
    try {
        if (request->prompt_text().empty()) {
            return {grpc::StatusCode::INVALID_ARGUMENT,
                    "User prompt is empty."};
        }

        // Get current time ONCE at every CheckCache() routine.
        const auto duration =
            std::chrono::system_clock::now().time_since_epoch();
        const auto secs =
            std::chrono::duration_cast<std::chrono::seconds>(duration);
        const auto curr_time = static_cast<uint64_t>(secs.count());

        int64_t reusable_node_id = -1;

        for (size_t i = 0; i < engine::L0_MAX_SLOTS; ++i) {
            auto& curr_node = memory_arena.GetNode(i);

            if (auto [state, ref_bit, length, offset] = curr_node.LoadControl();
                state == NodeState::READY) {
                // TODO: AVX2 math here
            } else if (state == NodeState::PENDING) {
                if (curr_time -
                        curr_node.created_at.load(std::memory_order_relaxed) >
                    engine::PENDING_LIFESPAN) {
                    uint64_t expected =
                        curr_node.control_block.load(std::memory_order_relaxed);
                    const uint64_t desired = ControlGenerator(
                        NodeState::DEAD, ref_bit, length, offset);

                    if (curr_node.control_block.compare_exchange_strong(
                            expected, desired, std::memory_order_release,
                            std::memory_order_relaxed)) {
                        reusable_node_id = static_cast<int64_t>(i);
                    }
                }
            } else if (state == NodeState::DEAD) {
                reusable_node_id = static_cast<int64_t>(i);
            }
        }

        if (reusable_node_id == -1) {
            // TODO: Compaction from L0 to L1 Buffer
        }

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        return {grpc::StatusCode::INTERNAL,
                std::string("[Vector Engine] Encounter error: ") + e.what()};
    } catch (...) {
        return {grpc::StatusCode::INTERNAL,
                "[Vector Engine] Unknown Fatal error"};
    }
}

bool SemanticServiceImpl::SetCache(const uint32_t node_id,
                                   const std::string& payload) const {
    if (payload.length() > engine::BUFFER_PAYLOAD_SIZE) {
        throw std::runtime_error("[Vector Engine] Oversized Payload");
    }

    const auto payload_len = static_cast<uint32_t>(payload.length());
    auto& [created_at, control_block] = memory_arena.GetNode(node_id);

    const uint64_t new_offset = WriteRingBuffer(
        node_id, reinterpret_cast<const uint8_t*>(payload.data()), payload_len);

    const uint64_t desired_control = ControlGenerator(
        NodeState::READY, EvictState::HOT, payload_len, new_offset);
    uint64_t expected_control = control_block.load(std::memory_order_relaxed);

    while (true) {
        if (static_cast<uint8_t>(expected_control >> 62) !=
            static_cast<uint8_t>(NodeState::PENDING)) {
            return false;
        }

        if (control_block.compare_exchange_weak(
                expected_control, desired_control, std::memory_order_release,
                std::memory_order_relaxed)) {
            return true;
        }
    }
}

uint64_t SemanticServiceImpl::WriteRingBuffer(const uint32_t node_id,
                                              const uint8_t* payload,
                                              const uint32_t length) const {
    const uint64_t header_offset = memory_arena.AllocatePayload(length);
    const uint64_t header_index =
        header_offset & (engine::BUFFER_PAYLOAD_SIZE - 1);

    // Create and write payload header
    const PayloadHeader header{engine::VALID_IDENTIFIER, node_id, length};
    std::memcpy(memory_arena.GetBufferPayload() + header_index, &header,
                sizeof(PayloadHeader));

    // Now write the payload text
    const uint64_t text_index = (header_index + sizeof(PayloadHeader)) &
                                (engine::BUFFER_PAYLOAD_SIZE - 1);

    if (engine::BUFFER_PAYLOAD_SIZE - text_index >= length) {
        std::memcpy(memory_arena.GetBufferPayload() + text_index, payload,
                    length);
    } else {
        const size_t chunk1_size = engine::BUFFER_PAYLOAD_SIZE - text_index;
        const size_t chunk2_size = length - chunk1_size;
        std::memcpy(memory_arena.GetBufferPayload() + text_index, payload,
                    chunk1_size);
        std::memcpy(memory_arena.GetBufferPayload(), payload + chunk1_size,
                    chunk2_size);
    }

    return header_offset;
}
