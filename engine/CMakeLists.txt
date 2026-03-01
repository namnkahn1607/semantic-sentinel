cmake_minimum_required(VERSION 3.22)
project(sentinel_engine VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

add_executable(sentinel_engine
        src/main.cpp
        pb/proto/sentinel.pb.cc
        pb/proto/sentinel.grpc.pb.cc
        include/embedder.hh
        src/embedder.cc
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
