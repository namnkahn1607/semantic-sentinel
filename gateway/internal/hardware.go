package internal

import (
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"
)

const (
	minRAMBytes   = 8 * 1024 * 1024 * 1024
	maxVirtualCPU = 4
)

func Enforce() error {
	if ramErr := checkRAM(); ramErr != nil {
		return ramErr
	}

	applyCPULimit()
	return nil
}

func applyCPULimit() {
	numCPUs := runtime.NumCPU()
	runtime.GOMAXPROCS(min(maxVirtualCPU, numCPUs/2))
}

func checkRAM() error {
	memData, readErr := os.ReadFile("/proc/meminfo")
	if readErr != nil {
		return nil
	}

	for _, line := range strings.Split(string(memData), "\n") {
		if !strings.HasPrefix(line, "MemTotal:") {
			continue
		}

		fields := strings.Fields(line)
		if len(fields) < 2 {
			break
		}

		kb, parseErr := strconv.ParseInt(fields[1], 10, 64)
		if parseErr != nil {
			break
		}

		if totalBytes := kb * 1024; totalBytes < minRAMBytes {
			return fmt.Errorf(
				"insufficient RAM. Require at least %dMB", minRAMBytes/(1024*1024),
			)
		}

		return nil
	}

	return nil
}
