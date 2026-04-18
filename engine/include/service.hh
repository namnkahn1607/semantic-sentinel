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

    grpc::Status CheckCache(grpc::ServerContext* context,
                            const proto::CheckCacheRequest* request,
                            proto::CheckCacheResponse* response) override;

    uint64_t WriteRingBuffer(const uint8_t* payload, size_t length);

    // Future gRPC method
    bool SetCache(uint32_t node_id, const std::string& payload);

private:
    MemoryArena& memory_arena;
};

#endif  // SENTINEL_ENGINE_SERVICE_HH
