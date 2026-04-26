//
// Created by nlnk on Apr 17, 26.
//

#include "service.hh"

#include "arena.hh"
#include "avx_math.hh"
#include "constant.hh"
#include "embedder.hh"
#include "raii_vector.hh"

SemanticServiceImpl::SemanticServiceImpl(MemoryArena& arena)
    : memory_arena(arena) {}

grpc::Status SemanticServiceImpl::CheckCache(
    [[maybe_unused]] grpc::ServerContext* context,
    const proto::CheckCacheRequest* request,
    proto::CheckCacheResponse* response) {
    try {
        if (request->prompt().empty()) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "User prompt is empty"};
        }

        // Vectorize the user prompt
        const AlignedVector query_vec =
            Embedder::GetInstance().Encode(request->prompt());

        // Get current time ONCE at every CheckCache() routine
        const auto duration =
            std::chrono::system_clock::now().time_since_epoch();
        const auto secs =
            std::chrono::duration_cast<std::chrono::seconds>(duration);
        const auto curr_time = static_cast<uint64_t>(secs.count());

        // Brute-force scanning the L0 Buffer
        float max_score = -1.0f;
        int32_t best_node_id = -1;
        int32_t reusable_node_id = -1;

        for (size_t i = 0; i < engine::L0_MAX_SLOTS; ++i) {
            auto& [created_at, control_block] = memory_arena.GetNode(i);
            const uint64_t best_ctrl = control_block.load(
                std::memory_order_relaxed);  // fine for x86, otherwise acquire
            auto [state, ref_bit, length, v_offset] = UnpackControl(best_ctrl);

            reusable_node_id =
                (state == NodeState::DEAD && reusable_node_id == -1)
                    ? static_cast<int32_t>(i)
                    : reusable_node_id;

            bool is_valid =
                (state == NodeState::READY || state == NodeState::MIGRATING);

            if (state == NodeState::PENDING) {
                const uint64_t birth_time =
                    created_at.load(std::memory_order_acquire);

                if (birth_time == 0) {
                    continue;
                }

                if (curr_time - birth_time > engine::PENDING_LIFESPAN) {
                    uint64_t expected_ctrl = best_ctrl;
                    const uint64_t desired_ctrl =
                        PackControl(NodeState::DEAD, ref_bit, length, v_offset);

                    if (control_block.compare_exchange_strong(
                            expected_ctrl, desired_ctrl,
                            std::memory_order_release,
                            std::memory_order_relaxed)) {
                        reusable_node_id = (reusable_node_id == -1)
                                               ? static_cast<int32_t>(i)
                                               : reusable_node_id;
                        created_at.store(0, std::memory_order_relaxed);
                    }
                } else {
                    is_valid = true;
                }
            }

            if (is_valid) {
                const float curr_score = CosineSimilarity(
                    query_vec.get(), memory_arena.GetVector(i));
                const bool is_better = curr_score > max_score;
                max_score = is_better ? curr_score : max_score;
                best_node_id =
                    is_better ? static_cast<int32_t>(i) : best_node_id;
            }
        }

        // TODO: Perform L1 Scan

        if (max_score >= engine::SIMILARITY_THRESHOLD) {
            auto& [created_at, control_block] =
                memory_arena.GetNode(static_cast<size_t>(best_node_id));

            const uint64_t best_ctrl =
                control_block.load(std::memory_order_acquire);
            const auto [state, ref_bit, length, offset] =
                UnpackControl(best_ctrl);

            switch (state) {
                case NodeState::READY:
                case NodeState::MIGRATING: {
                    memory_arena.ReadPayload(
                        offset, length, response->mutable_cached_payload());
                    response->set_check_state(proto::CACHE_STATE_HIT);
                    response->set_node_id(-1);

                    const uint64_t desired_ctrl =
                        PackControl(state, EvictState::HOT, length, offset);
                    uint64_t expected_ctrl = best_ctrl;

                    control_block.compare_exchange_strong(
                        expected_ctrl, desired_ctrl, std::memory_order_release,
                        std::memory_order_relaxed);

                    return grpc::Status::OK;
                }

                case NodeState::PENDING:
                    response->set_check_state(proto::CACHE_STATE_PENDING);
                    response->set_node_id(best_node_id);
                    return grpc::Status::OK;

                case NodeState::DEAD:
                    reusable_node_id = best_node_id;
                    break;
            }
        }

        if (reusable_node_id == -1) {
            throw std::runtime_error("High traffic");
        }

        auto& [created_at, control_block] =
            memory_arena.GetNode(static_cast<size_t>(reusable_node_id));

        if (uint64_t expected_ctrl =
                control_block.load(std::memory_order_relaxed);
            static_cast<NodeState>(expected_ctrl >> 62) == NodeState::DEAD) {
            const uint64_t desired_ctrl =
                PackControl(NodeState::PENDING, EvictState::HOT, 0, 0);

            if (control_block.compare_exchange_strong(
                    expected_ctrl, desired_ctrl, std::memory_order_release,
                    std::memory_order_relaxed)) {
                std::memcpy(memory_arena.GetVector(
                                static_cast<size_t>(reusable_node_id)),
                            query_vec.get(), engine::VECTOR_MEMSIZE);

                created_at.store(curr_time, std::memory_order::release);

                response->set_check_state(proto::CACHE_STATE_MISS);
                response->set_node_id(reusable_node_id);
                return grpc::Status::OK;
            }
        }

        response->set_check_state(proto::CACHE_STATE_MISS);
        response->set_node_id(-1);
        return grpc::Status::OK;

    } catch (const std::exception& e) {
        return {grpc::StatusCode::INTERNAL,
                std::string("Encounter error: ") + e.what()};
    } catch (...) {
        return {grpc::StatusCode::INTERNAL, "Unknown Fatal error"};
    }
}

