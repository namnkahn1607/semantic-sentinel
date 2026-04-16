# sentinel/Makefile

.PHONY: load-model bake-tokenizer config-engine build-engine run-engine build-gateway run-gateway build-docker run-prod stop-prod

VCPKG_ROOT ?= $(HOME)/vcpkg

load-model:
	@echo "Getting all-MiniLM-L6-v2 Inference Model from HuggingFace..."
	@bash scripts/get_model.sh

bake-tokenizer:
	@echo "Baking Tokenizer into the model..."
	@cd scripts && python bake_tokenizer.py

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

run-engine:
	@echo "Starting C++ Semantic Engine (Release)..."
	@INFERENCE_MODEL_PATH="$(PWD)/engine/model/sentinel-minilm-with-tokenizer.onnx" \
	ORT_EXTENSIONS_PATH="$(PWD)/engine/model/libortextensions.so" \
	./engine/build-release/sentinel_engine

build-gateway:
	@echo "Building Go Gateway..."
	@cd gateway && go build -o build-release/gateway main.go http_handler.go

run-gateway:
	@echo "Starting Go Gateway..."
	@cd gateway && go run .

gateway-1-1k:
	@echo "Measuring Baseline IPC Latency (Sequential requests)..."
	@~/go/bin/hey -n 1000 -c 1 -m POST -T "application/json" -d '{"prompt": "hello"}' http://localhost:8080/v1/cache/check

gateway-100-10k:
	@echo "Bombarding Gateway with 10k requests (Concurrency: 100)..."
	@~/go/bin/hey -n 10000 -c 100 -m POST -T "application/json" -d '{"prompt": "hello"}' http://localhost:8080/v1/cache/check

engine-50-100k:
	@echo "Shooting 100k requests (Concurrency: 50) directly at C++ Semantic Engine..."
	@ghz --insecure --proto ./api/proto/sentinel.proto --call proto.SemanticService.CheckCache -d '{"prompt_text": "hello"}' -c 50 -n 100000 unix:///tmp/sentinel.sock

build-docker: build-gateway build-engine
	@echo "Packaging Sentinel into Docker Image..."
	@docker build -t sentinel-prod .

run-prod:
	@echo "Deploying Sentinel to Docker..."
	@echo "Physical Constraints: 4 vCPUs (Cores 0-3), 8GB RAM Strict Limit"
	@docker run -it --rm \
		--name sentinel-instance \
		--cpuset-cpus="0-3" \
		--memory="8g" \
		-v $(PWD)/engine/model:/app/model \
		-e INFERENCE_MODEL_PATH="/app/model/sentinel-minilm-with-tokenizer.onnx" \
		-e ORT_EXTENSIONS_PATH="/app/model/libortextensions.so" \
		-p 8080:8080 \
		sentinel-prod

stop-prod:
	@echo "Terminating Sentinel container..."
	@docker stop sentinel-instance || true