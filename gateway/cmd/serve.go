package cmd

import (
	"context"
	"errors"
	"fmt"
	"gateway/internal"
	pb "gateway/pb/proto"
	"log"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/VictoriaMetrics/fastcache"
	"github.com/joho/godotenv"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

const (
	socketAddress = "unix:///tmp/sentinel.sock"
	endpoint      = "/v1/cache/check"
	serverPort    = ":8080"
	maxFD         = 65536

	warmUpConcurrency = 100
	warmUpTimeout     = 50 * time.Millisecond
	coldStartTimeout  = 100 * time.Millisecond
	shutdownTimeout   = 3 * time.Second

	l1CacheSize = 256 * 1024 * 1024
)

var VectorEngineBinPath string

var serveCmd = &cobra.Command{
	Use:   "serve",
	Short: "Start the HTTP Gateway as supervisor and Vector Engine",
	Long: `Loads ~/.strix/.env, forks Vector Engine, and starts HTTP Gateway
	in the current process. Acts as a supervisor: on SIGTERM or SIGINT
	it shuts down the HTTP server gracefully, thus causing its child process 
	Vector Engine to exit gracefully on its own.`,
	RunE: runServe,
}

func init() {
	serveCmd.Flags().StringVar(
		&VectorEngineBinPath,
		"engine",
		"./vector_engine",
		"Path to the Vector Engine binary",
	)
}

func runServe(_ *cobra.Command, _ []string) error {
	// 1. Permission & hardware checks and FD expansion.
	if permErr := AssertEnvPermissions(); permErr != nil {
		return permErr
	}

	if hardwareErr := internal.Enforce(); hardwareErr != nil {
		return hardwareErr
	}

	if fdErr := openMoreFD(); fdErr != nil {
		return fdErr
	}

	// 2. Load ~/.strix/.env onto the process environment.
	envPath, pathErr := EnvFilePath()
	if pathErr != nil {
		return pathErr
	}

	if loadErr := godotenv.Load(envPath); loadErr != nil {
		return fmt.Errorf("cannot load %s: %w", envPath, loadErr)
	}

	log.Println("[strix serve] Configuration loaded from .env")

	// 3. Fork a C++ process running Vector Engine.
	engineCmd := exec.Command(VectorEngineBinPath)
	engineCmd.Stdout = os.Stdout
	engineCmd.Stdin = os.Stdin

	// Whenever the parent process dies, its children also die.
	engineCmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGTERM,
	}

	if startErr := engineCmd.Start(); startErr != nil {
		return fmt.Errorf("cannot start Vector Engine at %q: %w",
			VectorEngineBinPath, startErr,
		)
	}

	log.Printf("[strix serve] Vector Engine started (PID %d)", engineCmd.Process.Pid)

	// 4. Initialize L1 Exact-match Cache.
	log.Printf(
		"[strix serve] Allocating %dMB off-heap memory for L1 Fast Cache...\n",
		l1CacheSize/(1024*1024),
	)
	l1Cache := fastcache.New(l1CacheSize)
	defer l1Cache.Reset()

	// 5.1. Open a single gRPC connection to Vector Engine
	conn, connErr := grpc.NewClient(
		socketAddress, grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if connErr != nil {
		return connErr
	}

	defer func() {
		if closeErr := conn.Close(); closeErr != nil {
			log.Printf("[Serve] gRPC connection close error: %v\n", closeErr)
		} else {
			log.Println("[Serve] gRPC connection closed.")
		}
	}()

	// 5.2. Create only ONE Vector Engine stub.
	clientStub := pb.NewSemanticServiceClient(conn)

	// 6. Perform high concurrent warming up mechanism
	log.Println("Starting functional warm-up (Thread Pool expansion)...")
	pingCtx, pingCancel := context.WithTimeout(context.Background(), warmUpTimeout)
	if _, pingErr := clientStub.CheckCache(
		pingCtx, &pb.CheckCacheRequest{Prompt: "warm-up signal"},
	); pingErr != nil {
		pingCancel()
		return fmt.Errorf("warm-up ping failed: %w", pingErr)
	}

	pingCancel()

	var wg sync.WaitGroup
	for range warmUpConcurrency {
		wg.Go(func() {
			burstCtx, burstCancel := context.WithTimeout(context.Background(), coldStartTimeout)
			defer burstCancel()
			_, _ = clientStub.CheckCache(burstCtx, &pb.CheckCacheRequest{Prompt: "warm-up burst"})
		})
	}

	wg.Wait()
	log.Println("[strix serve] Warm-up completed.")

	// 7. Register router (Server Mux).
	mux := http.NewServeMux()
	mux.HandleFunc(endpoint, internal.HandleCheckCache(clientStub, l1Cache))

	server := &http.Server{
		Addr:    serverPort,
		Handler: mux,
		// Mitigate Slowloris attack
		ReadHeaderTimeout: 3 * time.Second,
		ReadTimeout:       5 * time.Second,
		WriteTimeout:      10 * time.Second,
	}

	// 8. Create OS Signal listener.
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	serverErrChan := make(chan error, 1)
	go func() {
		log.Printf("[strix serve] HTTP Server listening on port: %s\n", serverPort)
		if serverErr := server.ListenAndServe(); serverErr != nil && !errors.Is(
			serverErr, http.ErrServerClosed,
		) {
			serverErrChan <- serverErr
		}
	}()

	select {
	case err := <-serverErrChan:
		return fmt.Errorf("gateway crashed: %w", err)
	case sig := <-sigChan:
		log.Printf("[strix serve] Received signal %v. Initiating graceful shutdown...\n",
			sig,
		)
	}

	// 9. HTTP Gateway graceful shutdown.
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), shutdownTimeout)
	defer shutdownCancel()

	if shutdownErr := server.Shutdown(shutdownCtx); shutdownErr != nil {
		return fmt.Errorf("server shutdown failed: %w", shutdownErr)
	}

	log.Println("[strix serve] HTTP Server stopped.")
	return nil
}

func openMoreFD() error {
	var rLimit syscall.Rlimit
	if err := syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rLimit); err != nil {
		return fmt.Errorf("cannot read ulimit: %w", err)
	}

	if rLimit.Cur < maxFD {
		rLimit.Cur = maxFD
		rLimit.Max = max(rLimit.Max, maxFD)
		if err := syscall.Setrlimit(syscall.RLIMIT_NOFILE, &rLimit); err != nil {
			log.Printf(
				"[strix serve] OS refused to raise ulimit - may crash under high load: %v\n",
				err,
			)
		} else {
			log.Printf("[strix serve] FD limit raised to %d\n", maxFD)
		}
	}

	return nil
}
