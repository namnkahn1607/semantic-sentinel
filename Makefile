# sentinel/Makefile

.PHONY: setup config-engine build-engine run-gateway stress-test

VCPKG_ROOT ?= $(HOME)/vcpkg

setup:
	@echo "Setting up environment..."
	@bash scripts/get_model.sh

config-engine:
	@echo "Configuring C++ Semantic Engine (Release)..."
	@cmake -B engine/build-release -S engine \
		-DCMAKE_BUILD_TYPE=Release \
		-G Ninja \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake

build-engine: config-engine
	@echo "Building C++ Semantic Engine..."
	@cmake --build engine/build-release -j 2

run-engine: build-engine
	@echo "Starting C++ Semantic Engine (Release)..."
	@./engine/build-release/sentinel_engine

run-gateway:
	@echo "Starting Go Gateway..."
	@cd gateway && go run main.go

stress-test:
	@echo "Bombarding Gateway with 10,000 requests (Concurrency: 100)..."
	@~/go/bin/hey -n 10000 -c 100 -m POST -T "application/json" -d '{"prompt": "hello"}' http://localhost:8080/v1/cache/check

stress-test-baseline:
	@echo "Measuring Baseline IPC Latency (Sequential requests)..."
	@~/go/bin/hey -n 10000 -c 1 -m POST -T "application/json" -d '{"prompt": "hello"}' http://localhost:8080/v1/cache/check