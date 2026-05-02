package proxy

import (
	"context"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestThunderingHerd_SingleLLMCall(t *testing.T) {
	const (
		numRequests = 100
		nodeID      = int32(42)
		wantPayload = `{"choices":[{"message":{"content":"mocked response"}}]}`
		llmLatency  = 30 * time.Millisecond
	)

	promise := pioneerRegister(nodeID)

	var (
		llmCallCount atomic.Int32

		mu         sync.Mutex
		gotPayload = make([]string, 0, numRequests-1)

		wg sync.WaitGroup
	)

	for i := 1; i < numRequests; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			payload, err, found := herdAwait(context.Background(), nodeID)

			if !found {
				t.Errorf("herd goroutine: promise not found — registry invariant violated")
				return
			}

			if err != nil {
				t.Errorf("herd goroutine: unexpected error: %v", err)
				return
			}

			mu.Lock()
			gotPayload = append(gotPayload, string(payload))
			mu.Unlock()
		}()
	}

	llmCallCount.Add(1)
	time.Sleep(llmLatency)
	pioneerFulfill(nodeID, promise, []byte(wantPayload), nil)

	mu.Lock()
	gotPayload = append(gotPayload, wantPayload)
	mu.Unlock()

	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case <-done:

	case <-time.After(5 * time.Second):
		t.Fatal("test timed out — goroutines leaked or deadlocked in herdAwait")
	}

	if got := llmCallCount.Load(); got != 1 {
		t.Errorf("LLM call count = %d, want 1: thundering herd not prevented", got)
	}

	mu.Lock()
	totalResults := len(gotPayload)
	mu.Unlock()

	if totalResults != numRequests {
		t.Errorf("received %d payloads, want %d", totalResults, numRequests)
	}

	for i, p := range gotPayload {
		if p != wantPayload {
			t.Errorf("payload[%d] = %q, want %q", i, p, wantPayload)
		}
	}
}
