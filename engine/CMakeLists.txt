cmake_minimum_required(VERSION 3.22)
project(sentinel_engine VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

add_library(sentinel_core STATIC
        # gRPC stubs files
        pb/proto/sentinel.pb.cc
        pb/proto/sentinel.grpc.pb.cc
        # Source files
        src/arena.cc
        src/embedder.cc
        src/service.cc
        # Header files
        include/arena.hh
        include/avx_math.hh
        include/constant.hh
        include/embedder.hh
        include/node.hh
        include/raii_vector.hh
        include/service.hh
)

target_compile_options(sentinel_core
        PUBLIC
        -march=x86-64-v3 # AVX, AVX2, BMI1, BMI2, FMA and LZCNT enabled
        PRIVATE
        -Wall # All basic warnings
        -Wextra # More warnings
        -Wpedantic # ISO C++ is a MUST
        -Wshadow # Local variables must not shadow outer-scope ones
        -Wconversion # When implicit conversion might cause data loss
        -Wunreachable-code # Unreachable code warnings
)

target_include_directories(sentinel_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/pb
)

target_link_libraries(sentinel_core PUBLIC
        gRPC::grpc++
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        onnxruntime::onnxruntime
)

# Total Executable
find_package(GTest CONFIG REQUIRED)
add_executable(sentinel_engine src/main.cc)
target_link_libraries(sentinel_engine PRIVATE sentinel_core)

# Testing Executables
add_executable(avx2_test tests/avx_math_test.cc)
target_link_libraries(avx2_test PRIVATE
        sentinel_core
        GTest::gtest_main
        GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(avx2_test)

# Benchmark Executables
find_package(benchmark CONFIG REQUIRED)
add_executable(avx2_bench bench/avx_bench.cc)
target_link_libraries(avx2_bench PRIVATE
        sentinel_core
        benchmark::benchmark
)