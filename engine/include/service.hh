//
// Created by nlnk on Apr 17, 26.
//

#ifndef SENTINEL_ENGINE_SERVICE_HH
#define SENTINEL_ENGINE_SERVICE_HH

#include "proto/sentinel.grpc.pb.h"

class MemoryArena;

class SemanticServiceImpl final : public proto::SemanticService::Service {
public:
    explicit SemanticServiceImpl(MemoryArena& arena);

    // No Copy/Move constructor
    SemanticServiceImpl(const SemanticServiceImpl&) = delete;

    // The READ gRPC method
    grpc::Status CheckCache(grpc::ServerContext* context,
                            const proto::CheckCacheRequest* request,
                            proto::CheckCacheResponse* response) override;

    // The 'future' WRITE gRPC method
    [[nodiscard]] bool SetCache(uint32_t node_id, const float* vector_data,
                                const std::string& payload) const;

private:
    uint64_t WriteRingBuffer(uint32_t node_id, const uint8_t* payload,
                             uint32_t length) const;

private:
    MemoryArena& memory_arena;
};

#endif  // SENTINEL_ENGINE_SERVICE_HH
