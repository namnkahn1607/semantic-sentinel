package main

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	pb "gateway/pb/proto"
	"log"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/VictoriaMetrics/fastcache"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

type CheckCacheAPIRequest struct {
	Prompt string `json:"prompt"`
}

type CheckCacheAPIResponse struct {
	IsHit           bool    `json:"is_hit"`
	CachePayload    string  `json:"cache_payload"`
	SimilarityScore float64 `json:"similarity_score"`
}

const (
	socketAddress = "unix:///tmp/sentinel.sock"
	endpoint      = "/v1/cache/check"
	serverPort    = ":8080"

	// Maximal model inference time is 20ms, service's SLA is 50ms,
	// so timeout must be between 20ms and 50ms (30ms is ideal).
	// We haven't attached Semantic Engine, so now latency only relies
	// on Context Switch and Network I/O.
	serviceTimeout        = 30 * time.Millisecond
	funcWupTimeout        = 50 * time.Millisecond
	coldSrtTimeout        = 100 * time.Millisecond
	serverShutdownTimeout = 5 * time.Second

	// 256MB L1 Fast Cache maximal size in memory
	maxL1CacheSize = 256 * 1024 * 1024
	maxReaderSize  = 1024 * 1024
	maxPromptLen   = 512

	// Number of warm-up Goroutines to initiate to force Semantic Engine
	// to expand Thread Pool and widen Flow Control Window.
	warmUpConcurrency = 100
)

// HTTP Handler
func handleCheckCache(stub pb.SemanticServiceClient, l1Cache *fastcache.Cache) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// 1. Method validation
		if r.Method != http.MethodPost {
			http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
			return
		}

		// 2. Decode Request (from JSON)
		var apiReq CheckCacheAPIRequest
		// Set max reader size of 1MB, preventing DDOS attack
		r.Body = http.MaxBytesReader(w, r.Body, maxReaderSize)
		if decErr := json.NewDecoder(r.Body).Decode(&apiReq); decErr != nil {
			log.Printf("Decoding error: %v\n", decErr)
			http.Error(w, "Bad Request", http.StatusBadRequest)
			return
		}

		// 3. Calculate prompt's SHA-256 hashcode
		promptBytes := []byte(apiReq.Prompt)
		hash := sha256.Sum256(promptBytes)
		hashKey := hash[:]

		// 4. Check on L1 Fast Cache using hashcode
		// Hit -> return to user.
		l1CachedPayload := l1Cache.Get(nil, hashKey)
		if l1CachedPayload != nil {
			w.Header().Set("Content-Type", "application/json")
			if _, writeErr := w.Write(l1CachedPayload); writeErr != nil {
				log.Printf("Respond Byte Writing Error: %v\n", writeErr)
			}

			return
		}

		// 5. Length Checking, forward to LLM Provider if exceeds 512 bytes
		if len(promptBytes) > maxPromptLen {
			// TODO: Make call to LLM Provider API
			mockLLMResponse := []byte(`{"source": "llm", "text": "long_tail_response"}`)

			// fastcache.Set() is Lock-free and use memcopy at the OS level.
			l1Cache.Set(hashKey, mockLLMResponse)
			w.Header().Set("Content-Type", "application/json")
			if _, writeErr := w.Write(mockLLMResponse); writeErr != nil {
				log.Printf("Respond Byte Writing Error: %v\n", writeErr)
			}

			return
		}

		// 6. gRPC Context with Timeout of 30ms
		ctx, cancel := context.WithTimeout(r.Context(), serviceTimeout)
		defer cancel()

		// 7. Remote Procedure Call
		grpcRes, rpcErr := stub.CheckCache(ctx, &pb.CheckCacheRequest{PromptText: apiReq.Prompt})
		if rpcErr != nil {
			log.Printf("RPC error encountered: %v\n", rpcErr)
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		// 8. Encode Request (into JSON)
		apiRes := CheckCacheAPIResponse{
			IsHit:           grpcRes.GetIsHit(),
			CachePayload:    grpcRes.GetCachedPayload(),
			SimilarityScore: float64(grpcRes.GetSimilarityScore()),
		}

		// Payload buffering
		payloadBytes, marshErr := json.Marshal(&apiRes)
		if marshErr != nil {
			log.Printf("Marshal Error: %v\n", marshErr)
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		// Write headers first, body later
		w.Header().Set("Content-Type", "application/json")
		// Then the payload data is sent later on.
		if _, writeErr := w.Write(payloadBytes); writeErr != nil {
			log.Printf("Respond Byte Writing Error: %v\n", writeErr)
		}
	}
}

