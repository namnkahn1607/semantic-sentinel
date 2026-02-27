package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	pb "gateway/pb/proto"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

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
	// Maximal model inference time is 20ms, service's SLA is 50ms,
	// so timeout must be between 20ms and 50ms (30ms is ideal).
	// We haven't attached Semantic Engine, so now latency only relies
	// on Context Switch and Network I/O.
	// TODO: After attaching Semantic Engine, change serviceTimeout to 30ms.
	serviceTimeout        = 2 * time.Millisecond
	warmupTimeout         = 50 * time.Millisecond
	serverShutdownTimeout = 5 * time.Second
	endpoint              = "/v1/cache/check"
	serverPort            = ":8080"
)

// HTTP Handler
func handleCheckCache(stub pb.SemanticServiceClient) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// 1. Method validation
		if r.Method != http.MethodPost {
			http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
			return
		}

		// 2. Decode Request (from JSON)
		var apiReq CheckCacheAPIRequest
		if decErr := json.NewDecoder(r.Body).Decode(&apiReq); decErr != nil {
			log.Printf("Decoding error: %v\n", decErr)
			http.Error(w, "Bad Request", http.StatusBadRequest)
			return
		}

		// 3. gRPC Context with Timeout of 2ms
		ctx, cancel := context.WithTimeout(r.Context(), serviceTimeout)
		defer cancel()

		// 4. Remote Procedure Call
		rpcRes, rpcErr := stub.CheckCache(ctx, &pb.CheckCacheRequest{PromptText: apiReq.Prompt})
		if rpcErr != nil {
			log.Printf("RPC error encountered: %v\n", rpcErr)
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		// 5. Encode Request (into JSON)
		apiRes := CheckCacheAPIResponse{
			IsHit:           rpcRes.GetIsHit(),
			CachePayload:    rpcRes.GetCachedPayload(),
			SimilarityScore: float64(rpcRes.GetSimilarityScore()),
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
		// Flush back into TCP Socket - status code is sent via network stream first.
		w.WriteHeader(http.StatusOK)
		// Then the payload data is sent later on.
		if _, writeErr := w.Write(payloadBytes); writeErr != nil {
			log.Printf("Respond Byte Writing Error: %v\n", writeErr)
		}
	}
}

func run() error {
	// 1. Setup ONLY ONE connection using grpc.NewClient() (no TLS encryption)
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

	// 2. Create ONLY ONE Client (Server's stub)
	clientStub := pb.NewSemanticServiceClient(conn)

	// 3. Warmup routine against gRPC's Lazy Connection
	log.Println("Warming up gRPC connection to Sematic Engine...")

	warmupCtx, warmupCancel := context.WithTimeout(context.Background(), warmupTimeout)
	defer warmupCancel()
	if _, warmupErr := clientStub.CheckCache(warmupCtx, &pb.CheckCacheRequest{PromptText: "warmup_signal"}); warmupErr != nil {
		return warmupErr
	}

	log.Println("Warmup completed.")

	// 4. Create ServeMux (Router) - register endpoint to handler function
	mux := http.NewServeMux()
	mux.HandleFunc(endpoint, handleCheckCache(clientStub))

	// 5. Create Server instance (with address and handler function)
	server := &http.Server{
		Addr:    serverPort,
		Handler: mux,
	}

	// 6. OS signal channel
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt /* SIGINT */, syscall.SIGTERM)

	// 7. HTTP Server error channel
	serverErrChan := make(chan error, 1)
	go func() {
		log.Printf("Gateway listening on %s...\n", serverPort)
		if serverErr := server.ListenAndServe(); serverErr != nil && !errors.Is(serverErr, http.ErrServerClosed) {
			serverErrChan <- serverErr
		}
	}()

	// 8. Catch ONLY the first item into one of the channels
	select {
	case err := <-serverErrChan:
		return fmt.Errorf("HTTP Server crashed %w", err)
	case sig := <-sigChan:
		log.Printf("Received signal: %v. Initiating graceful shutdown...\n", sig)
	}

	// 9. Create context timeout - the Server waits for remaining goroutines to finish
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), serverShutdownTimeout)
	defer shutdownCancel()

	// 10. Perform Server graceful shutdown
	if shutdownErr := server.Shutdown(shutdownCtx); shutdownErr != nil {
		return fmt.Errorf("HTTP Server shutdown failed: %w", shutdownErr)
	}

	log.Printf("HTTP Server stopped.")
	return nil
}

func main() {
	if err := run(); err != nil {
		log.Fatalf("Gateway terminated: %v", err)
	}
}
