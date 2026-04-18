package main

import (
	"context"
	"errors"
	"fmt"
	pb "gateway/pb/proto"
	"log"
	"net/http"
	"os"
	"os/signal"
	"runtime"
	"sync"
	"syscall"
	"time"

	"github.com/VictoriaMetrics/fastcache"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

const (
	socketAddress = "unix:///tmp/sentinel.sock"
	endpoint      = "/v1/cache/check"
	serverPort    = ":8080"

	funcWupTimeout        = 50 * time.Millisecond
	coldSrtTimeout        = 100 * time.Millisecond
	serverShutdownTimeout = 3 * time.Second

	// 256MB L1 Fast Cache maximal size in memory
	maxL1CacheSize = 256 * 1024 * 1024

	// Number of warm-up Goroutines to initiate to force Semantic Engine
	// to expand Thread Pool and widen Flow Control Window.
	warmUpConcurrency = 100
)

func main() {
	// Affinity setting: use only 1 vCPU
	runtime.GOMAXPROCS(1)

	if err := run(); err != nil {
		log.Fatalf("[HTTP Gateway] Gateway terminated: %v", err)
	}
}

func run() error {
	// 1. Initialize L1 Exact-Match Fast Cache
	log.Printf("[HTTP Gateway] Allocating %dMB Off-heap Memory for Exact-Match Cache...", maxL1CacheSize/(1024*1024))
	l1Cache := fastcache.New(maxL1CacheSize)
	defer l1Cache.Reset()

	// 2. Setup ONLY ONE connection using grpc.NewClient() (no TLS encryption)
	conn, connErr := grpc.NewClient(socketAddress, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if connErr != nil {
		return connErr
	}

	defer func() {
		if connCloseErr := conn.Close(); connCloseErr != nil {
			log.Printf("[HTTP Gateway] Connection close Error: %v\n", connCloseErr)
		} else {
			log.Printf("[HTTP Gateway] Closed UDS Connection.")
		}
	}()

	// 3. Create ONLY ONE Client (Server's stub)
	clientStub := pb.NewSemanticServiceClient(conn)

	// 4. High-Concurrency Warm-up routine
	log.Println("[HTTP Gateway] Initiating High-Concurrency Warm-up (Thread Pool Expansion)...")

	// Functional Warm-up (against gRPC's Lazy Connection)
	pingCtx, pingCancel := context.WithTimeout(context.Background(), funcWupTimeout)
	if _, pingErr := clientStub.CheckCache(pingCtx, &pb.CheckCacheRequest{PromptText: "warmup_signal"}); pingErr != nil {
		pingCancel()
		return fmt.Errorf("crashed Vector Engine on arrival: %w", pingErr)
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
	log.Println("[HTTP Gateway] Warmup completed.")

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
		log.Printf("[HTTP Gateway] Gateway listening on %s...\n", serverPort)
		if serverErr := server.ListenAndServe(); serverErr != nil && !errors.Is(serverErr, http.ErrServerClosed) {
			serverErrChan <- serverErr
		}
	}()

	// 9. Catch ONLY the first item into one of the channels
	select {
	case err := <-serverErrChan:
		return fmt.Errorf("[HTTP Gateway] Crashed due to:  %w", err)
	case sig := <-sigChan:
		log.Printf("[HTTP Gateway] Received signal: %v. Initiating graceful shutdown...\n", sig)
	}

	// 10. Create context timeout - the Server waits for remaining goroutines to finish
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), serverShutdownTimeout)
	defer shutdownCancel()

	// 11. Perform Server graceful shutdown
	if shutdownErr := server.Shutdown(shutdownCtx); shutdownErr != nil {
		return fmt.Errorf("[HTTP Gateway] Shutdown failed: %w", shutdownErr)
	}

	log.Printf("[HTTP Gateway] Server stopped.")
	return nil
}
