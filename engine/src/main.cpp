#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <vector>

#include "embedder.hh"
#include "proto/sentinel.grpc.pb.h"

class SemanticServiceImpl final : public proto::SemanticService::Service {
public:
    SemanticServiceImpl() : mock_database(10000, std::vector(384, 0.1f)) {}

    grpc::Status CheckCache([[maybe_unused]] grpc::ServerContext* context,
                            const proto::CheckCacheRequest* request,
                            proto::CheckCacheResponse* response) override {
        try {
            if (request->prompt_text().empty()) {
                return {grpc::StatusCode::INVALID_ARGUMENT,
                        "User prompt is empty."};
            }

            return grpc::Status::OK;

        } catch (const std::exception& e) {
            return {
                grpc::StatusCode::INTERNAL,
                std::string("[Vector Engine] Encounter error: ") + e.what()};
        } catch (...) {
            return {grpc::StatusCode::INTERNAL,
                    "[Vector Engine] Unknown Fatal error"};
        }
    };
};

void RunServer() {
    const std::string server_address{"unix:///tmp/sentinel.sock"};
    const auto socket_directory{"/tmp/sentinel.sock"};

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
    std::cout << "[Vector Engine] Opening to gRPC..." << std::endl;
    RunServer();
    std::cout << "[Vector Engine] Closing..." << std::endl;

    return 0;
}
