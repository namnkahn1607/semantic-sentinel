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

    // The WRITE gRPC method
    grpc::Status SetCache(grpc::ServerContext* context,
                          const proto::SetCacheRequest* request,
                          proto::SetCacheResponse* response) override;

private:
    MemoryArena& memory_arena;

    void ReadPayload(uint32_t offset, uint32_t length,
                        std::string* out_payload) const;

    uint64_t WritePayload(uint32_t node_id, const uint8_t* in_payload,
                             uint32_t length) const;
};

#endif  // SENTINEL_ENGINE_SERVICE_HH
