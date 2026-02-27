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

        response->set_similarity_score(response->is_hit() ? 1.0 : 0.0);

        return grpc::Status::OK;
    };
};

void RunServer() {
    const std::string server_address = "unix:///tmp/sentinel.sock";
    const auto socket_directory = "tmp/sentinel.sock";

    // Clear out old socket file from previous process run
    // before binding into new one.
    unlink(socket_directory);

    SemanticServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        server_address,
        grpc::InsecureServerCredentials());  // no TLS encryption
    builder.RegisterService(&service);

    // Force Linux creating a physical file (socket) at server_address
    // and bind() C++ process to it.
    const std::unique_ptr server(builder.BuildAndStart());

    // A daemon (background) process is not allowed to end main().
    // Call Wait() to lock main thread, releasing CPU for gRPC workers to
    // work on data traveling through the socket.
    server->Wait();
}

int main() {
    RunServer();
    return 0;
}