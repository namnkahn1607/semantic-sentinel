#include <grpcpp/grpcpp.h>

#include "embedder.hh"
#include "proto/sentinel.grpc.pb.h"

class SemanticServiceImpl final : public proto::SemanticService::Service {
public:
    grpc::Status CheckCache(grpc::ServerContext* context,
                            const proto::CheckCacheRequest* request,
                            proto::CheckCacheResponse* response) override {
        try {
            if (request->prompt_text().empty()) {
                return {grpc::StatusCode::INVALID_ARGUMENT, "Prompt is empty."};
            }

            const std::vector<float> req_vector =
                Embedder::getInstance().Encode(request->prompt_text());

            const std::vector mock_vector(384, 0.1f);

            const float similarity_score =
                Embedder::CosineSimilarity(req_vector, mock_vector);
            const bool is_hit = similarity_score >= 0.85f;

            response->set_is_hit(is_hit);
            response->set_similarity_score(similarity_score);
            response->set_cached_payload(is_hit ? "cached_payload" : "none");

            return grpc::Status::OK;

        } catch (const std::exception& e) {
            return {grpc::StatusCode::INTERNAL,
                    std::string("Engine Error: ") + e.what()};
        } catch (...) {
            return {grpc::StatusCode::INTERNAL,
                    "Unknown Fatal Error in C++ Engine."};
        }
    };
};

void RunServer() {
    const std::string server_address = "unix:///tmp/sentinel.sock";
    const auto socket_directory = "/tmp/sentinel.sock";

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
    try {
        const std::string test_prompt = "hello world";
        const std::vector<float> vec_b =
            Embedder::getInstance().Encode(test_prompt);
        std::cout << "Pipeline B (C++ Baked model) - First 8 floats: "
                  << std::endl;
        std::cout << "[";
        for (int i = 0; i < 8; ++i) {
            std::cout << std::fixed << std::setprecision(6) << vec_b[i];
            if (i < 7) {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    std::cout << "Completed. Opening to gRPC..." << std::endl;
    return 0;
}