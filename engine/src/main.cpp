#include <grpcpp/grpcpp.h>

#include "arena.hh"
#include "constant.hh"
#include "proto/sentinel.grpc.pb.h"

class SemanticServiceImpl final : public proto::SemanticService::Service {
public:
    explicit SemanticServiceImpl(MemoryArena& arena) : memory_arena(arena) {}

    // No Copy/Move constructor
    SemanticServiceImpl(const SemanticServiceImpl&) = delete;

    grpc::Status CheckCache(
        [[maybe_unused]] grpc::ServerContext* context,
        const proto::CheckCacheRequest* request,
        [[maybe_unused]] proto::CheckCacheResponse* response) override {
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
                auto& curr_node = memory_arena.getL0Node(i);
                const auto curr_state = static_cast<NodeState>(
                    curr_node.state.load(std::memory_order_acquire));

                if (curr_state == NodeState::READY) {
                    // TODO: AVX2 math here
                } else if (curr_state == NodeState::PENDING) {
                    if (curr_time - curr_node.created_at.load(
                                        std::memory_order_relaxed) >
                        engine::PENDING_LIFESPAN) {
                        curr_node.state.store(
                            static_cast<uint8_t>(NodeState::DEAD),
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
            return {
                grpc::StatusCode::INTERNAL,
                std::string("[Vector Engine] Encounter error: ") + e.what()};
        } catch (...) {
            return {grpc::StatusCode::INTERNAL,
                    "[Vector Engine] Unknown Fatal error"};
        }
    };

private:
    MemoryArena& memory_arena;
};

void RunServer() {
    const std::string server_address{"unix:///tmp/sentinel.sock"};
    const auto socket_directory{"/tmp/sentinel.sock"};

    // Clear out old socket file from previous process run
    // before binding into new one.
    unlink(socket_directory);

    // Main Thread is responsible for construct & deconstruct Memory Arena.
    const auto memory_arena = std::make_unique<MemoryArena>();

    // Service is only allowed to reference for reading and writing data.
    SemanticServiceImpl service(*memory_arena);

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
