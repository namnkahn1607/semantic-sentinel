package main

import (
	"context"
	"encoding/json"
	pb "gateway/pb/proto"
	"log"
	"net/http"
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
	serviceTimeout = 15 * time.Millisecond
	warmupTimeout  = 50 * time.Millisecond
	endpoint       = "/v1/cache/check"
	serverPort     = ":8080"
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

	// 4. Create ServeMux (Router) - register endpoint to action function
	mux := http.NewServeMux()
	mux.HandleFunc(endpoint, handleCheckCache(clientStub))

	// 5. Open HTTP Server at port 8080 listening for requests
	log.Printf("Gateway listening on %s...\n", serverPort)
	if portErr := http.ListenAndServe(serverPort, mux); portErr != nil {
		return portErr
	}

	return nil
}

func main() {
	if err := run(); err != nil {
		log.Fatalf("Gateway terminated: %v", err)
	}
}
