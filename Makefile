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

.PHONY: install build-gateway config-engine build-engine clean

install: $(BIN_DIR) build-gateway build-engine
	@echo "Done. Run 'strix init' then 'strix serve' to start."

build-gateway: $(BIN_DIR)
	@echo "Building HTTP Gateway..."
	cd $(GATEWAY_SRC) && CGO_ENABLED=0 $(GO) build $(GO_FLAGS) -o ../$(GATEWAY_BIN) .

config-engine:
	@echo "Configuring C++ Vector Engine (Release)..."
	@cmake -B $(ENGINE_BUILD) -S $(ENGINE_SRC) \
		-DCMAKE_BUILD_TYPE=Release \
		-G Ninja \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake

build-engine: config-engine $(BIN_DIR)
	@echo "Building C++ Vector Engine..."
	@cmake --build $(ENGINE_BUILD) --target strix_engine -j 4
	@cp $(ENGINE_BUILD)/strix_engine $(ENGINE_BIN)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

clean:
	@rm -rf $(BIN_DIR)
	@rm -rf $(ENGINE_BUILD)
	@echo "Cleaned."