package main

import (
	"context"
	pb "gateway/pb/proto"
	"log"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

const (
	socketAddress = "unix:///tmp/sentinel.sock"
	// Maximal model inference time is 20ms, service's SLA is 50ms,
	// so timeout must be between 20ms and 50ms.
	timeout = 30 * time.Millisecond
)

func main() {
	// 1. Setup connection using grpc.NewClient() (no TLS encryption)
	conn, connErr := grpc.NewClient(socketAddress, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if connErr != nil {
		log.Fatalf("cannot connect to %s\n", socketAddress)
	}

	defer func() {
		connCloseErr := conn.Close()
		if connCloseErr != nil {
			log.Print("cannot close connection\n")
		}
	}()

	// 2. Create a Client (Server's stub) using NewSemanticServiceClient()
	clientStub := pb.NewSemanticServiceClient(conn)

	// 3. Set timeout and resource handling
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	// 4. Make RPC request (with context), receive response
	response, rpcErr := clientStub.CheckCache(ctx, &pb.CheckCacheRequest{PromptText: "hello"})
	if rpcErr != nil {
		log.Printf("RPC error encountered: %v\n", rpcErr)
		return
	}

	log.Printf("Semantic Cache Result: %s %t", response.CachedPayload, response.IsHit)
}