grpc::Status SemanticServiceImpl::SetCache(
    [[maybe_unused]] grpc::ServerContext* context,
    const proto::SetCacheRequest* request, proto::SetCacheResponse* response) {
    try {
        if (request->node_id() < 0) {
            return {grpc::StatusCode::INVALID_ARGUMENT,
                    "Attempt to WRITE using negative node_id"};
        }

        if (request->uncached_payload().empty()) {
            return {grpc::StatusCode::INVALID_ARGUMENT,
                    "Cannot cache empty payload"};
        }

        const auto node_id = static_cast<uint32_t>(request->node_id());
        const std::string& payload = request->uncached_payload();

        if (payload.length() > engine::MAX_PAYLOAD_LENGTH) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "Oversized payload"};
        }

        const auto payload_len = static_cast<uint32_t>(payload.length());
        auto& [created_at, control_block] = memory_arena.GetNode(node_id);

        const uint64_t new_offset = memory_arena.WritePayload(
            node_id, reinterpret_cast<const uint8_t*>(payload.data()),
            payload_len);

        const uint64_t desired_control = PackControl(
            NodeState::READY, EvictState::HOT, payload_len, new_offset);
        uint64_t expected_control =
            control_block.load(std::memory_order_relaxed);

        while (true) {
            if (static_cast<uint8_t>(expected_control >> 62) !=
                static_cast<uint8_t>(NodeState::PENDING)) {
                response->set_success(false);
                break;
            }

            if (control_block.compare_exchange_weak(
                    expected_control, desired_control,
                    std::memory_order_release, std::memory_order_relaxed)) {
                response->set_success(true);
                break;
            }
        }

        return grpc::Status::OK;

    } catch (const std::exception& e) {
        return {grpc::StatusCode::INTERNAL,
                std::string("Encounter error") + e.what()};
    } catch (...) {
        return {grpc::StatusCode::INTERNAL, "Unknown Fatal error"};
    }
}
