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
	"path/filepath"
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
	socketAddress = "unix:///tmp/strix.sock"
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
	it shuts down the HTTP server gracefully, then the Death Pipe EOF signal
	causes the Vector Engine to exit cleanly on its own.`,
	RunE: runServe,
}

// strix/
// ├── bin/
// │   ├── strix_gateway  <- os.Executable()
// │   └── strix_engine
// └── engine/
//	   └── model/
//	       ├── strix-minilm-with-tokenizer.onnx
//	       └── libortextensions.so

type projectPaths struct {
	engineBinary string // strix/bin/strix_engine
	modelPath    string // strix/engine/model/strix-minilm-with-tokenizer.onnx
	ortLibPath   string // strix/engine/model/libortextensions.so
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

	// 2. Resolve artifact paths.
	paths, pathErr := resolvePaths()
	if pathErr != nil {
		return pathErr
	}

	log.Printf("[strix serve] Vector Engine binary: %s\n", paths.engineBinary)
	log.Printf("[strix serve] Inference model: %s\n", paths.modelPath)
	log.Printf("[strix serve] ORT extensions library: %s\n", paths.ortLibPath)

	// 3. Load ~/.strix/.env into the process environment.
	envPath, pathErr := EnvFilePath()
	if pathErr != nil {
		return pathErr
	}

	if loadErr := godotenv.Load(envPath); loadErr != nil {
		return fmt.Errorf("cannot load %s: %w", envPath, loadErr)
	}

	log.Println("[strix serve] Configuration loaded from .env")

	// 4. Fork a C++ process running Vector Engine.
	reader, writer, pipeErr := os.Pipe()
	if pipeErr != nil {
		return fmt.Errorf("cannot create Death Pipe: %w", pipeErr)
	}

	engineProc := exec.Command(VectorEngineBinPath)
	engineProc.ExtraFiles = []*os.File{reader}
	engineProc.Stdout = os.Stdout
	engineProc.Stderr = os.Stderr
	engineProc.Env = []string{
		"INFERENCE_MODEL_PATH=" + paths.modelPath,
		"ORT_EXTENSIONS_PATH=" + paths.ortLibPath,
	}

	if startErr := engineProc.Start(); startErr != nil {
		_ = reader.Close()
		_ = writer.Close()

		return fmt.Errorf("cannot start Vector Engine at %q: %w",
			VectorEngineBinPath, startErr,
		)
	}

	log.Printf("[strix serve] Vector Engine started (PID %d)", engineProc.Process.Pid)

	defer func() {
		if closeErr := writer.Close(); closeErr != nil {
			log.Printf(
				"[strix serve] CRITICAL: Death Pipe Write-end close failed: %v. "+
					"Sending SIGTERM as fallback\n",
				closeErr,
			)

			_ = engineProc.Process.Signal(syscall.SIGTERM)
		}
	}()

	if closeErr := reader.Close(); closeErr != nil {
		return fmt.Errorf("cannot close Read-end of the Death Pipe: %v", closeErr)
	}

	go func() {
		if waitErr := engineProc.Wait(); waitErr != nil {
			log.Printf("[strix serve] Vector Engine exited: %v", waitErr)
		} else {
			log.Println("[strix serve] Vector Engine exited cleanly.")
		}
	}()

	// 5. Initialize L1 Exact-match Cache.
	log.Printf(
		"[strix serve] Allocating %dMB off-heap memory for L1 Fast Cache...\n",
		l1CacheSize/(1024*1024),
	)
	l1Cache := fastcache.New(l1CacheSize)
	defer l1Cache.Reset()

	// 6.1. Open a single gRPC connection to Vector Engine
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

	// 6.2. Create only ONE Vector Engine stub.
	clientStub := pb.NewSemanticServiceClient(conn)

	// 7. Perform high concurrent warming up mechanism
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

	// 8. Register router (Server Mux).
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

	// 9. Create OS Signal listener.
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

	// 10. HTTP Gateway graceful shutdown.
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

func resolvePaths() (projectPaths, error) {
	execDir, dirErr := os.Executable()
	if dirErr != nil {
		return projectPaths{}, fmt.Errorf("cannot resolve executable path: %w", dirErr)
	}

	execDir, symErr := filepath.EvalSymlinks(execDir)
	if symErr != nil {
		return projectPaths{}, fmt.Errorf(
			"cannot eval symlink on executable path: %w", symErr,
		)
	}

	// Move one level up from bin/ to strix/
	projectRoot := filepath.Join(filepath.Dir(execDir), "..")

	return projectPaths{
		engineBinary: filepath.Join(projectRoot, "bin", "strix_engine"),
		modelPath: filepath.Join(
			projectRoot, "engine", "model", "strix-minilm-with-tokenizer.onnx",
		),
		ortLibPath: filepath.Join(
			projectRoot, "engine", "model", "libortextensions.so",
		),
	}, nil
}
