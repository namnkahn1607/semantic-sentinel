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
            auto& curr_node = memory_arena.GetL0Node(i);
            const auto curr_state = static_cast<NodeState>(
                curr_node.state.load(std::memory_order_acquire));

            if (curr_state == NodeState::READY) {
                // TODO: AVX2 math here
            } else if (curr_state == NodeState::PENDING) {
                if (curr_time -
                        curr_node.created_at.load(std::memory_order_relaxed) >
                    engine::PENDING_LIFESPAN) {
                    curr_node.state.store(static_cast<uint8_t>(NodeState::DEAD),
                                          std::memory_order_release);
                    reusable_node_id = static_cast<int64_t>(i);
                }
            } else if (curr_state == NodeState::DEAD) {
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

bool SemanticServiceImpl::SetCache(const uint64_t node_id,
                                   const std::string& payload) const {
    const uint64_t new_offset = WriteRingBuffer(
        reinterpret_cast<const uint8_t*>(payload.data()), payload.length());

    uint64_t expected_offset =
        memory_arena.GetL0Node(node_id).payload_offset.load(
            std::memory_order_relaxed);

    return memory_arena.GetL0Node(node_id)
        .payload_offset.compare_exchange_strong(expected_offset, new_offset,
                                                std::memory_order_release,
                                                std::memory_order_relaxed);
}

uint64_t SemanticServiceImpl::WriteRingBuffer(const uint8_t* payload,
                                              const size_t length) const {
    const uint64_t offset = memory_arena.AllocatePayload(length);
    const uint64_t index = offset & (engine::BUFFER_PAYLOAD_SIZE - 1);

    if (const size_t space_until_end = engine::BUFFER_PAYLOAD_SIZE - index;
        length < space_until_end) {
        std::memcpy(memory_arena.GetBufferPayload() + index, payload, length);
    } else {
        const size_t chunk1_size = engine::BUFFER_PAYLOAD_SIZE - index;
        const size_t chunk2_size = length - chunk1_size;
        std::memcpy(memory_arena.GetBufferPayload() + index, payload,
                    chunk1_size);
        std::memcpy(memory_arena.GetBufferPayload(), payload + chunk1_size,
                    chunk2_size);
    }

    return offset;
}
