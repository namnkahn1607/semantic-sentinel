# strix/Makefile

VCPKG_ROOT    ?= $(HOME)/vcpkg
BIN_DIR       := ./bin

GATEWAY_SRC   := ./gateway
GATEWAY_BIN   := $(BIN_DIR)/strix_gateway

GO            := go
GO_FLAGS      := -ldflags="-s -w"

ENGINE_SRC    := ./engine
ENGINE_BUILD  := ./engine/build-release
ENGINE_BIN    := $(BIN_DIR)/strix_engine

.PHONY: gen-dataclass build-gateway build-engine config-engine

gen-dataclass:
	@echo "Generating Go and C++ data classes for gRPC..."
	@buf generate
	@./engine/build-release/vcpkg_installed/x64-linux/tools/protobuf/protoc \
  	-I=api/proto/ \
  	--cpp_out=engine/pb/proto/ \
  	--grpc_out=engine/pb/proto/ \
  	--plugin=protoc-gen-grpc=./engine/build-release/vcpkg_installed/x64-linux/tools/grpc/grpc_cpp_plugin \
	strix.proto

build-gateway:
	@echo "Building HTTP Gateway..."
	cd $(GATEWAY_SRC) && CGO_ENABLED=0 $(GO) build $(GO_FLAGS) -o ../$(GATEWAY_BIN) .

config-engine:
	@echo "Configurating Release profile for Vector Engine..."
	@cmake -B $(ENGINE_BUILD) -S $(ENGINE_SRC) \
		-DCMAKE_BUILD_TYPE=Release \
		-G Ninja \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake

build-engine: config-engine
	@echo "Building Vector Engine..."
	@cmake --build $(ENGINE_BUILD) --target strix_engine -j 4
