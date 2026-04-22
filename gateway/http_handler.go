package main

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	pb "gateway/pb/proto"
	"log"
	"net/http"
	"time"

	"github.com/VictoriaMetrics/fastcache"
)

const (
	// Maximal model inference time is 20ms, service's SLA is 50ms,
	// so timeout must be between 20ms and 50ms (30ms is ideal).
	// We haven't attached Semantic Engine, so now latency only relies
	// on Context Switch and Network I/O.
	serviceTimeout = 30 * time.Millisecond

	maxReaderSize = 1024 * 1024
	maxPromptLen  = 512
)

type CheckCacheAPIRequest struct {
	Prompt string `json:"prompt"`
}

type CheckCacheAPIResponse struct {
	CheckState    pb.CacheState `json:"check_state"`
	NodeID        int32         `json:"node_id"`
	CachedPayload string        `json:"cached_payload"`
}

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
			log.Printf("[HTTP Gateway] Decoding error: %v\n", decErr)
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
				log.Printf("[HTTP Gateway] Respond Byte Writing Error: %v\n", writeErr)
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
				log.Printf("[HTTP Gateway] Respond Byte Writing Error: %v\n", writeErr)
			}

			return
		}

		// 6. gRPC Context with Timeout of 30ms
		ctx, cancel := context.WithTimeout(r.Context(), serviceTimeout)
		defer cancel()

		// 7. Remote Procedure Call
		grpcRes, rpcErr := stub.CheckCache(ctx, &pb.CheckCacheRequest{Prompt: apiReq.Prompt})
		if rpcErr != nil {
			log.Printf("[HTTP Gateway] RPC error encountered: %v\n", rpcErr)
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		// 8. Encode Request (into JSON)
		apiRes := CheckCacheAPIResponse{
			CheckState:    grpcRes.GetCheckState(),
			NodeID:        grpcRes.GetNodeId(),
			CachedPayload: grpcRes.GetCachedPayload(),
		}

		// Payload buffering
		payloadBytes, marshErr := json.Marshal(&apiRes)
		if marshErr != nil {
			log.Printf("[HTTP Gateway] Marshal Error: %v\n", marshErr)
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		// Write headers first, body later
		w.Header().Set("Content-Type", "application/json")
		// Then the payload data is sent later on.
		if _, writeErr := w.Write(payloadBytes); writeErr != nil {
			log.Printf("[HTTP Gateway] Respond Byte Writing Error: %v\n", writeErr)
		}
	}
}
