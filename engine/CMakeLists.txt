cmake_minimum_required(VERSION 3.22)
project(sentinel_engine VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

add_executable(sentinel_engine
        pb/proto/sentinel.pb.cc
        pb/proto/sentinel.grpc.pb.cc
        src/arena.cc
        src/embedder.cc
        src/main.cpp
        src/service.cc
        include/arena.hh
        include/constant.hh
        include/embedder.hh
        include/node.hh
        include/service.hh
)

target_compile_options(sentinel_engine PRIVATE
        -Wall # All basic warnings
        -Wextra # More warnings
        -Wpedantic # ISO C++ is a MUST
        -Wshadow # Local variables must not shadow outer-scope ones
        -Wconversion # When implicit conversion might cause data loss
        -Wunreachable-code # Unreachable code warnings
)

target_include_directories(sentinel_engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/pb
)

target_link_libraries(sentinel_engine PRIVATE
        gRPC::grpc++
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        onnxruntime::onnxruntime
)
