package internal

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
	ServiceTimeout = 50 * time.Millisecond

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

// HandleCheckCache returns an http.HandlerFunc that:
//  1. Validates the request method to be POST.
//  2. Decodes the JSON body.
//  3. Computes SHA-256 hash of the prompt and queries the L1 exact-match cache.
//  4. Falls through to the LLM provider for long prompts (> 512 bytes).
//  5. Falls through to the Vector Engine for short prompts.
func HandleCheckCache(stub pb.SemanticServiceClient, l1Cache *fastcache.Cache) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// 1. Validates the request method to be POST.
		if r.Method != http.MethodPost {
			http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
			return
		}

		// 2. Decodes the JSON body.
		var apiReq CheckCacheAPIRequest
		r.Body = http.MaxBytesReader(w, r.Body, maxReaderSize)
		if decErr := json.NewDecoder(r.Body).Decode(&apiReq); decErr != nil {
			log.Printf("[Handler] Decoding error: %v\n", decErr)
			http.Error(w, "Bad Request", http.StatusBadRequest)
			return
		}

		// 3.1. Computes SHA-256 hash of the prompt
		promptBytes := []byte(apiReq.Prompt)
		hash := sha256.Sum256(promptBytes)
		hashKey := hash[:]

		// 3.2. Check on L1 Fast Cache using hashcode. If hit, return immediately.
		if l1CachedPayload := l1Cache.Get(nil, hashKey); l1CachedPayload != nil {
			w.Header().Set("Content-Type", "application/json")
			if _, writeErr := w.Write(l1CachedPayload); writeErr != nil {
				log.Printf("[Handler] Write-response error at Fast Cache Hit: %v\n", writeErr)
			}

			return
		}

		// 4. Prompts longer than 512 bytes are forward to LLM Provider.
		if len(promptBytes) > maxPromptLen {
			// TODO: Replace mock with real LLM Provider call.
			mockLLMResponse := []byte(`{"source": "llm", "text": "long_tail_response"}`)

			// fastcache.Set() is lock-free and uses memcopy at the OS level.
			l1Cache.Set(hashKey, mockLLMResponse)
			w.Header().Set("Content-Type", "application/json")
			if _, writeErr := w.Write(mockLLMResponse); writeErr != nil {
				log.Printf("[Handler] Write-response error at LLM payload: %v\n", writeErr)
			}

			return
		}

		// 5. Short prompts handled to Vector Engine via gRPC.
		ctx, cancel := context.WithTimeout(r.Context(), ServiceTimeout)
		defer cancel()

		grpcRes, rpcErr := stub.CheckCache(ctx, &pb.CheckCacheRequest{Prompt: apiReq.Prompt})
		if rpcErr != nil {
			log.Printf("[Handler] Read RPC error: %v\n", rpcErr)

			// TODO: LLM Provider call as fallback.
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		// 6. Marshal and return the JSON response to client.
		apiRes := CheckCacheAPIResponse{
			CheckState:    grpcRes.GetCheckState(),
			NodeID:        grpcRes.GetNodeId(),
			CachedPayload: grpcRes.GetCachedPayload(),
		}

		payloadBytes, marshErr := json.Marshal(&apiRes)
		if marshErr != nil {
			log.Printf("[Handler] Marshal error: %v\n", marshErr)
			http.Error(w, "Internal Server Error", http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		if _, writeErr := w.Write(payloadBytes); writeErr != nil {
			log.Printf("[Handler] Write-response error at Cached payload: %v\n", writeErr)
		}
	}
}
