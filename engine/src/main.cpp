#include <grpcpp/grpcpp.h>

#include "proto/sentinel.grpc.pb.h"

class SemanticServiceImpl final : public proto::SemanticService::Service {
public:
    grpc::Status CheckCache(grpc::ServerContext* context,
                            const proto::CheckCacheRequest* request,
                            proto::CheckCacheResponse* response) override {
        if (request->prompt_text() == "hello") {
            response->set_is_hit(true);
            response->set_cached_payload("world");
        } else {
            response->set_is_hit(false);
        }

        return grpc::Status::OK;
    };
};

int main() {
    return 0;
}