func main() {
	if err := run(); err != nil {
		log.Fatalf("Gateway terminated: %v", err)
	}
}

func run() error {
	// 1. Initialize L1 Exact-Match Fast Cache
	log.Printf("Allocating %dMB Off-heap Memory for L1 Exact-Match Cache...", maxL1CacheSize)
	l1Cache := fastcache.New(maxL1CacheSize)
	defer l1Cache.Reset()

	// 2. Setup ONLY ONE connection using grpc.NewClient() (no TLS encryption)
	conn, connErr := grpc.NewClient(socketAddress, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if connErr != nil {
		return connErr
	}

	defer func() {
		if connCloseErr := conn.Close(); connCloseErr != nil {
			log.Printf("Connection close Error: %v\n", connCloseErr)
		} else {
			log.Printf("Closed UDS Connection.")
		}
	}()

	// 3. Create ONLY ONE Client (Server's stub)
	clientStub := pb.NewSemanticServiceClient(conn)

	// 4. High-Concurrency Warm-up routine
	log.Println("Initiating High-Concurrency Warm-up (Thread Pool Expansion)...")

	// Functional Warm-up (against gRPC's Lazy Connection)
	pingCtx, pingCancel := context.WithTimeout(context.Background(), funcWupTimeout)
	if _, pingErr := clientStub.CheckCache(pingCtx, &pb.CheckCacheRequest{PromptText: "warmup_signal"}); pingErr != nil {
		pingCancel()
		return fmt.Errorf("semantic engine crashed on arrival: %w", pingErr)
	}

	pingCancel()

	// Cold start Warm-up (100 requests in 100 Goroutines simultaneously)
	var wg sync.WaitGroup
	for range warmUpConcurrency {
		wg.Add(1)
		go func() {
			defer wg.Done()
			burstCtx, burstCancel := context.WithTimeout(context.Background(), coldSrtTimeout)
			defer burstCancel()
			_, _ = clientStub.CheckCache(burstCtx, &pb.CheckCacheRequest{PromptText: "warmup_burst"})
		}()
	}

	wg.Wait()
	log.Println("Warmup completed.")

	// 5. Create ServeMux (Router) - register endpoint to handler function
	mux := http.NewServeMux()
	mux.HandleFunc(endpoint, handleCheckCache(clientStub, l1Cache))

	// 6. Create Server instance (with address and handler function)
	server := &http.Server{
		Addr:    serverPort,
		Handler: mux,
	}

	// 7. OS signal channel
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt /* SIGINT */, syscall.SIGTERM)

	// 8. HTTP Server error channel
	serverErrChan := make(chan error, 1)
	go func() {
		log.Printf("Gateway listening on %s...\n", serverPort)
		if serverErr := server.ListenAndServe(); serverErr != nil && !errors.Is(serverErr, http.ErrServerClosed) {
			serverErrChan <- serverErr
		}
	}()

	// 9. Catch ONLY the first item into one of the channels
	select {
	case err := <-serverErrChan:
		return fmt.Errorf("HTTP Server crashed %w", err)
	case sig := <-sigChan:
		log.Printf("Received signal: %v. Initiating graceful shutdown...\n", sig)
	}

	// 10. Create context timeout - the Server waits for remaining goroutines to finish
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), serverShutdownTimeout)
	defer shutdownCancel()

	// 11. Perform Server graceful shutdown
	if shutdownErr := server.Shutdown(shutdownCtx); shutdownErr != nil {
		return fmt.Errorf("HTTP Server shutdown failed: %w", shutdownErr)
	}

	log.Printf("HTTP Server stopped.")
	return nil
}
