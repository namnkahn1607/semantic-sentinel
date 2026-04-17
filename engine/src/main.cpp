#include <grpcpp/grpcpp.h>

#include "arena.hh"
#include "proto/sentinel.grpc.pb.h"
#include "service.hh"

void RunServer(MemoryArena& arena) {
    const std::string server_address{"unix:///tmp/sentinel.sock"};
    const auto socket_directory{"/tmp/sentinel.sock"};

    // Clear out old socket file from previous process run
    // before binding into new one.
    unlink(socket_directory);

    // Service is only allowed to reference for reading and writing data.
    SemanticServiceImpl service(arena);

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
    // Main Thread is responsible for construct & deconstruct Memory Arena.
    const auto memory_arena = std::make_unique<MemoryArena>();

    std::cout << "[Vector Engine] Opening to gRPC..." << std::endl;
    RunServer(*memory_arena);
    std::cout << "[Vector Engine] Closing..." << std::endl;

    return 0;
}
