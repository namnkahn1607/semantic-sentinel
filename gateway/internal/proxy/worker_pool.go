package proxy

import (
	"context"
	pb "gateway/pb/proto"
	"log"
	"time"
)

const (
	numWorkers  = 4
	jobQueueCap = 5000
	jobTimeout  = 50 * time.Millisecond
)

type Job struct {
	NodeID  int32
	Payload []byte
}

type WorkerPool struct {
	queue chan Job
}

func NewWorkerPool(stub pb.SemanticServiceClient) *WorkerPool {
	wp := &WorkerPool{
		queue: make(chan Job, jobQueueCap),
	}

	for range numWorkers {
		go wp.runWorker(stub)
	}

	return wp
}

func (wp *WorkerPool) TryEnqueue(nodeID int32, payload []byte) {
	select {
	case wp.queue <- Job{NodeID: nodeID, Payload: payload}:
	default:
	}
}

func (wp *WorkerPool) Stop() {
	close(wp.queue)
}

func (wp *WorkerPool) runWorker(stub pb.SemanticServiceClient) {
	for job := range wp.queue {
		ctx, cancel := context.WithTimeout(context.Background(), jobTimeout)

		_, rpcErr := stub.SetCache(ctx, &pb.SetCacheRequest{
			NodeId:          job.NodeID,
			UncachedPayload: job.Payload,
		})

		cancel()

		if rpcErr != nil {
			log.Printf("[Worker Pool] RPC Write failed for nodeID = %d: %v\n",
				job.NodeID, rpcErr,
			)
		}
	}
}
